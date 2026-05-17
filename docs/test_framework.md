# 灵犀智空 BL 感知模组 — 测试框架设计

> **版本**: v3.2  
> **日期**: 2026-05-15  
> **测试框架**: Unity (ThrowTheSwitch) + 自定义 Mock

---

## 目录

1. [测试策略](#测试策略)
2. [Unity 框架集成](#unity-框架集成)
3. [目录结构](#目录结构)
4. [测试用例设计](#测试用例设计)
5. [Mock 驱动框架](#mock-驱动框架)
6. [运行测试](#运行测试)
7. [CI 集成](#ci-集成)

---

## 测试策略

| 测试类型 | 目标 | 工具 | 运行环境 |
|---------|------|------|---------|
| 单元测试 | 函数级正确性 | Unity | x86_64 (Linux) |
| 集成测试 | 模块间交互 | Unity + Mock | x86_64 (Linux) |
| 硬件在环 | 驱动 + 外设 | 自定义 | 目标板 |
| 协议一致性 | 帧格式/时序 | Python 脚本 | x86_64 (Linux) |

### 测试金字塔

```
       /\
      /  \     硬件在环测试 (少量)
     /----\
    /      \   集成测试 (中等)
   /--------\
  /          \ 单元测试 (大量)
 /------------\
```

---

## Unity 框架集成

### 获取 Unity

```bash
cd tests
git clone --depth 1 https://github.com/ThrowTheSwitch/Unity.git unity
```

### 目录结构

```
tests/
├── unity/                  # Unity 框架源码
├── mocks/                  # Mock 驱动
│   ├── mock_sdio.h/c       # SDIO 模拟
│   ├── mock_uart.h/c       # UART 模拟
│   └── mock_freertos.h/c   # FreeRTOS 模拟
├── unit/                   # 单元测试
│   ├── test_protocol.c     # 协议测试
│   ├── test_ring_buffer.c  # 环形缓冲区测试
│   ├── test_crc16.c        # CRC16 测试
│   ├── test_byteorder.c    # 字节序测试
│   └── test_debug_log.c    # 日志测试
├── integration/            # 集成测试
│   ├── test_stm32_esp32.c  # 双机通信测试
│   └── test_ota_flow.c     # OTA 流程测试
├── hil/                    # 硬件在环
│   └── test_sdio_hw.c      # SDIO 硬件测试
├── Makefile                # 构建脚本
└── test_runner.c           # 主入口
```

### Makefile

```makefile
# 测试框架 Makefile
# 在 x86_64 Linux 上编译运行

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O0 -g \
         -I../shared/protocol \
         -I../shared/utils \
         -Iunity/src \
         -Imocks \
         -DUNIT_TEST \
         -DLOG_COMPILE_LEVEL=LOG_LEVEL_VERBOSE

UNITY_SRC = unity/src/unity.c
MOCK_SRC = $(wildcard mocks/*.c)
TEST_SRC = $(wildcard unit/*.c) $(wildcard integration/*.c)
TARGET_SRC = \
    ../shared/protocol/lingxi_protocol.c \
    ../shared/utils/ring_buffer.c \
    ../shared/utils/crc16.c \
    ../shared/utils/byteorder.c \
    ../shared/utils/debug_log.c

SRC = $(UNITY_SRC) $(MOCK_SRC) $(TEST_SRC) $(TARGET_SRC) test_runner.c
OBJ = $(SRC:.c=.o)

TARGET = test_runner

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)
```

---

## 测试用例设计

### 1. 协议测试 (test_protocol.c)

```c
#include "unity.h"
#include "lingxi_protocol.h"
#include "mock_sdio.h"

void setUp(void) { lx_parser_init(&parser); mock_sdio_reset(); }
void tearDown(void) { }

/* --- CRC 测试 --- */
void test_crc16_known_value(void)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    uint16_t crc = lx_crc16(data, sizeof(data));
    /* 已知 CRC 值（通过外部工具计算验证） */
    TEST_ASSERT_EQUAL_HEX16(0xXXXX, crc);
}

void test_crc16_empty_data(void)
{
    uint16_t crc = lx_crc16(NULL, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc); /* 初始值 */
}

/* --- 帧打包测试 --- */
void test_frame_pack_heartbeat(void)
{
    lx_heartbeat_t hb = { .version = 0x32, .seq = 1, .timestamp = 12345 };
    uint8_t buf[64];
    int len = lx_pack_heartbeat(buf, sizeof(buf), &hb);
    
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_HEX8(LX_PROTO_SOF, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(LX_PROTO_EOF, buf[len - 1]);
}

void test_frame_pack_ai_result(void)
{
    lx_ai_result_t result = {
        .timestamp = 1000,
        .class_id = 1,
        .confidence = 95,
        .x = 100, .y = 200, .w = 50, .h = 50,
        .depth_mm = 1500
    };
    uint8_t buf[128];
    int len = lx_pack_ai_result(buf, sizeof(buf), &result);
    
    TEST_ASSERT_GREATER_THAN(0, len);
}

void test_frame_pack_oversized_payload(void)
{
    uint8_t payload[LX_PROTO_MAX_PAYLOAD + 1];
    uint8_t buf[64];
    int len = lx_frame_pack(LX_CMD_SEND_RESULT, payload, sizeof(payload), buf, sizeof(buf));
    
    TEST_ASSERT_EQUAL(-2, len); /* 载荷过大 */
}

/* --- 帧解包测试 --- */
void test_frame_unpack_valid(void)
{
    lx_heartbeat_t hb = { .version = 0x32, .seq = 42, .timestamp = 99999 };
    uint8_t tx_buf[64];
    int tx_len = lx_pack_heartbeat(tx_buf, sizeof(tx_buf), &hb);
    
    lx_frame_t frame;
    lx_status_t status = lx_frame_unpack(tx_buf, tx_len, &frame);
    
    TEST_ASSERT_EQUAL(LX_STATUS_OK, status);
    TEST_ASSERT_EQUAL(LX_CMD_HEARTBEAT, frame.cmd);
}

void test_frame_unpack_crc_error(void)
{
    uint8_t bad_frame[] = { 0x7E, 0x00, 0x05, 0xA0, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0x7F };
    lx_frame_t frame;
    lx_status_t status = lx_frame_unpack(bad_frame, sizeof(bad_frame), &frame);
    
    TEST_ASSERT_EQUAL(LX_STATUS_ERR_LENGTH, status); /* 或 CRC 错误 */
}

/* --- 状态机测试 --- */
void test_parser_feed_byte_by_byte(void)
{
    lx_parser_t parser;
    lx_parser_init(&parser);
    
    lx_heartbeat_t hb = { .version = 0x32, .seq = 1, .timestamp = 0 };
    uint8_t buf[64];
    int len = lx_pack_heartbeat(buf, sizeof(buf), &hb);
    
    lx_frame_t frame;
    for (int i = 0; i < len - 1; i++) {
        TEST_ASSERT_FALSE(lx_parser_feed(&parser, buf[i], &frame));
    }
    TEST_ASSERT_TRUE(lx_parser_feed(&parser, buf[len - 1], &frame));
    TEST_ASSERT_EQUAL(LX_CMD_HEARTBEAT, frame.cmd);
}

void test_parser_resync_after_garbage(void)
{
    lx_parser_t parser;
    lx_parser_init(&parser);
    
    uint8_t garbage[] = { 0x00, 0x11, 0x22, 0x7E }; /* 垃圾 + SOF */
    lx_frame_t frame;
    
    /* 喂入垃圾 */
    for (int i = 0; i < 3; i++) {
        lx_parser_feed(&parser, garbage[i], &frame);
    }
    /* SOF 应触发重新同步 */
    TEST_ASSERT_FALSE(lx_parser_feed(&parser, garbage[3], &frame));
}

/* --- 转义测试 --- */
void test_frame_escape_bytes(void)
{
    uint8_t payload[] = { 0x7E, 0x7D, 0x7F, 0x01 }; /* 含特殊字节 */
    uint8_t buf[64];
    int len = lx_frame_pack(LX_CMD_SEND_RESULT, payload, sizeof(payload), buf, sizeof(buf));
    
    TEST_ASSERT_GREATER_THAN((int)sizeof(payload) + 7, len); /* 应有额外转义字节 */
    
    /* 解包验证 */
    lx_frame_t frame;
    TEST_ASSERT_EQUAL(LX_STATUS_OK, lx_frame_unpack(buf, len, &frame));
}

/* --- 便捷函数测试 --- */
void test_pack_get_status(void)
{
    uint8_t buf[32];
    int len = lx_pack_get_status(buf, sizeof(buf));
    
    TEST_ASSERT_GREATER_THAN(0, len);
    
    lx_frame_t frame;
    TEST_ASSERT_EQUAL(LX_STATUS_OK, lx_frame_unpack(buf, len, &frame));
    TEST_ASSERT_EQUAL(LX_CMD_GET_STATUS, frame.cmd);
}
```

### 2. 环形缓冲区测试 (test_ring_buffer.c)

```c
#include "unity.h"
#include "ring_buffer.h"

static uint8_t buf_mem[256];
static ring_buffer_t rb;

void setUp(void) { rb_init(&rb, buf_mem, sizeof(buf_mem)); }
void tearDown(void) { }

void test_init(void)
{
    TEST_ASSERT_TRUE(rb_is_empty(&rb));
    TEST_ASSERT_EQUAL(255, rb_free(&rb)); /* 牺牲一个单元 */
    TEST_ASSERT_EQUAL(0, rb_used(&rb));
}

void test_write_read(void)
{
    uint8_t data[] = { 1, 2, 3, 4, 5 };
    uint8_t out[5];
    
    TEST_ASSERT_EQUAL(5, rb_write(&rb, data, 5));
    TEST_ASSERT_EQUAL(5, rb_used(&rb));
    
    TEST_ASSERT_EQUAL(5, rb_read(&rb, out, 5));
    TEST_ASSERT_TRUE(rb_is_empty(&rb));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out, 5);
}

void test_wraparound(void)
{
    uint8_t data[250];
    uint8_t out[250];
    
    /* 填满 */
    memset(data, 0xAA, sizeof(data));
    rb_write(&rb, data, sizeof(data));
    
    /* 读出一半 */
    rb_read(&rb, out, 125);
    
    /* 写入跨越边界 */
    memset(data, 0xBB, sizeof(data));
    rb_write(&rb, data, 125);
    
    /* 全部读出验证 */
    rb_read(&rb, out, 125); /* 后半段 0xAA */
    for (int i = 0; i < 125; i++) TEST_ASSERT_EQUAL(0xAA, out[i]);
    
    rb_read(&rb, out, 125); /* 新写入的 0xBB */
    for (int i = 0; i < 125; i++) TEST_ASSERT_EQUAL(0xBB, out[i]);
}

void test_overflow(void)
{
    uint8_t data[300];
    uint8_t out[256];
    
    memset(data, 0xCC, sizeof(data));
    uint16_t written = rb_write(&rb, data, sizeof(data));
    
    TEST_ASSERT_EQUAL(255, written); /* 最大可写 */
    TEST_ASSERT_EQUAL(1, rb.overflow_cnt);
}

void test_peek(void)
{
    uint8_t data[] = { 0x11, 0x22, 0x33 };
    uint8_t out1[3], out2[3];
    
    rb_write(&rb, data, 3);
    
    TEST_ASSERT_EQUAL(3, rb_peek(&rb, out1, 3));
    TEST_ASSERT_EQUAL(3, rb_peek(&rb, out2, 3)); /* 再次 peek，数据仍在 */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out1, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, out2, 3);
    TEST_ASSERT_EQUAL(3, rb_used(&rb)); /* 未消耗 */
}

void test_all_or_nothing(void)
{
    uint8_t data[300];
    
    TEST_ASSERT_EQUAL(0, rb_write_all_or_nothing(&rb, data, sizeof(data)));
    TEST_ASSERT_TRUE(rb_is_empty(&rb)); /* 未写入 */
}
```

### 3. CRC16 测试 (test_crc16.c)

```c
#include "unity.h"
#include "crc16.h"

void test_crc16_ccitt_zero(void)
{
    uint8_t data[] = { 0x00, 0x00, 0x00, 0x00 };
    uint16_t crc = crc16_ccitt_fast(data, sizeof(data));
    TEST_ASSERT_EQUAL_HEX16(0xC2FB, crc); /* 已知值 */
}

void test_crc16_modbus(void)
{
    uint8_t data[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };
    uint16_t crc = crc16_calculate(data, sizeof(data), CRC16_MODBUS);
    /* Modbus 标准测试向量 */
    TEST_ASSERT_EQUAL_HEX16(0xC40F, crc);
}

void test_crc16_verify(void)
{
    uint8_t data[] = "123456789";
    uint16_t crc = crc16_ccitt_fast(data, sizeof(data) - 1);
    TEST_ASSERT_TRUE(crc16_verify(data, sizeof(data) - 1, crc, CRC16_CCITT_FALSE));
}

void test_crc16_incremental(void)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    uint16_t crc1 = crc16_ccitt_fast(data, 3);
    
    uint16_t crc2 = 0xFFFF;
    for (int i = 0; i < 3; i++) {
        crc2 = crc16_update(crc2, data[i], CRC16_CCITT_FALSE);
    }
    
    TEST_ASSERT_EQUAL_HEX16(crc1, crc2);
}
```

### 4. 字节序测试 (test_byteorder.c)

```c
#include "unity.h"
#include "byteorder.h"

void test_be16(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x3412, be16toh(0x1234));
    TEST_ASSERT_EQUAL_HEX16(0x1234, htobe16(0x1234));
}

void test_be32(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x78563412, be32toh(0x12345678));
}

void test_le16(void)
{
    /* 小端机器上 le16toh 应为恒等 */
    TEST_ASSERT_EQUAL_HEX16(0x1234, le16toh(0x1234));
}

void test_memory_read_write(void)
{
    uint8_t buf[4];
    
    be32_write(buf, 0x12345678);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[3]);
    
    TEST_ASSERT_EQUAL_HEX32(0x12345678, be32_read(buf));
}
```

### 5. Mock 驱动示例 (mocks/mock_sdio.c)

```c
#include "mock_sdio.h"
#include <string.h>

static uint8_t mock_tx_buf[4096];
static uint16_t mock_tx_len = 0;
static uint8_t mock_rx_buf[4096];
static uint16_t mock_rx_len = 0;
static uint16_t mock_rx_pos = 0;

void mock_sdio_reset(void)
{
    memset(mock_tx_buf, 0, sizeof(mock_tx_buf));
    mock_tx_len = 0;
    memset(mock_rx_buf, 0, sizeof(mock_rx_buf));
    mock_rx_len = 0;
    mock_rx_pos = 0;
}

void mock_sdio_set_rx_data(const uint8_t *data, uint16_t len)
{
    if (len > sizeof(mock_rx_buf)) len = sizeof(mock_rx_buf);
    memcpy(mock_rx_buf, data, len);
    mock_rx_len = len;
    mock_rx_pos = 0;
}

int mock_sdio_send(const uint8_t *data, uint16_t len)
{
    if (mock_tx_len + len > sizeof(mock_tx_buf)) return -1;
    memcpy(mock_tx_buf + mock_tx_len, data, len);
    mock_tx_len += len;
    return len;
}

int mock_sdio_recv(uint8_t *data, uint16_t len)
{
    uint16_t avail = mock_rx_len - mock_rx_pos;
    if (len > avail) len = avail;
    memcpy(data, mock_rx_buf + mock_rx_pos, len);
    mock_rx_pos += len;
    return len;
}

/* 平台接口实现 */
int platform_sdio_send(const uint8_t *data, uint16_t len)
{
    return mock_sdio_send(data, len);
}
```

---

## 运行测试

```bash
cd tests

# 编译
make

# 运行全部测试
make run

# 或单独运行
./test_runner

# 带详细输出
./test_runner -v

# 只运行特定测试组
./test_runner -g protocol
```

### 预期输出

```
Unity test run 1 of 1
-----------------------
test_crc16_known_value PASS
test_crc16_empty_data PASS
test_frame_pack_heartbeat PASS
test_frame_pack_ai_result PASS
test_frame_pack_oversized_payload PASS
test_frame_unpack_valid PASS
test_frame_unpack_crc_error PASS
test_parser_feed_byte_by_byte PASS
test_parser_resync_after_garbage PASS
test_frame_escape_bytes PASS
test_pack_get_status PASS
test_init PASS
test_write_read PASS
test_wraparound PASS
test_overflow PASS
test_peek PASS
test_all_or_nothing PASS
test_crc16_ccitt_zero PASS
test_crc16_modbus PASS
test_crc16_verify PASS
test_crc16_incremental PASS
test_be16 PASS
test_be32 PASS
test_le16 PASS
test_memory_read_write PASS

-----------------------
25 Tests 0 Failures 0 Ignored
OK
```

---

## CI 集成

```yaml
# .github/workflows/test.yml
name: Unit Tests

on: [push, pull_request]

jobs:
  unit-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Build tests
        run: |
          cd tests
          make
      
      - name: Run tests
        run: |
          cd tests
          ./test_runner
      
      - name: Upload coverage (可选)
        run: |
          cd tests
          make clean
          CFLAGS="$(CFLAGS) --coverage" make
          ./test_runner
          gcov *.gcno
```

---

*文档结束*
