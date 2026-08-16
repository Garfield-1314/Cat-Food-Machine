/* 模拟器桩：NVS 键值存储（内存模拟） */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define MAX_KEYS 32

typedef struct {
    char key[32];
    uint8_t value_u8;
    bool has_u8;
    int32_t value_i32;
    bool has_i32;
} nvs_entry_t;

static nvs_entry_t s_entries[MAX_KEYS];
static int s_count = 0;

static nvs_entry_t *find(const char *key)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].key, key) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static nvs_entry_t *alloc(const char *key)
{
    nvs_entry_t *e = find(key);
    if (e) {
        return e;
    }
    if (s_count < MAX_KEYS) {
        e = &s_entries[s_count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->key, key, sizeof(e->key) - 1);
        return e;
    }
    return NULL;
}

esp_err_t nvs_open(const char *name, uint32_t open_mode, nvs_handle_t *out_handle)
{
    (void)name;
    (void)open_mode;
    if (out_handle) {
        *out_handle = 1;
    }
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value)
{
    (void)handle;
    nvs_entry_t *e = find(key);
    if (e && e->has_u8) {
        if (out_value) *out_value = e->value_u8;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    (void)handle;
    nvs_entry_t *e = alloc(key);
    if (!e) return ESP_FAIL;
    e->value_u8 = value;
    e->has_u8 = true;
    return ESP_OK;
}

esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value)
{
    (void)handle;
    nvs_entry_t *e = find(key);
    if (e && e->has_i32) {
        if (out_value) *out_value = e->value_i32;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    (void)handle;
    nvs_entry_t *e = alloc(key);
    if (!e) return ESP_FAIL;
    e->value_i32 = value;
    e->has_i32 = true;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    (void)handle;
    (void)key;
    (void)out_value;
    (void)length;
    return ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    (void)handle;
    (void)key;
    (void)value;
    (void)length;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    nvs_entry_t *e = find(key);
    if (e) {
        memset(e, 0, sizeof(*e));
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_flash_init(void)
{
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    return ESP_OK;
}
