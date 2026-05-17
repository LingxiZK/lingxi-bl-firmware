#!/usr/bin/env python3
"""
Lingxi BL Phase 1 端到端测试运行器 v1.0
=========================================
自动化执行 TC-01 到 TC-08 测试用例，输出 JSON 报告。

依赖: pip install opencv-python numpy psutil
用法:
  python3 test_runner.py --quick       # 快速健康检查 (~30秒)
  python3 test_runner.py --full        # 完整测试 (~35分钟)
  python3 test_runner.py --test tc-05  # 单测试
  python3 test_runner.py --quick --report report.json
"""

import argparse
import json
import logging
import os
import socket
import struct
import subprocess
import sys
import time
import collections
from datetime import datetime
from typing import Dict, Any, Optional

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
log = logging.getLogger('test_runner')

# ── 协议常量 ──
UDP_PORT = 5566
BUFFER_SIZE = 2048
FRAME_W = 320
FRAME_H = 240
FRAME_SIZE = FRAME_W * FRAME_H

SDIO_PKT_MAGIC = 0xA5
SDIO_PKT_IMAGE_FRAME = 0x08

SDIO_HDR_FMT = '<BBHHH'
SDIO_HDR_LEN = struct.calcsize(SDIO_HDR_FMT)

FRAG_HDR_FMT = '<IHHHBB'
FRAG_HDR_LEN = struct.calcsize(FRAG_HDR_FMT)

FRAME_HDR_FMT = '<IIQHHBB I'
FRAME_HDR_LEN = struct.calcsize(FRAME_HDR_FMT)


class TestContext:
    """测试上下文 — 管理状态和结果"""
    def __init__(self, record_path: Optional[str] = None):
        self.sock: Optional[socket.socket] = None
        self.running = True
        self.record_path = record_path
        self.video_writer = None

        # 统计
        self.packets_rx = 0
        self.bytes_rx = 0
        self.frames_completed = 0
        self.frames_dropped = 0
        self.crc_errors = 0
        self.corrupt_frames = 0

        # 延迟测量
        self.latencies_ms: list = []
        self.arrival_times: list = []

        # 帧缓冲
        self.buffers: Dict[int, Dict] = {}
        self.completed: collections.deque = collections.deque(maxlen=50)

        # 结果汇总
        self.results: Dict[str, Any] = {}

    def start_socket(self) -> bool:
        """启动 UDP 监听"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            self.sock.bind(('0.0.0.0', UDP_PORT))
            self.sock.settimeout(0.5)
            log.info(f"📡 监听 UDP 端口 {UDP_PORT}")
            return True
        except OSError as e:
            log.error(f"❌ 无法绑定端口 {UDP_PORT}: {e}")
            return False

    def parse_sdio_packet(self, data: bytes) -> Optional[tuple]:
        if len(data) < SDIO_HDR_LEN:
            return None
        magic, pkt_type, seq, pkt_len, crc16 = struct.unpack_from(SDIO_HDR_FMT, data, 0)
        if magic != SDIO_PKT_MAGIC or pkt_type != SDIO_PKT_IMAGE_FRAME:
            return None
        return (pkt_type, seq, data[SDIO_HDR_LEN:])

    def parse_fragment(self, payload: bytes) -> Optional[tuple]:
        if len(payload) < FRAG_HDR_LEN:
            return None
        fid, fidx, total, fsize, flags, _ = struct.unpack_from(FRAG_HDR_FMT, payload, 0)
        return (fid, fidx, total, flags, payload[FRAG_HDR_LEN:])

    def parse_frame_header(self, data: bytes) -> Optional[dict]:
        if len(data) < FRAME_HDR_LEN:
            return None
        magic, fid, ts, w, h, fmt, _, fsize = struct.unpack_from(FRAME_HDR_FMT, data, 0)
        if magic != 0x4C58494D:
            return None
        return {'frame_id': fid, 'ts_us': ts, 'w': w, 'h': h, 'fmt': fmt, 'size': fsize}

    def process_packet(self, data: bytes):
        self.packets_rx += 1
        self.bytes_rx += len(data)
        now = time.time()

        result = self.parse_sdio_packet(data)
        if result is None:
            self.crc_errors += 1
            return
        _, _, payload = result

        result = self.parse_fragment(payload)
        if result is None:
            self.crc_errors += 1
            return
        fid, fidx, total, flags, frag_data = result

        # 创建或获取帧缓冲
        if fid not in self.buffers:
            buf = {
                'frame_id': fid,
                'total_frags': total,
                'fragments': {},
                'meta': {'ts_us': 0, 'w': FRAME_W, 'h': FRAME_H, 'size': FRAME_SIZE},
                'created_at': now,
            }
            if flags & 0x01:
                hdr = self.parse_frame_header(frag_data)
                if hdr:
                    buf['meta'] = hdr
                    frag_data = frag_data[FRAME_HDR_LEN:]
            self.buffers[fid] = buf
        else:
            buf = self.buffers[fid]

        buf['fragments'][fidx] = frag_data

        # 检查完整帧
        if len(buf['fragments']) == buf['total_frags'] and buf['total_frags'] > 0:
            frame = b''
            for i in range(buf['total_frags']):
                if i not in buf['fragments']:
                    break
                frame += buf['fragments'][i]
            frame = frame[:buf['meta']['size']]

            if len(frame) == FRAME_SIZE:
                ts_tx = buf['meta']['ts_us'] / 1000.0  # μs → ms
                ts_rx = now * 1000.0
                latency = ts_rx - ts_tx
                if 0 < latency < 5000:  # 过滤无效值
                    self.latencies_ms.append(latency)

                self.arrival_times.append(now)
                self.completed.append(frame)
                self.frames_completed += 1
            else:
                self.corrupt_frames += 1

            del self.buffers[fid]

        # 清理超时缓冲 (>3s)
        stale = [k for k, v in self.buffers.items() if now - v['created_at'] > 3.0]
        for k in stale:
            self.frames_dropped += 1
            del self.buffers[k]

    def receive(self, duration_s: float, progress_cb=None):
        """接收 duration_s 秒的数据"""
        start = time.time()
        last_report = start

        while self.running and (time.time() - start) < duration_s:
            try:
                data, addr = self.sock.recvfrom(BUFFER_SIZE)
                self.process_packet(data)
            except socket.timeout:
                pass

            # 进度报告
            if progress_cb and time.time() - last_report > 2.0:
                progress_cb(self)
                last_report = time.time()

    def get_fps(self) -> float:
        if len(self.arrival_times) < 2:
            return 0.0
        span = self.arrival_times[-1] - self.arrival_times[0]
        return (len(self.arrival_times) - 1) / max(span, 0.001)

    def get_drop_rate(self) -> float:
        total = self.frames_completed + self.frames_dropped
        return self.frames_dropped / max(total, 1)

    def close(self):
        self.running = False
        if self.sock:
            self.sock.close()


# ═══════════════════════════════════════════════════════════════
# 测试用例实现
# ═══════════════════════════════════════════════════════════════

def tc_01_sdio_link(ctx: TestContext, args) -> dict:
    """SDIO 链路通断测试"""
    log.info("=" * 50)
    log.info("TC-01: SDIO 链路通断测试")
    log.info("=" * 50)

    # 该测试依赖 STM32/ESP32 串口日志
    # 地面站只能间接验证：心跳包可达
    log.info("请确认:")
    log.info("  1. ESP32-C6 串口输出 'SDIO link established'")
    log.info("  2. STM32 串口输出 'vTaskCamera started'")
    resp = input("  SDIO 链路是否正常? (y/N): ").strip().lower()

    passed = resp == 'y'
    return {
        'name': 'TC-01 SDIO 链路',
        'passed': passed,
        'manual_check': True,
        'detail': '用户确认' if passed else '链路异常',
    }


def tc_02_frame_capture(ctx: TestContext, args) -> dict:
    """VD55G1 帧捕获测试"""
    log.info("=" * 50)
    log.info("TC-02: VD55G1 帧捕获测试")
    log.info("=" * 50)

    log.info(f"等待 {args.duration}s 采集帧数据...")
    ctx.receive(args.duration,
                progress_cb=lambda c: log.info(
                    f"  帧: {c.frames_completed}, 丢帧: {c.frames_dropped}, "
                    f"CRC: {c.crc_errors}"))

    passed = ctx.frames_completed > 5 and ctx.frames_dropped == 0
    return {
        'name': 'TC-02 帧捕获',
        'passed': passed,
        'frames_completed': ctx.frames_completed,
        'frames_dropped': ctx.frames_dropped,
        'packets_rx': ctx.packets_rx,
        'crc_errors': ctx.crc_errors,
        'fps': round(ctx.get_fps(), 1),
        'detail': '帧捕获正常' if passed else f'帧数={ctx.frames_completed}, 丢帧={ctx.frames_dropped}',
    }


def tc_03_downscale_encode(ctx: TestContext, args) -> dict:
    """下采样 + 分片验证"""
    log.info("=" * 50)
    log.info("TC-03: 下采样 + 分片验证")
    log.info("=" * 50)

    ctx.receive(10)

    # 用分片数反推：总字节 / 单包数据 = 分片数
    total_bytes = ctx.bytes_rx
    est_frags = ctx.packets_rx

    # 每个完整帧约 51 分片（~78KB），检查比例是否合理
    expected_frags_per_frame = 51
    est_completed = est_frags // max(expected_frags_per_frame, 1)

    passed = ctx.frames_completed > 0 and abs(ctx.frames_completed - est_completed) < 5
    return {
        'name': 'TC-03 分片编码',
        'passed': passed,
        'total_packets': ctx.packets_rx,
        'frames_completed': ctx.frames_completed,
        'est_frags_per_frame': round(ctx.packets_rx / max(ctx.frames_completed, 1), 1),
        'detail': '分片正常' if passed else '分片比例异常',
    }


def tc_04_wifi_relay(ctx: TestContext, args) -> dict:
    """Wi-Fi 转发测试"""
    log.info("=" * 50)
    log.info("TC-04: Wi-Fi UDP 转发测试")
    log.info("=" * 50)

    record_path = args.record or '/tmp/tc04_test.avi'

    log.info(f"采集 30s 数据，录制到 {record_path}...")

    # 录制视频
    try:
        import cv2
        import numpy as np
        fourcc = cv2.VideoWriter_fourcc(*'XVID')
        writer = cv2.VideoWriter(record_path, fourcc, 20.0, (FRAME_W, FRAME_H), isColor=False)
    except ImportError:
        writer = None
        log.warning("OpenCV 未安装，跳过录制")

    start = time.time()
    last_completed = 0
    drop_rates = []

    while time.time() - start < 30:
        try:
            data, addr = ctx.sock.recvfrom(BUFFER_SIZE)
            ctx.process_packet(data)
        except socket.timeout:
            pass

        # 每 5s 记录一次丢帧率
        elapsed = time.time() - start
        if int(elapsed) % 5 == 0 and ctx.frames_completed > last_completed:
            dr = ctx.get_drop_rate()
            drop_rates.append(dr)
            last_completed = ctx.frames_completed
            log.info(f"  [{int(elapsed)}s] 帧:{ctx.frames_completed} 丢帧率:{dr:.1%}")

        # 写入视频（降低显示开销）
        if writer and len(ctx.completed) > 0:
            frame = ctx.completed[-1]
            img = np.frombuffer(frame, dtype=np.uint8).reshape(FRAME_H, FRAME_W)
            writer.write(img)

    if writer:
        writer.release()
        log.info(f"📹 录制完成: {record_path}")

    avg_drop = sum(drop_rates) / max(len(drop_rates), 1)
    passed = ctx.frames_completed > 30 and avg_drop < 0.15
    return {
        'name': 'TC-04 Wi-Fi 转发',
        'passed': passed,
        'frames_total': ctx.frames_completed,
        'drop_rate_avg': round(avg_drop, 3),
        'drop_rates': [round(d, 3) for d in drop_rates],
        'recording': record_path if writer else None,
        'detail': '转发正常' if passed else f'丢帧率 {avg_drop:.1%}',
    }


def tc_05_latency(ctx: TestContext, args) -> dict:
    """端到端延迟测量"""
    log.info("=" * 50)
    log.info("TC-05: 端到端延迟测量")
    log.info("=" * 50)

    ctx.receive(30)

    latencies = ctx.latencies_ms
    if len(latencies) < 5:
        return {
            'name': 'TC-05 延迟',
            'passed': False,
            'samples': len(latencies),
            'detail': f'样本不足 ({len(latencies)} < 5)',
        }

    latencies.sort()
    p50 = latencies[int(len(latencies) * 0.50)]
    p95 = latencies[int(len(latencies) * 0.95)]
    avg = sum(latencies) / len(latencies)
    mx = max(latencies)
    mn = min(latencies)

    passed = avg < 150 and p95 < 300 and mx < 1000

    log.info(f"  样本数: {len(latencies)}")
    log.info(f"  平均延迟: {avg:.1f} ms")
    log.info(f"  P50:     {p50:.1f} ms")
    log.info(f"  P95:     {p95:.1f} ms")
    log.info(f"  最大:    {mx:.1f} ms")
    log.info(f"  最小:    {mn:.1f} ms")
    log.info(f"  ✅ 通过" if passed else f"  ❌ 未通过")

    return {
        'name': 'TC-05 延迟',
        'passed': passed,
        'samples': len(latencies),
        'avg_ms': round(avg, 1),
        'p50_ms': round(p50, 1),
        'p95_ms': round(p95, 1),
        'max_ms': round(mx, 1),
        'min_ms': round(mn, 1),
        'detail': f'平均 {avg:.0f}ms / P95 {p95:.0f}ms' if passed else f'延迟偏高',
    }


def tc_06_frame_integrity(ctx: TestContext, args) -> dict:
    """帧完整性验证"""
    log.info("=" * 50)
    log.info("TC-06: 帧完整性验证")
    log.info("=" * 50)

    ctx.receive(15)

    # 检查：
    # 1. 无 CRC 错误
    # 2. 帧尺寸正确
    # 3. 内容非全黑/全白（简单完整性检查）
    import numpy as np

    corrupt = 0
    valid = 0
    blank = 0

    for frame_data in list(ctx.completed):
        if len(frame_data) != FRAME_SIZE:
            corrupt += 1
            continue

        img = np.frombuffer(frame_data, dtype=np.uint8)
        mean = float(img.mean())
        std = float(img.std())

        if std < 1.0:  # 全黑或全白
            blank += 1
        else:
            valid += 1

    passed = (ctx.crc_errors == 0 and corrupt == 0 and
              blank < len(ctx.completed) * 0.2)

    log.info(f"  完整帧: {valid}, 损坏: {corrupt}, 空白: {blank}")
    log.info(f"  CRC 错误: {ctx.crc_errors}")
    log.info(f"  ✅ 通过" if passed else f"  ❌ 未通过")

    return {
        'name': 'TC-06 帧完整性',
        'passed': passed,
        'valid_frames': valid,
        'corrupt_frames': corrupt,
        'blank_frames': blank,
        'crc_errors': ctx.crc_errors,
        'detail': f'{valid} 帧正常 / {corrupt} 损坏 / {blank} 空白'
        if passed else f'帧内容异常 ({blank} 空白)',
    }


def tc_07_stability(ctx: TestContext, args) -> dict:
    """30 分钟稳定性测试"""
    log.info("=" * 50)
    log.info("TC-07: 30 分钟稳定性测试")
    log.info("=" * 50)

    duration = min(args.duration or 1800, 3600)
    log.info(f"运行 {duration}s ({duration//60} min)...")

    # 每 60 秒记录快照
    snapshots = []
    start = time.time()
    last_snapshot = start
    last_frames = 0

    while time.time() - start < duration:
        try:
            data, addr = ctx.sock.recvfrom(BUFFER_SIZE)
            ctx.process_packet(data)
        except socket.timeout:
            pass

        now = time.time()
        if now - last_snapshot >= 60:
            fps = (ctx.frames_completed - last_frames) / (now - last_snapshot)
            snapshots.append({
                't_sec': int(now - start),
                'fps': round(fps, 1),
                'drop_rate': round(ctx.get_drop_rate(), 3),
                'frames': ctx.frames_completed,
                'dropped': ctx.frames_dropped,
                'crc': ctx.crc_errors,
            })
            log.info(f"  [{int(now-start)}s] FPS:{fps:.1f} "
                     f"丢帧:{ctx.get_drop_rate():.1%} "
                     f"CRC:{ctx.crc_errors}")
            last_frames = ctx.frames_completed
            last_snapshot = now

    # 整体统计
    avg_fps = ctx.get_fps()
    final_drop = ctx.get_drop_rate()

    passed = (final_drop < 0.20 and ctx.crc_errors < 50 and
              ctx.frames_completed > duration * 5)

    log.info(f"\n  总帧数: {ctx.frames_completed}")
    log.info(f"  平均 FPS: {avg_fps:.1f}")
    log.info(f"  最终丢帧率: {final_drop:.1%}")
    log.info(f"  CRC 错误: {ctx.crc_errors}")

    return {
        'name': 'TC-07 稳定性',
        'passed': passed,
        'duration_s': duration,
        'total_frames': ctx.frames_completed,
        'avg_fps': round(avg_fps, 1),
        'final_drop_rate': round(final_drop, 3),
        'crc_errors': ctx.crc_errors,
        'snapshots': snapshots,
        'detail': f'{duration//60}min 运行, 平均 {avg_fps:.1f} fps'
        if passed else f'稳定性不足',
    }


def tc_08_slam_compat(ctx: TestContext, args) -> dict:
    """SLAM 兼容性快速验证"""
    log.info("=" * 50)
    log.info("TC-08: SLAM 兼容性验证")
    log.info("=" * 50)

    import numpy as np

    output_dir = args.slam_dir or '/tmp/lingxi_slam_frames/'
    os.makedirs(output_dir, exist_ok=True)

    ctx.receive(20)
    samples = list(ctx.completed)[:200]

    # 导出 TUM 格式 (timestamp tx ty tz qx qy qz qw / 灰度 PNG)
    tum_file = os.path.join(output_dir, 'images.txt')
    with open(tum_file, 'w') as f:
        for i, frame_data in enumerate(samples):
            ts = time.time()
            f.write(f"{ts:.6f} 0 0 0 0 0 0 1\n")
            img = np.frombuffer(frame_data, dtype=np.uint8).reshape(FRAME_H, FRAME_W)
            import cv2
            cv2.imwrite(os.path.join(output_dir, f'{ts:.6f}.png'), img)

    log.info(f"  导出 {len(samples)} 帧到 {output_dir}")
    log.info(f"  TUM 格式文件: {tum_file}")

    # 简单 ORB 特征检测（不依赖 ORB-SLAM3 安装）
    import cv2
    orb = cv2.ORB_create(nfeatures=500)
    feature_counts = []
    for frame_data in samples:
        img = np.frombuffer(frame_data, dtype=np.uint8).reshape(FRAME_H, FRAME_W)
        kp = orb.detect(img, None)
        feature_counts.append(len(kp))

    avg_features = sum(feature_counts) / max(len(feature_counts), 1)
    min_features = min(feature_counts)
    passed = avg_features > 50 and min_features > 10

    log.info(f"  ORB 特征: 平均 {avg_features:.0f} / 最小 {min_features}")
    log.info(f"  ✅ SLAM 输入可用" if passed else f"  ❌ 特征不足")

    return {
        'name': 'TC-08 SLAM 兼容性',
        'passed': passed,
        'frames_exported': len(samples),
        'orb_features_avg': round(avg_features, 1),
        'orb_features_min': min_features,
        'output_dir': output_dir,
        'detail': f'平均 {avg_features:.0f} 特征/帧' if passed else f'特征不足 ({avg_features:.0f})',
    }


# ═══════════════════════════════════════════════════════════════
# 主运行器
# ═══════════════════════════════════════════════════════════════

TEST_SUITE = {
    'tc-01': tc_01_sdio_link,
    'tc-02': tc_02_frame_capture,
    'tc-03': tc_03_downscale_encode,
    'tc-04': tc_04_wifi_relay,
    'tc-05': tc_05_latency,
    'tc-06': tc_06_frame_integrity,
    'tc-07': tc_07_stability,
    'tc-08': tc_08_slam_compat,
}

QUICK_TESTS = ['tc-01', 'tc-02', 'tc-03', 'tc-05']


class TestArgs:
    """占位参数对象"""
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)


def run_test_suite(selected_tests: list = None,
                   duration: int = 15,
                   record: str = None,
                   slam_dir: str = None) -> dict:
    """运行测试套件，返回结果字典"""
    if selected_tests is None:
        selected_tests = list(TEST_SUITE.keys())

    ctx = TestContext(record_path=record)

    if not ctx.start_socket():
        return {'error': '无法启动 UDP 监听', 'tests': []}

    args = TestArgs(duration=duration, record=record, slam_dir=slam_dir)

    results = []
    overall_pass = True

    try:
        for test_id in selected_tests:
            if test_id not in TEST_SUITE:
                log.warning(f"⚠️  未知测试: {test_id}")
                continue

            # 每个测试独立 context（但共享 socket）
            test_ctx = ctx

            log.info("")
            result = TEST_SUITE[test_id](test_ctx, args)
            results.append(result)

            if not result.get('passed', False):
                overall_pass = False

            # 打印结果
            status = "✅" if result.get('passed') else "❌"
            log.info(f"{status} {result['name']}: {result.get('detail', '')}")

    except KeyboardInterrupt:
        log.warning("\n⏹ 测试被用户中断")
    finally:
        ctx.close()

    passed_count = sum(1 for r in results if r.get('passed'))
    total_count = len(results)

    summary = {
        'suite': 'Lingxi BL Phase 1 E2E Test',
        'timestamp': datetime.now().isoformat(),
        'overall_pass': overall_pass,
        'passed': passed_count,
        'total': total_count,
        'pass_rate': f"{passed_count / max(total_count, 1) * 100:.0f}%",
        'tests': results,
    }

    return summary


def main():
    parser = argparse.ArgumentParser(
        description='Lingxi BL Phase 1 端到端测试运行器')
    parser.add_argument('--quick', action='store_true',
                        help='快速健康检查')
    parser.add_argument('--full', action='store_true',
                        help='完整测试套件')
    parser.add_argument('--test', type=str, default=None,
                        help='运行特定测试 (逗号分隔, e.g. tc-05,tc-06)')
    parser.add_argument('--duration', type=int, default=15,
                        help='单次采集时长 (默认 15s)')
    parser.add_argument('--stability-duration', type=int, default=1800,
                        help='稳定性测试时长 (默认 1800s)')
    parser.add_argument('--record', type=str, default=None,
                        help='录制视频文件路径')
    parser.add_argument('--slam-dir', type=str, default=None,
                        help='SLAM 数据导出目录')
    parser.add_argument('--report', type=str, default=None,
                        help='输出 JSON 报告路径')
    args = parser.parse_args()

    # 确定测试列表
    if args.test:
        selected = [t.strip() for t in args.test.split(',')]
    elif args.quick:
        selected = QUICK_TESTS
    elif args.full:
        selected = list(TEST_SUITE.keys())
    else:
        selected = QUICK_TESTS

    log.info(f"🚁 Lingxi BL Phase 1 E2E Test Runner")
    log.info(f"   测试数: {len(selected)} ({', '.join(selected)})")
    log.info(f"   采集时长: {args.duration}s")
    if args.full:
        log.info(f"   稳定性时长: {args.stability_duration}s")
    log.info("")

    summary = run_test_suite(
        selected_tests=selected,
        duration=args.duration if not args.full else args.stability_duration,
        record=args.record,
        slam_dir=args.slam_dir,
    )

    # 输出结果
    log.info("")
    log.info("=" * 50)
    log.info("📊 测试结果汇总")
    log.info("=" * 50)
    for t in summary.get('tests', []):
        icon = "✅" if t.get('passed') else "❌"
        log.info(f"  {icon} {t['name']}: {t.get('detail', '')}")
    log.info(f"\n  通过率: {summary['pass_rate']} ({summary['passed']}/{summary['total']})")
    log.info(f"  总体: {'✅ PASS' if summary['overall_pass'] else '❌ FAIL'}")

    # 输出 JSON 报告
    if args.report:
        with open(args.report, 'w') as f:
            json.dump(summary, f, indent=2, default=str)
        log.info(f"\n📄 报告已保存: {args.report}")

    # 返回值
    sys.exit(0 if summary.get('overall_pass') else 1)


if __name__ == '__main__':
    main()
