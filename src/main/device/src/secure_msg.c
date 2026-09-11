#include "device/inc/secure_msg.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

static const char *TAG = "secure_msg";

#define IV_LEN             12
#define TAG_LEN            16
#define KEY_LEN            32
#define SIG_LEN            32
#define REPLAY_CACHE_SIZE  16
#define TS_WINDOW_SEC      120

#define ENVELOPE_TEMPLATE  "{\"v\":1,\"iv\":\"%s\",\"ct\":\"%s\",\"tag\":\"%s\",\"sig\":\"%s\"}"

/* 大缓冲优先使用 PSRAM，避免内部 RAM 不足（图片信封可达数百 KB） */
static void *sec_malloc(size_t size)
{
#ifdef CONFIG_SPIRAM
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p != NULL) {
        return p;
    }
#endif
    return malloc(size);
}

typedef struct {
    bool used;
    char nonce[SECURE_NONCE_HEX_LEN + 1];
    int64_t ts;
} replay_entry_t;

static replay_entry_t s_replay[REPLAY_CACHE_SIZE];
static int s_replay_next = 0;

/* 密钥派生：SHA256(base + suffix)，base 为 boot 或 cmd 主密钥 */
static void derive_subkey(bool boot, const char *token, const char *openid,
                          const char *suffix, uint8_t out[KEY_LEN])
{
    uint8_t base[KEY_LEN];
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    const char *prefix = boot ? "catfood:boot:" : "catfood:cmd:";
    mbedtls_sha256_update(&ctx, (const uint8_t *)prefix, strlen(prefix));
    mbedtls_sha256_update(&ctx, (const uint8_t *)token, strlen(token));
    if (!boot && openid != NULL) {
        mbedtls_sha256_update(&ctx, (const uint8_t *)":", 1);
        mbedtls_sha256_update(&ctx, (const uint8_t *)openid, strlen(openid));
    }
    mbedtls_sha256_finish(&ctx, base);
    mbedtls_sha256_free(&ctx);

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, base, sizeof(base));
    mbedtls_sha256_update(&ctx, (const uint8_t *)suffix, strlen(suffix));
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

static bool hmac_sha256(const uint8_t key[KEY_LEN], const uint8_t *data,
                        size_t len, uint8_t out[SIG_LEN])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return false;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    int ret = mbedtls_md_hmac_starts(&ctx, key, KEY_LEN);
    if (ret == 0) {
        ret = mbedtls_md_hmac_update(&ctx, data, len);
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_finish(&ctx, out);
    }
    mbedtls_md_free(&ctx);
    return ret == 0;
}

static char *b64_encode(const uint8_t *data, size_t len)
{
    size_t cap = 4 * ((len + 2) / 3) + 1;
    char *out = sec_malloc(cap);
    if (out == NULL) {
        return NULL;
    }

    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, cap, &olen, data, len) != 0) {
        free(out);
        return NULL;
    }
    out[olen] = '\0';
    return out;
}

static uint8_t *b64_decode(const char *str, size_t *out_len)
{
    if (str == NULL) {
        return NULL;
    }

    size_t slen = strlen(str);
    size_t cap = (slen / 4 + 1) * 3 + 1;
    uint8_t *out = sec_malloc(cap);
    if (out == NULL) {
        return NULL;
    }

    size_t olen = 0;
    if (mbedtls_base64_decode(out, cap, &olen, (const unsigned char *)str, slen) != 0) {
        free(out);
        return NULL;
    }
    *out_len = olen;
    return out;
}

static char *pack_internal(bool boot, const char *token, const char *openid,
                           const uint8_t *pt, size_t pt_len)
{
    if (token == NULL || token[0] == '\0' || (!boot && (openid == NULL || openid[0] == '\0'))) {
        return NULL;
    }

    uint8_t enc_key[KEY_LEN];
    uint8_t mac_key[KEY_LEN];
    derive_subkey(boot, token, openid, "|enc", enc_key);
    derive_subkey(boot, token, openid, "|mac", mac_key);

    uint8_t iv[IV_LEN];
    esp_fill_random(iv, sizeof(iv));

    uint8_t *ct = sec_malloc(pt_len > 0 ? pt_len : 1);
    if (ct == NULL) {
        return NULL;
    }

    uint8_t tag[TAG_LEN];
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int gcm_ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, enc_key, KEY_LEN * 8);
    if (gcm_ret == 0) {
        gcm_ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, pt_len,
                                            iv, sizeof(iv), NULL, 0,
                                            pt, ct, TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    if (gcm_ret != 0) {
        free(ct);
        ESP_LOGW(TAG, "gcm encrypt failed");
        return NULL;
    }

    char *b64_iv = b64_encode(iv, sizeof(iv));
    char *b64_ct = b64_encode(ct, pt_len);
    char *b64_tag = b64_encode(tag, sizeof(tag));
    free(ct);
    if (b64_iv == NULL || b64_ct == NULL || b64_tag == NULL) {
        free(b64_iv);
        free(b64_ct);
        free(b64_tag);
        return NULL;
    }

    size_t msg_len = 2 + strlen(b64_iv) + 1 + strlen(b64_ct) + 1 + strlen(b64_tag) + 1;
    char *msg = sec_malloc(msg_len);
    uint8_t sig[SIG_LEN];
    bool sig_ok = false;
    if (msg != NULL) {
        snprintf(msg, msg_len, "1|%s|%s|%s", b64_iv, b64_ct, b64_tag);
        sig_ok = hmac_sha256(mac_key, (const uint8_t *)msg, strlen(msg), sig);
        free(msg);
    }
    if (!sig_ok) {
        free(b64_iv);
        free(b64_ct);
        free(b64_tag);
        return NULL;
    }

    char *b64_sig = b64_encode(sig, sizeof(sig));
    if (b64_sig == NULL) {
        free(b64_iv);
        free(b64_ct);
        free(b64_tag);
        return NULL;
    }

    /* 手工拼接信封，避免 cJSON 对大字段（图片 base64）的多次内部 RAM 复制 */
    size_t out_len = sizeof(ENVELOPE_TEMPLATE) + strlen(b64_iv) + strlen(b64_ct) +
                     strlen(b64_tag) + strlen(b64_sig);
    char *out = sec_malloc(out_len);
    if (out != NULL) {
        snprintf(out, out_len, ENVELOPE_TEMPLATE, b64_iv, b64_ct, b64_tag, b64_sig);
    }

    free(b64_iv);
    free(b64_ct);
    free(b64_tag);
    free(b64_sig);
    return out;
}

static uint8_t *unpack_internal(bool boot, const char *token, const char *openid,
                                const char *envelope, size_t *out_len)
{
    if (token == NULL || envelope == NULL || out_len == NULL) {
        return NULL;
    }

    cJSON *env = cJSON_Parse(envelope);
    if (env == NULL) {
        return NULL;
    }

    cJSON *v = cJSON_GetObjectItem(env, "v");
    cJSON *iv_j = cJSON_GetObjectItem(env, "iv");
    cJSON *ct_j = cJSON_GetObjectItem(env, "ct");
    cJSON *tag_j = cJSON_GetObjectItem(env, "tag");
    cJSON *sig_j = cJSON_GetObjectItem(env, "sig");

    if (!cJSON_IsNumber(v) || v->valueint != 1 ||
        !cJSON_IsString(iv_j) || !cJSON_IsString(ct_j) ||
        !cJSON_IsString(tag_j) || !cJSON_IsString(sig_j)) {
        cJSON_Delete(env);
        return NULL;
    }

    size_t iv_len = 0, ct_len = 0, tag_len = 0, sig_len = 0;
    uint8_t *iv = b64_decode(iv_j->valuestring, &iv_len);
    uint8_t *ct = b64_decode(ct_j->valuestring, &ct_len);
    uint8_t *tag = b64_decode(tag_j->valuestring, &tag_len);
    uint8_t *sig = b64_decode(sig_j->valuestring, &sig_len);

    uint8_t *result = NULL;
    if (iv == NULL || ct == NULL || tag == NULL || sig == NULL ||
        iv_len != IV_LEN || tag_len != TAG_LEN || sig_len != SIG_LEN) {
        goto cleanup;
    }

    uint8_t mac_key[KEY_LEN];
    uint8_t enc_key[KEY_LEN];
    derive_subkey(boot, token, openid, "|mac", mac_key);
    derive_subkey(boot, token, openid, "|enc", enc_key);

    char *b64_iv = b64_encode(iv, iv_len);
    char *b64_ct = b64_encode(ct, ct_len);
    char *b64_tag = b64_encode(tag, tag_len);
    if (b64_iv == NULL || b64_ct == NULL || b64_tag == NULL) {
        free(b64_iv);
        free(b64_ct);
        free(b64_tag);
        goto cleanup;
    }

    size_t msg_len = 2 + strlen(b64_iv) + 1 + strlen(b64_ct) + 1 + strlen(b64_tag) + 1;
    char *msg = sec_malloc(msg_len);
    uint8_t expect[SIG_LEN];
    bool sig_ok = false;
    if (msg != NULL) {
        snprintf(msg, msg_len, "1|%s|%s|%s", b64_iv, b64_ct, b64_tag);
        sig_ok = hmac_sha256(mac_key, (const uint8_t *)msg, strlen(msg), expect);
        free(msg);
    }
    free(b64_iv);
    free(b64_ct);
    free(b64_tag);

    if (!sig_ok || memcmp(expect, sig, SIG_LEN) != 0) {
        ESP_LOGW(TAG, "signature mismatch");
        goto cleanup;
    }

    uint8_t *pt = sec_malloc(ct_len > 0 ? ct_len : 1);
    if (pt == NULL) {
        goto cleanup;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int gcm_ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, enc_key, KEY_LEN * 8);
    if (gcm_ret == 0) {
        gcm_ret = mbedtls_gcm_auth_decrypt(&gcm, ct_len, iv, iv_len, NULL, 0,
                                           tag, TAG_LEN, ct, pt);
    }
    mbedtls_gcm_free(&gcm);

    if (gcm_ret != 0) {
        ESP_LOGW(TAG, "gcm decrypt failed");
        free(pt);
        goto cleanup;
    }

    *out_len = ct_len;
    result = pt;

cleanup:
    free(iv);
    free(ct);
    free(tag);
    free(sig);
    cJSON_Delete(env);
    return result;
}

char *secure_msg_pack_json(const char *token, const char *openid, const cJSON *obj)
{
    if (obj == NULL) {
        return NULL;
    }

    char *plain = cJSON_PrintUnformatted(obj);
    if (plain == NULL) {
        return NULL;
    }

    char *out = pack_internal(openid == NULL, token, openid,
                              (const uint8_t *)plain, strlen(plain));
    free(plain);
    return out;
}

char *secure_msg_pack_buffer(const char *token, const char *openid,
                             const uint8_t *data, size_t len)
{
    return pack_internal(openid == NULL, token, openid, data, len);
}

uint8_t *secure_msg_unpack_buffer(const char *token, const char *openid,
                                  const char *envelope, size_t *out_len)
{
    return unpack_internal(openid == NULL, token, openid, envelope, out_len);
}

cJSON *secure_msg_unpack_json(const char *token, const char *openid,
                              const char *envelope)
{
    size_t len = 0;
    uint8_t *plain = unpack_internal(openid == NULL, token, openid, envelope, &len);
    if (plain == NULL) {
        return NULL;
    }

    cJSON *obj = cJSON_ParseWithLength((const char *)plain, len);
    free(plain);
    return obj;
}

bool secure_msg_accept_message(const cJSON *obj)
{
    return secure_msg_accept_message_window(obj, TS_WINDOW_SEC);
}

bool secure_msg_accept_message_window(const cJSON *obj, int64_t window_sec)
{
    if (obj == NULL) {
        return false;
    }

    cJSON *nonce = cJSON_GetObjectItem(obj, "nonce");
    cJSON *ts = cJSON_GetObjectItem(obj, "ts");
    if (!cJSON_IsString(nonce) || nonce->valuestring[0] == '\0' ||
        strlen(nonce->valuestring) > SECURE_NONCE_HEX_LEN) {
        return false;
    }
    if (!cJSON_IsNumber(ts)) {
        return false;
    }

    int64_t now = (int64_t)time(NULL);
    int64_t msg_ts = (int64_t)ts->valuedouble;

    /* 时间未同步时跳过时间窗校验，仍由 nonce 防重放 */
    if (now > 1704067200) { /* 2024-01-01 */
        int64_t diff = now - msg_ts;
        if (diff > window_sec || diff < -window_sec) {
            ESP_LOGW(TAG, "timestamp out of window (%lld, limit %lld)",
                     (long long)diff, (long long)window_sec);
            return false;
        }
    }

    for (int i = 0; i < REPLAY_CACHE_SIZE; i++) {
        if (s_replay[i].used && strcmp(s_replay[i].nonce, nonce->valuestring) == 0) {
            ESP_LOGW(TAG, "replayed nonce rejected");
            return false;
        }
    }

    replay_entry_t *slot = &s_replay[s_replay_next];
    slot->used = true;
    strncpy(slot->nonce, nonce->valuestring, sizeof(slot->nonce) - 1);
    slot->nonce[sizeof(slot->nonce) - 1] = '\0';
    slot->ts = msg_ts;
    s_replay_next = (s_replay_next + 1) % REPLAY_CACHE_SIZE;
    return true;
}
