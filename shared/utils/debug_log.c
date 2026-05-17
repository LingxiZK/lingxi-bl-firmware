/**
 * @file debug_log.c
 * @brief 调试日志系统实现
 * @version 3.2
 * @date 2026-05-15
 */

#include "debug_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* 内部缓冲区大小 */
#define LOG_BUF_SIZE 256

/* 运行时日志级别 */
static log_level_t s_log_level = LOG_LEVEL_DEBUG;

/* 日志级别字符串 */
static const char *s_level_str[] = {
    "NONE", "ERR", "WRN", "INF", "DBG", "VRB"
};

void log_set_level(log_level_t level)
{
    if (level < LOG_LEVEL_MAX) {
        s_log_level = level;
    }
}

log_level_t log_get_level(void)
{
    return s_log_level;
}

void log_write(log_level_t level, const char *module,
               const char *file, int line, const char *fmt, ...)
{
    if (level > s_log_level || level > LOG_COMPILE_LEVEL) {
        return;
    }

    char buf[LOG_BUF_SIZE];
    int pos = 0;

    /* 时间戳 */
#if LOG_ENABLE_TIMESTAMP
    uint32_t ts = log_platform_timestamp();
    pos += snprintf(buf + pos, LOG_BUF_SIZE - pos, "[%lu]", (unsigned long)ts);
#endif

    /* 级别 */
    if (level < LOG_LEVEL_MAX) {
        pos += snprintf(buf + pos, LOG_BUF_SIZE - pos, "[%s]", s_level_str[level]);
    }

    /* 模块名 */
#if LOG_ENABLE_MODULE
    if (module != NULL) {
        pos += snprintf(buf + pos, LOG_BUF_SIZE - pos, "[%s]", module);
    }
#endif

    /* 文件行号 */
#if LOG_ENABLE_FILE_LINE
    const char *filename = file;
    /* 提取文件名（不含路径） */
    const char *p = strrchr(file, '/');
    if (p != NULL) {
        filename = p + 1;
    } else {
        p = strrchr(file, '\\');
        if (p != NULL) {
            filename = p + 1;
        }
    }
    pos += snprintf(buf + pos, LOG_BUF_SIZE - pos, " %s:%d ", filename, line);
#endif

    /* 格式化消息 */
    va_list args;
    va_start(args, fmt);
    pos += vsnprintf(buf + pos, LOG_BUF_SIZE - pos, fmt, args);
    va_end(args);

    /* 确保结尾有换行 */
    if (pos < LOG_BUF_SIZE - 1) {
        buf[pos++] = '\n';
        buf[pos] = '\0';
    } else {
        buf[LOG_BUF_SIZE - 2] = '\n';
        buf[LOG_BUF_SIZE - 1] = '\0';
    }

    /* 输出 */
    log_platform_output(buf);
}

void log_hexdump(log_level_t level, const uint8_t *data, uint16_t len, const char *label)
{
    if (level > s_log_level || data == NULL || len == 0) {
        return;
    }

    if (label != NULL) {
        log_write(level, LOG_MODULE_NAME, __FILE__, __LINE__,
                  "HEXDUMP: %s (%u bytes)", label, (unsigned)len);
    } else {
        log_write(level, LOG_MODULE_NAME, __FILE__, __LINE__,
                  "HEXDUMP: (%u bytes)", (unsigned)len);
    }

    char line[80];
    for (uint16_t i = 0; i < len; i += 16) {
        int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos, "%04X: ", i);

        /* 十六进制 */
        for (uint16_t j = 0; j < 16; j++) {
            if (i + j < len) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", data[i + j]);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
            }
        }

        pos += snprintf(line + pos, sizeof(line) - pos, " |");

        /* ASCII */
        for (uint16_t j = 0; j < 16; j++) {
            if (i + j < len) {
                uint8_t c = data[i + j];
                if (c >= 32 && c < 127) {
                    pos += snprintf(line + pos, sizeof(line) - pos, "%c", c);
                } else {
                    pos += snprintf(line + pos, sizeof(line) - pos, ".");
                }
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, " ");
            }
        }

        pos += snprintf(line + pos, sizeof(line) - pos, "|");
        log_platform_output(line);
        log_platform_output("\n");
    }
}
