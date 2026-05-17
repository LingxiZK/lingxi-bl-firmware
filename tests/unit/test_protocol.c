/**
 * @file test_protocol.c
 * @brief 通信协议单元测试
 * @version 3.2
 * @date 2026-05-15
 */

#include "unity.h"
#include "lingxi_protocol.h"

static lx_parser_t parser;

void setUp(void)
{
    lx_parser_init(&parser);
}

void tearDown(void)
{
}

/* --- CRC 测试 --- */
void test_crc16_known_value(void)
{
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t crc = lx_crc16(data, sizeof(data));
    uint16_t expected = 0xB6F9;
    TEST_ASSERT_EQUAL_HEX16(expected, crc);
}

void test_crc16_empty_data(void)
{
    uint16_t crc = lx_crc16(NULL, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc);
}

/* --- 帧打包测试 --- */
void test_frame_pack_heartbeat(void)
{
    lx_heartbeat_t hb = {.version = 0x32, .seq = 1, .timestamp = 12345};
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
        .depth_mm = 1500};
    uint8_t buf[128];
    int len = lx_pack_ai_result(buf, sizeof(buf), &result);

    TEST_ASSERT_GREATER_THAN(0, len);
}

void test_frame_pack_oversized_payload(void)
{
    uint8_t payload[LX_PROTO_MAX_PAYLOAD + 1];
    uint8_t buf[64];
    int len = lx_frame_pack(LX_CMD_SEND_RESULT, payload, sizeof(payload), buf, sizeof(buf));

    TEST_ASSERT_EQUAL(-2, len);
}

/* --- 帧解包测试 --- */
void test_frame_unpack_valid(void)
{
    lx_heartbeat_t hb = {.version = 0x32, .seq = 42, .timestamp = 99999};
    uint8_t tx_buf[64];
    int tx_len = lx_pack_heartbeat(tx_buf, sizeof(tx_buf), &hb);

    lx_frame_t frame;
    lx_status_t status = lx_frame_unpack(tx_buf, tx_len, &frame);

    TEST_ASSERT_EQUAL(LX_STATUS_OK, status);
    TEST_ASSERT_EQUAL(LX_CMD_HEARTBEAT, frame.cmd);
}

void test_frame_unpack_crc_error(void)
{
    uint8_t bad_frame[] = {0x7E, 0x00, 0x05, 0xA0, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0x7F};
    lx_frame_t frame;
    lx_status_t status = lx_frame_unpack(bad_frame, sizeof(bad_frame), &frame);

    TEST_ASSERT_EQUAL(LX_STATUS_ERR_LENGTH, status);
}

/* --- 状态机测试 --- */
void test_parser_feed_byte_by_byte(void)
{
    lx_heartbeat_t hb = {.version = 0x32, .seq = 1, .timestamp = 0};
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
    uint8_t garbage[] = {0x00, 0x11, 0x22, 0x7E};
    lx_frame_t frame;

    for (int i = 0; i < 3; i++) {
        lx_parser_feed(&parser, garbage[i], &frame);
    }
    TEST_ASSERT_FALSE(lx_parser_feed(&parser, garbage[3], &frame));
}

/* --- 转义测试 --- */
void test_frame_escape_bytes(void)
{
    uint8_t payload[] = {0x7E, 0x7D, 0x7F, 0x01};
    uint8_t buf[64];
    int len = lx_frame_pack(LX_CMD_SEND_RESULT, payload, sizeof(payload), buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN((int)sizeof(payload) + 7, len);

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

/* --- 命令名称测试 --- */
void test_cmd_name(void)
{
    TEST_ASSERT_EQUAL_STRING("HEARTBEAT", lx_cmd_name(LX_CMD_HEARTBEAT));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", lx_cmd_name(0xFF));
}

void test_status_name(void)
{
    TEST_ASSERT_EQUAL_STRING("OK", lx_status_name(LX_STATUS_OK));
    TEST_ASSERT_EQUAL_STRING("ERR_CRC", lx_status_name(LX_STATUS_ERR_CRC));
}

void test_protocol_group(void)
{
    RUN_TEST(test_crc16_known_value);
    RUN_TEST(test_crc16_empty_data);
    RUN_TEST(test_frame_pack_heartbeat);
    RUN_TEST(test_frame_pack_ai_result);
    RUN_TEST(test_frame_pack_oversized_payload);
    RUN_TEST(test_frame_unpack_valid);
    RUN_TEST(test_frame_unpack_crc_error);
    RUN_TEST(test_parser_feed_byte_by_byte);
    RUN_TEST(test_parser_resync_after_garbage);
    RUN_TEST(test_frame_escape_bytes);
    RUN_TEST(test_pack_get_status);
    RUN_TEST(test_cmd_name);
    RUN_TEST(test_status_name);
}
