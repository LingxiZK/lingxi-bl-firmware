# 灵犀智空 BL 感知模组 — 测试框架 Makefile
# 在 x86_64 Linux 上编译运行单元测试

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O0 -g \
         -I../../shared/protocol \
         -I../../shared/utils \
         -Iunit \
         -Imocks \
         -DUNIT_TEST \
         -DLOG_COMPILE_LEVEL=LOG_LEVEL_VERBOSE

# Unity 框架（需克隆）
UNITY_DIR = unity
UNITY_SRC = $(UNITY_DIR)/src/unity.c

# 目标源码
TARGET_SRC = \
    ../../shared/protocol/lingxi_protocol.c \
    ../../shared/utils/ring_buffer.c \
    ../../shared/utils/crc16.c \
    ../../shared/utils/byteorder.c \
    ../../shared/utils/debug_log.c

# 测试代码
TEST_SRC = \
    unit/test_protocol.c \
    unit/test_ring_buffer.c \
    unit/test_crc16.c \
    unit/test_byteorder.c \
    unit/test_debug_log.c

# Mock
MOCK_SRC = mocks/mock_sdio.c

# 主入口
MAIN_SRC = test_runner.c

SRC = $(UNITY_SRC) $(MOCK_SRC) $(TEST_SRC) $(TARGET_SRC) $(MAIN_SRC)
OBJ = $(SRC:.c=.o)

TARGET = test_runner

.PHONY: all clean run unity

all: unity $(TARGET)

unity:
	@if [ ! -d "$(UNITY_DIR)/src" ]; then \
		echo "Cloning Unity framework..."; \
		git clone --depth 1 https://github.com/ThrowTheSwitch/Unity.git $(UNITY_DIR); \
	fi

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)
