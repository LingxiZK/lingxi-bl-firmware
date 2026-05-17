#!/usr/bin/env python3
"""
Lingxi BL 地面站 — 视频流接收与 SLAM 前端 v1.0
=================================================
接收无人机 Wi-Fi UDP 传输的图像帧分片，重组为灰度帧，支持：
1. 实时预览显示
2. 帧率/延迟统计
3. 保存为视频文件
4. (可选) ORB-SLAM3 数据源

协议:
  一发一整帧 = 多个 UDP 分片
  每个分片 = sdio_packet_t (8字节头 + 负载)
  分片头 = app_stream_fragment_header_t (12字节)
  首分片含 app_stream_frame_header_t (28字节)
  
用法:
  python3 ground_station.py [--port 5566] [--display] [--record out.avi]
"""

import socket
import struct
import argparse
import time
import threading
import collections
from dataclasses import dataclass, field
from typing import Optional, Dict, List

# ── 协议常量 (与 STM32N6 端一致) ──
SDIO_PKT_MAGIC = 0xA5
SDIO_PKT_IMAGE_FRAME = 0x08

# sdio_packet_t header: magic(1) + type(1) + seq(2) + len(2) + crc16(2) = 8 bytes
SDIO_HEADER_FMT = '<BBHHH'  # magic, type, seq, len, crc16
SDIO_HEADER_SIZE = struct.calcsize(SDIO_HEADER_FMT)

# app_stream_fragment_header_t: frame_id(4) + frag_idx(2) + total_frags(2) + frag_size(2) + flags(1) + reserved(1) = 12
FRAG_HEADER_FMT = '<IHHHBB'
FRAG_HEADER_SIZE = struct.calcsize(FRAG_HEADER_FMT)

# app_stream_frame_header_t (仅首分片): magic(4) + frame_id(4) + ts(8) + w(2) + h(2) + fmt(1) + reserved(1) + size(4) = 28
FRAME_HEADER_FMT = '<IIQHHBB I'
FRAME_HEADER_SIZE = struct.calcsize(FRAME_HEADER_FMT)

# 图像尺寸
DEFAULT_WIDTH = 320
DEFAULT_HEIGHT = 240
DEFAULT_FRAME_SIZE = DEFAULT_WIDTH * DEFAULT_HEIGHT


@dataclass
class FrameBuffer:
    """单帧重组缓冲"""
    frame_id: int = 0
    total_frags: int = 0
    fragments: Dict[int, bytes] = field(default_factory=dict)
    timestamp_us: int = 0
    width: int = DEFAULT_WIDTH
    height: int = DEFAULT_HEIGHT
    format: int = 0  # 0=RAW8
    frame_size: int = DEFAULT_FRAME_SIZE
    created_at: float = 0.0

    def is_complete(self) -> bool:
        return len(self.fragments) == self.total_frags and self.total_frags > 0

    def assemble(self) -> Optional[bytes]:
        """按分片索引顺序组装完整帧"""
        if not self.is_complete():
            return None
        data = b''
        for i in range(self.total_frags):
            if i not in self.fragments:
                return None
            data += self.fragments[i]
        return data[:self.frame_size]


class GroundStation:
    """地面站主类"""

    def __init__(self, port: int = 5566, display: bool = True,
                 record: Optional[str] = None):
        self.port = port
        self.display = display
        self.record_path = record
        self.running = False

        # 帧缓冲管理
        self.frame_buffers: Dict[int, FrameBuffer] = {}
        self.lock = threading.Lock()
        self.completed_frames: collections.deque = collections.deque(maxlen=10)

        # 统计
        self.stats = {
            'packets_rx': 0,
            'frames_completed': 0,
            'frames_dropped': 0,
            'bytes_rx': 0,
            'last_fps': 0.0,
            'last_frame_ts': 0.0,
        }
        self.stats_lock = threading.Lock()

        # 预览窗口
        self.window_name = 'Lingxi BL - Ground Station'
        self.video_writer = None

    def _parse_sdio_packet(self, data: bytes) -> Optional[tuple]:
        """解析 SDIO 包，返回 (type, seq, payload) 或 None"""
        if len(data) < SDIO_HEADER_SIZE:
            return None
        magic, pkt_type, seq, pkt_len, crc16 = struct.unpack_from(
            SDIO_HEADER_FMT, data, 0)

        if magic != SDIO_PKT_MAGIC:
            return None
        if pkt_type != SDIO_PKT_IMAGE_FRAME:
            return None

        payload = data[SDIO_HEADER_SIZE:]
        return (pkt_type, seq, payload)

    def _parse_fragment(self, payload: bytes) -> Optional[tuple]:
        """解析分片头，返回 (frame_id, frag_idx, total, flags, frag_data)"""
        if len(payload) < FRAG_HEADER_SIZE:
            return None
        frame_id, frag_idx, total, frag_size, flags, _ = struct.unpack_from(
            FRAG_HEADER_FMT, payload, 0)
        frag_data = payload[FRAG_HEADER_SIZE:]
        return (frame_id, frag_idx, total, flags, frag_data)

    def _parse_frame_header(self, frag_data: bytes) -> Optional[dict]:
        """从首分片数据中解析帧头"""
        if len(frag_data) < FRAME_HEADER_SIZE:
            return None
        (magic, frame_id, ts, w, h, fmt, _reserved,
         frame_size) = struct.unpack_from(FRAME_HEADER_FMT, frag_data, 0)
        if magic != 0x4C58494D:  # "LXIM"
            return None
        return {
            'frame_id': frame_id,
            'timestamp_us': ts,
            'width': w,
            'height': h,
            'format': fmt,
            'frame_size': frame_size,
        }

    def _process_packet(self, data: bytes, addr: tuple):
        """处理一个 UDP 数据包"""
        with self.stats_lock:
            self.stats['packets_rx'] += 1
            self.stats['bytes_rx'] += len(data)

        # 解析 SDIO 包
        result = self._parse_sdio_packet(data)
        if result is None:
            return
        _, seq, payload = result

        # 解析分片头
        result = self._parse_fragment(payload)
        if result is None:
            return
        frame_id, frag_idx, total, flags, frag_data = result

        with self.lock:
            # 获取或创建帧缓冲
            if frame_id not in self.frame_buffers:
                buf = FrameBuffer(
                    frame_id=frame_id,
                    total_frags=total,
                    created_at=time.time()
                )

                # 首分片含帧头
                if flags & 0x01:
                    hdr = self._parse_frame_header(frag_data)
                    if hdr:
                        buf.timestamp_us = hdr['timestamp_us']
                        buf.width = hdr['width']
                        buf.height = hdr['height']
                        buf.frame_size = hdr['frame_size']
                        # 帧头后的数据才是有用图像
                        frag_data = frag_data[FRAME_HEADER_SIZE:]

                self.frame_buffers[frame_id] = buf
            else:
                buf = self.frame_buffers[frame_id]

            # 存储分片
            buf.fragments[frag_idx] = frag_data

            # 检查是否完整
            if buf.is_complete():
                frame_data = buf.assemble()
                if frame_data:
                    self.completed_frames.append({
                        'data': frame_data,
                        'width': buf.width,
                        'height': buf.height,
                        'frame_id': buf.frame_id,
                        'timestamp_us': buf.timestamp_us,
                    })
                    with self.stats_lock:
                        self.stats['frames_completed'] += 1
                        self.stats['last_frame_ts'] = time.time()
                # 清理已完成的缓冲
                del self.frame_buffers[frame_id]

        # 清理超时缓冲 (>5秒)
        now = time.time()
        with self.lock:
            stale = [fid for fid, buf in self.frame_buffers.items()
                     if now - buf.created_at > 5.0]
            for fid in stale:
                with self.stats_lock:
                    self.stats['frames_dropped'] += 1
                del self.frame_buffers[fid]

    def _display_loop(self):
        """显示循环"""
        import cv2
        import numpy as np

        cv2.namedWindow(self.window_name, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(self.window_name, 640, 480)

        fps_counter = collections.deque(maxlen=30)
        last_frame_time = time.time()

        if self.record_path:
            fourcc = cv2.VideoWriter_fourcc(*'XVID')
            self.video_writer = cv2.VideoWriter(
                self.record_path, fourcc, 30.0,
                (DEFAULT_WIDTH, DEFAULT_HEIGHT), isColor=False
            )

        while self.running:
            if len(self.completed_frames) > 0:
                frame_info = self.completed_frames.popleft()
                frame_data = frame_info['data']

                # 重建图像
                img = np.frombuffer(frame_data, dtype=np.uint8).reshape(
                    frame_info['height'], frame_info['width'])

                # 计算 FPS
                now = time.time()
                fps_counter.append(now - last_frame_time)
                last_frame_time = now
                avg_fps = 1.0 / (sum(fps_counter) / max(len(fps_counter), 1))

                # 叠加状态信息
                display_img = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
                cv2.putText(display_img,
                            f"FPS: {avg_fps:.1f}  Frame: {frame_info['frame_id']}  "
                            f"Size: {frame_info['width']}x{frame_info['height']}",
                            (8, 20), cv2.FONT_HERSHEY_SIMPLEX,
                            0.5, (0, 255, 0), 1)
                cv2.putText(display_img,
                            f"RX: {self.stats['packets_rx']}pkts  "
                            f"Drop: {self.stats['frames_dropped']}",
                            (8, 40), cv2.FONT_HERSHEY_SIMPLEX,
                            0.5, (0, 200, 200), 1)

                cv2.imshow(self.window_name, display_img)

                # 录制
                if self.video_writer:
                    self.video_writer.write(img)

            # 等待按键或超时
            key = cv2.waitKey(10) & 0xFF
            if key == ord('q') or key == 27:  # q 或 ESC
                self.running = False
                break

        cv2.destroyAllWindows()
        if self.video_writer:
            self.video_writer.release()

    def run(self):
        """启动地面站"""
        self.running = True

        # UDP 接收
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
        sock.bind(('0.0.0.0', self.port))
        sock.settimeout(1.0)  # 1秒超时用于检查 running 标志

        print(f"🚁 Lingxi BL Ground Station")
        print(f"📡 Listening on UDP port {self.port}")
        print(f"📺 Display: {'ON' if self.display else 'OFF'}")
        if self.record_path:
            print(f"📹 Recording to: {self.record_path}")
        print(f"Press 'q' or ESC to quit\n")

        # 启动显示线程
        display_thread = None
        if self.display:
            display_thread = threading.Thread(target=self._display_loop, daemon=True)
            display_thread.start()

        # 接收循环
        try:
            while self.running:
                try:
                    data, addr = sock.recvfrom(2048)
                    self._process_packet(data, addr)
                except socket.timeout:
                    continue
        except KeyboardInterrupt:
            print("\n⏹ Shutting down...")
        finally:
            self.running = False
            sock.close()
            if display_thread:
                display_thread.join(timeout=2.0)

        # 打印统计
        elapsed = time.time() - self.stats['last_frame_ts'] if self.stats['last_frame_ts'] > 0 else 1
        print(f"\n📊 Statistics:")
        print(f"  Packets received:  {self.stats['packets_rx']}")
        print(f"  Frames completed:  {self.stats['frames_completed']}")
        print(f"  Frames dropped:    {self.stats['frames_dropped']}")
        print(f"  Data received:     {self.stats['bytes_rx'] / 1024:.1f} KB")


def main():
    parser = argparse.ArgumentParser(
        description='Lingxi BL Ground Station - Video Stream Receiver')
    parser.add_argument('--port', type=int, default=5566,
                        help='UDP listen port (default: 5566)')
    parser.add_argument('--no-display', action='store_true',
                        help='Disable display window (headless mode)')
    parser.add_argument('--record', type=str, default=None,
                        help='Record to video file (e.g. output.avi)')
    args = parser.parse_args()

    station = GroundStation(
        port=args.port,
        display=not args.no_display,
        record=args.record
    )
    station.run()


if __name__ == '__main__':
    main()
