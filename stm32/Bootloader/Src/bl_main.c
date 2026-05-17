/*******************************************************************************
 * @file    bl_main.c
 * @brief   Lingxi N6 AI Deck — Bootloader 主程序
 * @version 1.1.0
 * @date    2026-05-16
 *
 * 修正说明 (v1.1.0):
 *   STM32N657 没有用户 Internal Flash。上电后 Boot ROM 先执行，
 *   根据 BOOT0/BOOT1 引脚状态选择启动源。
 *   正常运行时 BOOT0=0, BOOT1=0 (Flash boot)，
 *   Boot ROM 从 XIP Flash(0x70000000)加载 Bootloader 到 RAM 执行。
 *
 * 启动流程:
 *   1. Boot ROM 从 XIP Flash 加载 Bootloader 到 RAM 执行
 *   2. 初始化 RCC / GPIO / UART / SysTick
 *   3. 初始化 OctoSPI XIP 控制器
 *   4. 通过 XSPI 读取 Info Sector(W25Q512)，确定激活分区
 *   5. 3s 超时监听 UART 升级命令
 *   6. 跳转到 XIP App (A 或 B)
 ******************************************************************************/

#include "stm32n6xx_hal.h"
#include "bl_config.h"
#include "bl_protocol.h"
#include "bl_flash.h"
#include <string.h>

/* 全局变量 */
static UART_HandleTypeDef huart1;
static bl_info_sector_t g_info;
static volatile uint32_t g_tick_ms = 0;
static volatile bool g_enter_boot = false;
static volatile bool g_uart_rx_flag = false;
static uint8_t g_rx_byte;

/* 前向声明 */
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_UART1_Init(void);
static void MX_IWDG_Init(void);
static void print_banner(void);
static void enter_upgrade_mode(void);
static void attempt_boot_app(void);
static void WDG_Refresh(void);

/*============================================================================
 * 中断处理
 *===========================================================================*/
void SysTick_Handler(void)
{
    HAL_IncTick();
    g_tick_ms++;
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        bl_protocol_process_byte(g_rx_byte);
        g_uart_rx_flag = true;
        HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);
    }
}

/*============================================================================
 * 启动入口
 *===========================================================================*/
int main(void)
{
    /* 禁用中断 */
    __disable_irq();

    /* HAL 初始化 */
    HAL_Init();
    SystemClock_Config();

    /* 初始化外设 */
    MX_GPIO_Init();
    MX_UART1_Init();
    MX_IWDG_Init();

    /* 启用 SysTick */
    HAL_SYSTICK_Config(SystemCoreClock / 1000);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

    /* 启用中断 */
    __enable_irq();

    /* 打印 Banner */
    print_banner();

    /* 初始化 Flash 层 (XSPI) */
    bl_flash_init();
    bl_xspi_init();

    /* 读取 Bootloader Info Sector (从 W25Q512 XIP Flash) */
    bl_err_t err = bl_info_read(&g_info);
    if (err != BL_OK || !bl_info_validate(&g_info)) {
        ERR_PRINT("Info invalid, init default");
        bl_info_init_default();
        bl_info_read(&g_info);
    }

    /* 初始化 UART OTA 协议 */
    bl_protocol_init();
    HAL_UART_Receive_IT(&huart1, &g_rx_byte, 1);

    /* --- 超时监听阶段 --- */
    uint32_t start_tick = g_tick_ms;
    uint32_t last_wdg = g_tick_ms;
    bool timeout_reached = false;

    while (!timeout_reached) {
        WDG_Refresh();

        /* 检查 3s 超时 */
        if ((g_tick_ms - start_tick) >= BL_BOOT_TIMEOUT_MS) {
            timeout_reached = true;
        }

        /* 如果收到 ENTER_BOOT 命令 */
        if (g_enter_boot) {
            enter_upgrade_mode();
            /* 不会返回 */
        }

        /* 每 500ms 打印一个点 */
        if ((g_tick_ms - start_tick) % 500 == 0) {
            static uint32_t last_dot = 0;
            if (g_tick_ms != last_dot) {
                uart1_send_string(".");
                last_dot = g_tick_ms;
            }
        }
    }

    /* 超时，尝试启动 App */
    uart1_send_string("\r\n[BL] Timeout, booting app...\r\n");
    attempt_boot_app();

    /* 不应该执行到这里 */
    while (1) {
        WDG_Refresh();
    }
}

/*============================================================================
 * 升级模式
 *===========================================================================*/
static void enter_upgrade_mode(void)
{
    uart1_send_string("\r\n[BL] Enter UPGRADE mode\r\n");
    g_enter_boot = false;

    /* 注册 OTA 命令处理器 */
    extern void bl_ota_register_handlers(void);
    bl_ota_register_handlers();

    /* 循环监听 */
    while (1) {
        WDG_Refresh();
        /* bl_protocol 在中断回调中处理字节，
         * 在这里检查是否收到 REBOOT 命令 */
        HAL_Delay(10);
    }
}

/*============================================================================
 * 跳转到 App
 *===========================================================================*/
static void attempt_boot_app(void)
{
    uint32_t target_addr = 0;
    const char *part_name = "";

    if (g_info.active_partition == 0) {
        target_addr = BL_APP_A_ADDR;
        part_name = "AppA";
    } else {
        target_addr = BL_APP_B_ADDR;
        part_name = "AppB";
    }

    /* 校验固件 */
    bl_err_t err = BL_OK;
    if (g_info.active_partition == 0) {
        err = bl_verify_firmware(BL_APP_A_ADDR, g_info.app_a.size, g_info.app_a.crc32);
    } else {
        err = bl_verify_firmware(BL_APP_B_ADDR, g_info.app_b.size, g_info.app_b.crc32);
    }

    if (err != BL_OK) {
        ERR_PRINT("Firmware verify failed, try fallback");
        /* 尝试另一分区 */
        uint8_t fallback = (g_info.active_partition == 0) ? 1 : 0;
        if (fallback == 0) {
            err = bl_verify_firmware(BL_APP_A_ADDR, g_info.app_a.size, g_info.app_a.crc32);
            target_addr = BL_APP_A_ADDR;
            part_name = "AppA(fallback)";
        } else {
            err = bl_verify_firmware(BL_APP_B_ADDR, g_info.app_b.size, g_info.app_b.crc32);
            target_addr = BL_APP_B_ADDR;
            part_name = "AppB(fallback)";
        }
        if (err != BL_OK) {
            ERR_PRINT("No valid app found, stay in bootloader");
            enter_upgrade_mode();
            return;
        }
    }

    /* 打印信息 */
    char buf[64];
    snprintf(buf, sizeof(buf), "[BL] Jump to %s @ 0x%08X\r\n", part_name, (unsigned)target_addr);
    uart1_send_string(buf);

    /* 清理 */
    HAL_UART_DeInit(&huart1);
    HAL_DeInit();

    /* 跳转 */
    bl_jump_to_app(target_addr);
}

/*============================================================================
 * 系统时钟配置
 *===========================================================================*/
static void SystemClock_Config(void)
{
    /* STM32N6 默认配置，实际项目建议用 CubeMX 生成 */
    /* 此处仅保留框架，具体数值需根据板载调整 */
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_EnableVddIO2();

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_HCLK |
                                    RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                                    RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 |
                                    RCC_CLOCKTYPE_PCLK5);
    RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}

/*============================================================================
 * GPIO 初始化
 *===========================================================================*/
static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* PA5 LED */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/*============================================================================
 * UART1 初始化 (PA9/PA10, 飞控通信)
 *===========================================================================*/
static void MX_UART1_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = BL_UART_BAUDRATE;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/*============================================================================
 * IWDG 初始化
 *===========================================================================*/
static void MX_IWDG_Init(void)
{
    /* 看门狗，10s 超时 */
    IWDG_HandleTypeDef hiwdg;
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;   /* 32kHz / 64 = 500Hz */
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    hiwdg.Init.Reload = 5000;                   /* 5000 / 500 = 10s */
    HAL_IWDG_Init(&hiwdg);
}

static void WDG_Refresh(void)
{
    static IWDG_HandleTypeDef hiwdg;
    hiwdg.Instance = IWDG;
    HAL_IWDG_Refresh(&hiwdg);
}

/*============================================================================
 * Banner 输出
 *===========================================================================*/
static void print_banner(void)
{
    char buf[128];
    uart1_send_string("\r\n");
    uart1_send_string("================================\r\n");
    uart1_send_string("  Lingxi N6 AI Deck Bootloader\r\n");
    uart1_send_string("  Version: ");
    snprintf(buf, sizeof(buf), "%d.%d.%d\r\n",
             BL_VERSION_MAJOR, BL_VERSION_MINOR, BL_VERSION_PATCH);
    uart1_send_string(buf);
    uart1_send_string("================================\r\n");
}

/*============================================================================
 * 错误处理
 *===========================================================================*/
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(100);
    }
}

/*============================================================================
 * 调试输出实现 (weak, 可在其他文件覆盖)
 *===========================================================================*/
__attribute__((weak))
int uart1_send_string(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)str, strlen(str), 100);
    return 0;
}

/*============================================================================
 * 全局跳转标志 (OTA 处理器设置)
 *===========================================================================*/
void bl_set_enter_boot(bool val)
{
    g_enter_boot = val;
}

bool bl_get_enter_boot(void)
{
    return g_enter_boot;
}

UART_HandleTypeDef* bl_get_uart_handle(void)
{
    return &huart1;
}

uint32_t bl_get_tick_ms(void)
{
    return g_tick_ms;
}
