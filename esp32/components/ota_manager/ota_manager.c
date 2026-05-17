#include "ota_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include <string.h>

#define TAG "OTA_MGR"

/*=============================================================================
 * 内部状态
 *===========================================================================*/

static ota_state_t          s_state = OTA_STATE_IDLE;
static ota_config_t         s_config = {0};
static uint32_t             s_received_size = 0;
static uint8_t              s_progress = 0;
static ota_progress_cb_t    s_progress_cb = NULL;
static ota_complete_cb_t    s_complete_cb = NULL;
static void                *s_cb_arg = NULL;
static SemaphoreHandle_t      s_mutex = NULL;
static bool                   s_initialized = false;

/* OTA 句柄 */
static esp_ota_handle_t     s_ota_handle = 0;
static const esp_partition_t *s_ota_partition = NULL;

/* 校验 */
static uint32_t             s_crc32 = 0xFFFFFFFFU;
static mbedtls_sha256_context s_sha_ctx;
static uint8_t              s_sha256[32];

/*=============================================================================
 * CRC32 计算
 *===========================================================================*/

static const uint32_t s_crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc = s_crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

/*=============================================================================
 * 内部函数
 *===========================================================================*/

static void ota_set_state(ota_state_t state)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = state;
    xSemaphoreGive(s_mutex);

    if (s_progress_cb) {
        s_progress_cb(s_progress, state, s_cb_arg);
    }
}

static void ota_update_progress(void)
{
    if (s_config.firmware_size > 0) {
        s_progress = (uint8_t)((s_received_size * 100) / s_config.firmware_size);
    }

    if (s_progress_cb) {
        s_progress_cb(s_progress, s_state, s_cb_arg);
    }
}

/*=============================================================================
 * API 实现
 *===========================================================================*/

esp_err_t ota_manager_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 检查当前分区 */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        ESP_LOGI(TAG, "Running partition: %s", running->label);
    }

    /* 检查 OTA 分区 */
    s_ota_partition = esp_ota_get_next_update_partition(NULL);
    if (s_ota_partition) {
        ESP_LOGI(TAG, "OTA partition: %s, size=%d", s_ota_partition->label, s_ota_partition->size);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "OTA manager initialized");
    return ESP_OK;
}

esp_err_t ota_manager_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_manager_cancel();

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    s_initialized = false;
    return ESP_OK;
}

esp_err_t ota_manager_register_callbacks(ota_progress_cb_t progress_cb,
                                          ota_complete_cb_t complete_cb, void *arg)
{
    s_progress_cb = progress_cb;
    s_complete_cb = complete_cb;
    s_cb_arg = arg;
    return ESP_OK;
}

esp_err_t ota_manager_prepare(const ota_config_t *config)
{
    if (!s_initialized || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state != OTA_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 检查固件大小 */
    if (config->firmware_size > OTA_MAX_FIRMWARE_SIZE) {
        ESP_LOGE(TAG, "Firmware too large: %d", config->firmware_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 获取 OTA 分区 */
    s_ota_partition = esp_ota_get_next_update_partition(NULL);
    if (s_ota_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_ERR_NOT_FOUND;
    }

    if (s_ota_partition->size < config->firmware_size) {
        ESP_LOGE(TAG, "OTA partition too small: %d < %d",
                 s_ota_partition->size, config->firmware_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 开始 OTA */
    esp_err_t ret = esp_ota_begin(s_ota_partition, config->firmware_size, &s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %d", ret);
        return ret;
    }

    /* 初始化校验 */
    s_crc32 = 0xFFFFFFFFU;
    mbedtls_sha256_init(&s_sha_ctx);
    mbedtls_sha256_starts(&s_sha_ctx, 0);

    /* 保存配置 */
    memcpy(&s_config, config, sizeof(ota_config_t));
    s_received_size = 0;
    s_progress = 0;

    ota_set_state(OTA_STATE_PREPARE);
    ESP_LOGI(TAG, "OTA prepared, size=%d, partition=%s", config->firmware_size, s_ota_partition->label);
    return ESP_OK;
}

esp_err_t ota_manager_receive_chunk(const lx_payload_ota_chunk_t *chunk)
{
    if (!s_initialized || chunk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state != OTA_STATE_PREPARE && s_state != OTA_STATE_RECEIVING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (chunk->chunk_size == 0 || chunk->chunk_size > OTA_CHUNK_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* 检查偏移 */
    if (chunk->offset != s_received_size) {
        ESP_LOGW(TAG, "Chunk offset mismatch: expected=%d, got=%d", s_received_size, chunk->offset);
        return ESP_ERR_INVALID_ARG;
    }

    /* 写入 OTA 分区 */
    esp_err_t ret = esp_ota_write(s_ota_handle, chunk->data, chunk->chunk_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA write failed: %d", ret);
        ota_set_state(OTA_STATE_ERROR);
        return ret;
    }

    /* 更新校验 */
    s_crc32 = crc32_update(s_crc32, chunk->data, chunk->chunk_size);
    mbedtls_sha256_update(&s_sha_ctx, chunk->data, chunk->chunk_size);

    s_received_size += chunk->chunk_size;
    ota_set_state(OTA_STATE_RECEIVING);
    ota_update_progress();

    ESP_LOGD(TAG, "Received chunk: offset=%d, size=%d, progress=%d%%",
             chunk->offset, chunk->chunk_size, s_progress);
    return ESP_OK;
}

esp_err_t ota_manager_verify(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state != OTA_STATE_RECEIVING) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 检查接收大小 */
    if (s_received_size != s_config.firmware_size) {
        ESP_LOGE(TAG, "Size mismatch: received=%d, expected=%d", s_received_size, s_config.firmware_size);
        ota_set_state(OTA_STATE_VERIFY_FAIL);
        return ESP_ERR_INVALID_SIZE;
    }

    ota_set_state(OTA_STATE_VERIFYING);

    /* 完成 SHA256 */
    mbedtls_sha256_finish(&s_sha_ctx, s_sha256);
    mbedtls_sha256_free(&s_sha_ctx);

    /* 完成 CRC32 */
    s_crc32 ^= 0xFFFFFFFFU;

    /* 校验 CRC32 */
    if (s_crc32 != s_config.crc32) {
        ESP_LOGE(TAG, "CRC32 mismatch: calc=0x%08X, expected=0x%08X", s_crc32, s_config.crc32);
        ota_set_state(OTA_STATE_VERIFY_FAIL);
        return ESP_ERR_INVALID_CRC;
    }

    /* 结束 OTA 写入 */
    esp_err_t ret = esp_ota_end(s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %d", ret);
        ota_set_state(OTA_STATE_VERIFY_FAIL);
        return ret;
    }

    ota_set_state(OTA_STATE_VERIFY_OK);
    ESP_LOGI(TAG, "OTA verify OK, CRC32=0x%08X", s_crc32);
    return ESP_OK;
}

esp_err_t ota_manager_commit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state != OTA_STATE_VERIFY_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_set_state(OTA_STATE_COMMITTING);

    /* 设置启动分区 */
    esp_err_t ret = esp_ota_set_boot_partition(s_ota_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %d", ret);
        ota_set_state(OTA_STATE_ERROR);
        return ret;
    }

    ota_set_state(OTA_STATE_COMPLETE);
    ESP_LOGI(TAG, "OTA committed, new partition: %s", s_ota_partition->label);

    if (s_complete_cb) {
        s_complete_cb(true, (const char *)s_config.version, s_cb_arg);
    }

    return ESP_OK;
}

esp_err_t ota_manager_rollback(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_set_state(OTA_STATE_ROLLBACK);

    /* 回滚到上一个版本 */
    esp_err_t ret = esp_ota_roll_back_partition();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "OTA rollback done");
    return ESP_OK;
}

esp_err_t ota_manager_cancel(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    s_ota_partition = NULL;
    memset(&s_config, 0, sizeof(s_config));
    s_received_size = 0;
    s_progress = 0;

    ota_set_state(OTA_STATE_IDLE);
    ESP_LOGI(TAG, "OTA cancelled");
    return ESP_OK;
}

ota_state_t ota_manager_get_state(void)
{
    ota_state_t state;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state = s_state;
    xSemaphoreGive(s_mutex);
    return state;
}

uint8_t ota_manager_get_progress(void)
{
    return s_progress;
}

esp_err_t ota_manager_get_running_partition(char *label, size_t len)
{
    if (label == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(label, running->label, len - 1);
    label[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t ota_manager_get_info(ota_config_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(info, &s_config, sizeof(ota_config_t));
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t ota_manager_start_wifi_download(const char *url)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "WiFi OTA download started: %s", url);

    /* TODO: 实现 HTTP 下载 */
    /* 这里可以集成 esp_https_ota 或自定义下载逻辑 */

    return ESP_OK;
}

esp_err_t ota_manager_process_ble_data(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* BLE OTA 数据包格式：
     * [0-3]: 偏移量 (uint32_t)
     * [4-5]: 数据长度 (uint16_t)
     * [6-7]: 块序号 (uint16_t)
     * [8...]: 数据
     */
    if (len < 8) {
        return ESP_ERR_INVALID_SIZE;
    }

    lx_payload_ota_chunk_t chunk = {0};
    chunk.offset = *(uint32_t *)data;
    chunk.chunk_size = *(uint16_t *)(data + 4);
    chunk.chunk_seq = *(uint16_t *)(data + 6);

    if (chunk.chunk_size > len - 8) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(chunk.data, data + 8, chunk.chunk_size);

    return ota_manager_receive_chunk(&chunk);
}

esp_err_t ota_manager_notify_stm32(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    lx_payload_ota_status_t status = {0};
    status.status = (uint8_t)s_state;
    status.progress = s_progress;
    status.total_size = s_config.firmware_size;
    status.received_size = s_received_size;
    status.crc32 = s_crc32;

    if (s_state == OTA_STATE_VERIFY_FAIL || s_state == OTA_STATE_ERROR) {
        status.error_code = 0x0001; /* 通用错误 */
    }

    /* 通过协议处理器发送 */
    extern esp_err_t proto_handler_send_event(lx_frame_type_t type, const uint8_t *data, uint16_t len);
    return proto_handler_send_event(LX_DATA_OTA_STATUS, (const uint8_t *)&status, sizeof(status));
}
