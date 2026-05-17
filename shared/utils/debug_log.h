/*******************************************************************************
 * @file    debug_log.h
 * @brief   统一调试日志
 ******************************************************************************/
#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOG_E(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_ERROR) dbg_printf("[E] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_W(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_WARN)  dbg_printf("[W] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_I(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_INFO)  dbg_printf("[I] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_D(fmt, ...) do { if (LOG_LEVEL >= LOG_LEVEL_DEBUG) dbg_printf("[D] " fmt "\r\n", ##__VA_ARGS__); } while(0)

void dbg_printf(const char *fmt, ...);
void dbg_init(void);

#endif
