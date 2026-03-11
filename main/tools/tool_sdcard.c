#include "tools/tool_sdcard.h"
#include "hardware/echoear_config.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "tool_sdcard";

#define TOOL_SDCARD_BASE "/sdcard"
#define TOOL_SDCARD_MAX_READ (32 * 1024)

static sdmmc_card_t *s_sdcard_card = NULL;

static bool sdcard_mountpoint_ready(void)
{
    struct stat st = { 0 };
    return (stat(TOOL_SDCARD_BASE, &st) == 0) && S_ISDIR(st.st_mode);
}

bool tool_sdcard_is_mounted(void)
{
    return sdcard_mountpoint_ready();
}

esp_err_t tool_sdcard_get_status(char *output, size_t output_size)
{
    struct statvfs vfs = { 0 };

    if ((output == NULL) || (output_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sdcard_mountpoint_ready()) {
        snprintf(output, output_size, "mounted: no\npath: %s\n", TOOL_SDCARD_BASE);
        return ESP_ERR_NOT_FOUND;
    }

    if (statvfs(TOOL_SDCARD_BASE, &vfs) != 0) {
        snprintf(
            output, output_size,
            "mounted: yes\npath: %s\nfs_stats: unavailable\ncard_handle: %s\n",
            TOOL_SDCARD_BASE, (s_sdcard_card != NULL) ? "yes" : "no"
        );
        return ESP_FAIL;
    }

    {
        uint64_t block_size = (uint64_t)((vfs.f_frsize != 0) ? vfs.f_frsize : vfs.f_bsize);
        uint64_t total_bytes = block_size * (uint64_t)vfs.f_blocks;
        uint64_t free_bytes = block_size * (uint64_t)vfs.f_bavail;
        uint64_t used_bytes = (total_bytes >= free_bytes) ? (total_bytes - free_bytes) : 0;

        snprintf(
            output, output_size,
            "mounted: yes\n"
            "path: %s\n"
            "card_handle: %s\n"
            "block_size: %llu\n"
            "total_bytes: %llu\n"
            "used_bytes: %llu\n"
            "free_bytes: %llu\n",
            TOOL_SDCARD_BASE,
            (s_sdcard_card != NULL) ? "yes" : "no",
            (unsigned long long)block_size,
            (unsigned long long)total_bytes,
            (unsigned long long)used_bytes,
            (unsigned long long)free_bytes
        );
    }

    return ESP_OK;
}

esp_err_t tool_sdcard_mount(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    sdmmc_card_t *card = NULL;
    esp_err_t err;

    if (sdcard_mountpoint_ready()) {
        return ESP_OK;
    }

    gpio_set_pull_mode(ECHOEAR_SD_CMD, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(ECHOEAR_SD_D0, GPIO_PULLUP_ONLY);

    host.slot = SDMMC_HOST_SLOT_1;
    slot_config.width = 1;
    slot_config.clk = ECHOEAR_SD_CLK;
    slot_config.cmd = ECHOEAR_SD_CMD;
    slot_config.d0 = ECHOEAR_SD_D0;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(
        TAG,
        "Mounting SD card via 1-bit SDMMC: CLK=%d CMD=%d D0=%d",
        ECHOEAR_SD_CLK, ECHOEAR_SD_CMD, ECHOEAR_SD_D0
    );

    err = esp_vfs_fat_sdmmc_mount(TOOL_SDCARD_BASE, &host, &slot_config, &mount_config, &card);
    if ((err == ESP_ERR_INVALID_STATE) && sdcard_mountpoint_ready()) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(err));
        return err;
    }

    s_sdcard_card = card;
    ESP_LOGI(TAG, "SD card mounted at %s", TOOL_SDCARD_BASE);
    if (s_sdcard_card != NULL) {
        sdmmc_card_print_info(stdout, s_sdcard_card);
    }
    return ESP_OK;
}

static bool validate_sd_path(const char *path, bool allow_root)
{
    if (path == NULL) {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }
    if (allow_root && strcmp(path, TOOL_SDCARD_BASE) == 0) {
        return true;
    }
    return strncmp(path, TOOL_SDCARD_BASE "/", sizeof(TOOL_SDCARD_BASE)) == 0;
}

static esp_err_t ensure_sdcard_ready(char *output, size_t output_size)
{
    if (!sdcard_mountpoint_ready()) {
        esp_err_t err = tool_sdcard_mount();
        if (err != ESP_OK) {
            snprintf(
                output, output_size,
                "Error: %s mount failed (%s)",
                TOOL_SDCARD_BASE, esp_err_to_name(err)
            );
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t tool_read_sd_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    cJSON *offset_item = NULL;
    cJSON *max_bytes_item = NULL;
    const char *path = NULL;
    struct stat st = { 0 };
    long offset = 0;
    size_t max_read = 0;
    FILE *f = NULL;
    size_t bytes_read = 0;
    esp_err_t err = ESP_OK;

    if (root == NULL) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    err = ensure_sdcard_ready(output, output_size);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }

    path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    offset_item = cJSON_GetObjectItem(root, "offset");
    max_bytes_item = cJSON_GetObjectItem(root, "max_bytes");

    if (!validate_sd_path(path, false)) {
        snprintf(output, output_size, "Error: path must start with /sdcard/ and must not contain '..'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if ((offset_item != NULL) && cJSON_IsNumber(offset_item)) {
        offset = (long)offset_item->valuedouble;
    }
    if (offset < 0) {
        snprintf(output, output_size, "Error: offset must be >= 0");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    max_read = (output_size > 0) ? (output_size - 1) : 0;
    if (max_read > TOOL_SDCARD_MAX_READ) {
        max_read = TOOL_SDCARD_MAX_READ;
    }
    if ((max_bytes_item != NULL) && cJSON_IsNumber(max_bytes_item) && (max_bytes_item->valuedouble > 0)) {
        size_t requested = (size_t)max_bytes_item->valuedouble;
        if (requested < max_read) {
            max_read = requested;
        }
    }

    if (stat(path, &st) != 0) {
        snprintf(output, output_size, "Error: file not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    if (!S_ISREG(st.st_mode)) {
        snprintf(output, output_size, "Error: path is not a regular file: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (offset > st.st_size) {
        snprintf(output, output_size, "Error: offset %ld exceeds file size %ld", offset, (long)st.st_size);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        snprintf(output, output_size, "Error: cannot open file: %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        snprintf(output, output_size, "Error: seek failed for %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    bytes_read = fread(output, 1, max_read, f);
    output[bytes_read] = '\0';

    if (ferror(f)) {
        fclose(f);
        snprintf(output, output_size, "Error: read failed for %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fclose(f);
    ESP_LOGI(
        TAG, "read_sd_file: %s offset=%ld bytes=%u/%ld",
        path, offset, (unsigned)bytes_read, (long)st.st_size
    );
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_list_sd_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *path = TOOL_SDCARD_BASE;
    struct stat st = { 0 };
    DIR *dir = NULL;
    struct dirent *ent = NULL;
    size_t off = 0;
    int count = 0;
    esp_err_t err = ESP_OK;

    if ((root == NULL) && (input_json != NULL) && (input_json[0] != '\0')) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    err = ensure_sdcard_ready(output, output_size);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }

    if (root != NULL) {
        const char *input_path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
        if ((input_path != NULL) && (input_path[0] != '\0')) {
            path = input_path;
        }
    }

    if (!validate_sd_path(path, true)) {
        snprintf(output, output_size, "Error: path must start with /sdcard and must not contain '..'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if ((stat(path, &st) != 0) || !S_ISDIR(st.st_mode)) {
        snprintf(output, output_size, "Error: directory not found: %s", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    dir = opendir(path);
    if (dir == NULL) {
        snprintf(output, output_size, "Error: cannot open directory: %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    while (((ent = readdir(dir)) != NULL) && (off < output_size - 1)) {
        char full_path[512];
        struct stat entry_st = { 0 };
        const char *suffix = "";

        if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);
        if ((stat(full_path, &entry_st) == 0) && S_ISDIR(entry_st.st_mode)) {
            suffix = "/";
        }

        if (stat(full_path, &entry_st) == 0) {
            off += snprintf(
                output + off, output_size - off, "%s%s (%ld bytes)\n",
                full_path, suffix, (long)entry_st.st_size
            );
        } else {
            off += snprintf(output + off, output_size - off, "%s%s\n", full_path, suffix);
        }
        count++;
    }

    closedir(dir);
    if (count == 0) {
        snprintf(output, output_size, "(empty directory)");
    }

    ESP_LOGI(TAG, "list_sd_dir: %s -> %d entries", path, count);
    cJSON_Delete(root);
    return ESP_OK;
}
