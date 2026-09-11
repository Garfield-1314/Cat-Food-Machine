#ifndef __SECURE_MSG_H
#define __SECURE_MSG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 应用层安全信封：AES-256-GCM + HMAC-SHA256
 * 与云端 cloud/functions/deviceGateway/secure.js 完全对应。
 *
 * 密钥派生：
 *   base(boot) = SHA256("catfood:boot:" + token)
 *   base(cmd)  = SHA256("catfood:cmd:" + token + ":" + ownerOpenid)
 *   macKey     = SHA256(base + "|mac")
 *   encKey     = SHA256(base + "|enc")
 *
 * 信封 JSON：{"v":1,"iv":b64,"ct":b64,"tag":b64,"sig":b64}
 *   sig = HMAC-SHA256(macKey, "1|" + b64(iv) + "|" + b64(ct) + "|" + b64(tag))
 */

#define SECURE_NONCE_HEX_LEN 32

/**
 * @brief 打包 JSON 对象为加密信封
 * @param token 设备绑定 token
 * @param openid 主账号 openid；为 NULL 时使用 boot 密钥（绑定阶段）
 * @param obj 待加密 JSON 对象
 * @return malloc 的 JSON 字符串（调用方 free），失败返回 NULL
 */
char *secure_msg_pack_json(const char *token, const char *openid, const cJSON *obj);

/**
 * @brief 打包二进制数据为加密信封
 * @return malloc 的 JSON 字符串（调用方 free），失败返回 NULL
 */
char *secure_msg_pack_buffer(const char *token, const char *openid,
                             const uint8_t *data, size_t len);

/**
 * @brief 解包加密信封为二进制
 * @param out_len 输出明文长度
 * @return malloc 的明文（调用方 free），失败返回 NULL
 */
uint8_t *secure_msg_unpack_buffer(const char *token, const char *openid,
                                  const char *envelope, size_t *out_len);

/**
 * @brief 解包加密信封为 JSON 对象
 * @return cJSON 对象（调用方 cJSON_Delete），失败返回 NULL
 */
cJSON *secure_msg_unpack_json(const char *token, const char *openid,
                              const char *envelope);

/**
 * @brief 防重放校验：检查 nonce 是否重复、时间戳是否在允许窗口内
 * @param obj 已解密的消息对象（需含 nonce/ts 字段）
 * @return true 通过并记录 nonce；false 拒绝
 */
bool secure_msg_accept_message(const cJSON *obj);

/**
 * @brief 防重放校验（自定义时间窗）
 *
 * 绑定握手对时钟漂移更宽容；指令仍应使用默认的 ±120s 窗口。
 *
 * @param obj 已解密的消息对象（需含 nonce/ts 字段）
 * @param window_sec 允许的时间偏差（秒）
 * @return true 通过并记录 nonce；false 拒绝
 */
bool secure_msg_accept_message_window(const cJSON *obj, int64_t window_sec);

#ifdef __cplusplus
}
#endif

#endif /* __SECURE_MSG_H */
