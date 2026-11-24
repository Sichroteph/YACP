// ESP-IDF deliberately provides a weak descriptor for application overrides.
// Keep the board-specific fields identical to esp_app_format/esp_app_desc.c;
// only the application identity comes from this firmware build instead of the
// cached Arduino core's upstream Git tag.
#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>

#ifdef YACP_FIRMWARE
#define YACP_STRINGIFY_INNER(value) #value
#define YACP_STRINGIFY(value) YACP_STRINGIFY_INNER(value)

const __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#endif
    .version = CROSSPOINT_VERSION,
    .project_name = "YACP",
#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#endif
    .idf_ver = YACP_STRINGIFY(ESP_IDF_VERSION_MAJOR) "." YACP_STRINGIFY(ESP_IDF_VERSION_MINOR) "."
               YACP_STRINGIFY(ESP_IDF_VERSION_PATCH),
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};

_Static_assert(sizeof(CROSSPOINT_VERSION) <= sizeof(esp_app_desc.version), "YACP version is too long");
#endif
