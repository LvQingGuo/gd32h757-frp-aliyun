// SPDX-License-Identifier: GPL-3.0-only
/**
 ******************************************************************************
 * @file    cloud_client.c
 * @author  Qinn
 * @brief   实现远程调试云端隧道客户端，包括 FRP v1 控制流、认证心跳与 Web 工作连接。
 ******************************************************************************
 * @attention
 * 本模块属于可选远程调试功能；隧道异常不应影响本地采集和其他核心业务。
 * 控制流遵循当前适配的 frps 0.71.1 协议，tcpMux 必须关闭。
 ******************************************************************************
 * @details
 * 本文件负责 FRP 登录、代理注册、心跳维护和工作连接转发。
 * TLS 负责连接传输保护；登录成功后，控制流另使用 PBKDF2 派生密钥及 AES-CFB 保护。
 * 工作连接通过独立 TLS 连接桥接本机 Web 服务，不负责网络参数配置或底层 TLS 适配。
 ******************************************************************************
 */

/* 本模块依赖云端配置/端口适配、FreeRTOS 调度、LwIP Socket 与 mbedTLS。 */
#include "cloud_client.h"
#include "cloud_config.h"
#include "cloud_port.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
#include "mbedtls/ssl.h"
#include "mbedtls/platform_util.h"
#include "cloud_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CLOUD_WORK_LIMIT 3U      		/* 限制同时维护的工作连接任务数量。 */
#define CLOUD_MESSAGE_LIMIT 2048U		/* 限制单条控制消息正文的最大缓冲区容量。 */

/**
 * @brief  保存单条 FRP TLS 流的加密状态与收发偏移。
 * @details 控制流可启用 AES-CFB；工作连接维持 TLS 内的原始数据转发模式。
 */
typedef struct {
    cloudTls *tls;                   		/* 当前流使用的 TLS 连接上下文。 */
    bool encrypted;                  		/* 是否在 TLS 数据层之上启用 AES-CFB。 */
    bool sentIv;                     		/* 发送方向的初始向量是否已经发送。 */
    bool standby;                    		/* 工作流是否仍处于等待云端调度的备用状态。 */
    uint32_t workEpoch;              		/* 创建该工作流时记录的控制会话代际。 */
    size_t receivedIv;               		/* 接收方向已收取的初始向量字节数。 */
    unsigned char txIv[16], rxIv[16];		/* 发送和接收方向各自的 CFB 初始向量/反馈状态。 */
    size_t txOffset, rxOffset;       		/* AES-CFB 当前分组内的字节偏移。 */
    mbedtls_aes_context aes;         		/* 当前流独占的 AES 运算上下文。 */
} cloudStream;

/**
 * @brief  保存一条工作连接的生命周期状态和统计快照。
 * @details 工作槽数量受 CLOUD_WORK_LIMIT 限制；epoch 用于识别已失效的控制会话。
 */
typedef struct {
    volatile bool used;                            		/* 工作槽是否已分配给某条工作连接。 */
    uint32_t epoch;                                		/* 所属控制会话代际，用于淘汰旧连接。 */
    char runId[80];                                		/* FRP 控制会话返回的运行标识。 */
    volatile unsigned int stage;                   		/* 工作连接阶段，供状态接口观察。 */
    volatile uint32_t toBoard, toCloud, stackSpare;		/* 转发字节数及任务栈剩余空间快照。 */
    volatile int remoteFd, localFd;                		/* 云端与本机 Socket 描述符。 */
} cloudWork;

/* 独立示例使用常规 BSS；集成者可根据链接布局指定运行期配置的放置位置。 */
static cloudConfig sConfig;
static cloudWork sWork[CLOUD_WORK_LIMIT];
static volatile uint32_t sEpoch;                                                               		/* 控制会话代际；重连时递增以使旧工作任务失效。 */
static volatile unsigned int sState;                                                           		/* 当前客户端阶段，供状态查询接口读取。 */
static volatile uint32_t sLoginCount, sWorkCount, sRequestCount, sWorkAttempts, sPings, sPongs;		/* 运行计数器，不包含凭据或正文。 */
static const char *const sStateNames[] = {"未配置","对时中","TLS连接","FRP登录","代理注册","在线","退避"};       		/* 与 sState 阶段编号对应的显示名称。 */

/**
 * @brief  在指定时间内等待 Socket 变为可读。
 * @param  fd            待检查的 Socket 描述符。
 * @param  milliseconds  最大等待时间，单位为毫秒。
 * @return Socket 可读时返回 true；超时或等待失败时返回 false。
 * @details 若 mbedTLS 已缓存解密后的数据，调用方应先通过 cloudTlsPending() 检查。
 */
static bool socketReadable(int fd, uint32_t milliseconds)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(fd, &set);                                          		/* 将目标 Socket 加入可读事件集合 */
    timeout.tv_sec = milliseconds / 1000U;                     		/* 将毫秒超时拆分为秒和微秒 */
    timeout.tv_usec = (milliseconds % 1000U) * 1000U;
    return lwip_select(fd + 1, &set, NULL, NULL, &timeout) > 0;		/* 只有检测到可读事件才返回成功 */
}

/**
 * @brief  按当前 FRP 认证规则生成十六进制认证字段。
 * @param  output     接收 32 个十六进制字符及结尾零的输出缓冲区。
 * @param  timestamp 当前 UTC 秒时间戳。
 * @details 输入由配置中的 token 与十进制时间戳拼接后计算 MD5；临时明文和摘要使用后清零。
 */
static void authKey(char output[33], uint64_t timestamp)
{
    char input[112];
    unsigned char digest[16];
    size_t index;
    int length = snprintf(input, sizeof(input), "%s%llu", sConfig.token, (unsigned long long)timestamp);		/* 按协议拼接 token 与十进制 UTC 秒 */
    (void)mbedtls_md5((const unsigned char *)input, (size_t)length, digest);                            		/* 计算认证字段所需的 MD5 摘要 */
    for(index = 0U; index < sizeof(digest); ++index) (void)snprintf(output + 2U * index, 3U, "%02x", digest[index]);
    mbedtls_platform_zeroize(input, sizeof(input));                                                     		/* 清除包含 token 的临时输入缓冲区 */
    mbedtls_platform_zeroize(digest, sizeof(digest));                                                   		/* 清除临时摘要，避免残留在栈内存中 */
}

/**
 * @brief  从控制消息中提取指定的 JSON 字符串字段。
 * @param  json      待解析的以零结尾的 JSON 文本。
 * @param  key       要查找的字段名。
 * @param  output    接收字段值的缓冲区。
 * @param  capacity  输出缓冲区容量，包含结尾零。
 * @return 输入格式可接受且字段值可处理时返回 true；格式错误、不支持的转义或容量不足时返回 false。
 * @details 若字段不存在，函数返回 true 且 output 保持为空字符串。
 */
static bool jsonString(const char *json, const char *key, char *output, size_t capacity)
{
    const char *cursor = json;
    size_t keyLength = strlen(key);
    output[0] = '\0';                                                                                     		/* 先将输出置空，避免字段缺失时残留旧值 */
    while(*cursor != '\0') {
        const char *begin;
        size_t length;
        if(*cursor++ != '"') continue;
        begin = cursor;
        while(*cursor != '\0' && *cursor != '"') {
            if(*cursor == '\\' && cursor[1] != '\0') ++cursor;                                            		/* 跳过转义字符后的字节，避免误判字符串结束位置 */
            ++cursor;
        }
        if(*cursor != '"') return false;
        length = (size_t)(cursor - begin);
        ++cursor;
        if(length != keyLength || memcmp(begin, key, length) != 0) continue;
        while(*cursor == ' ' || *cursor == '\r' || *cursor == '\n' || *cursor == '\t') ++cursor;
        if(*cursor != ':') continue;
        ++cursor;
        while(*cursor == ' ' || *cursor == '\r' || *cursor == '\n' || *cursor == '\t') ++cursor;
        if(*cursor++ != '"') return false;
        length = 0U;
        while(*cursor != '\0' && *cursor != '"') {
            if(*cursor == '\\' || (unsigned char)*cursor < 0x20U || length + 1U >= capacity) return false;		/* 拒绝不支持的转义、控制字符或超出缓冲区的值 */
            output[length++] = *cursor++;
        }
        if(*cursor != '"') return false;
        output[length] = '\0';                                                                            		/* 为提取出的字符串补上结尾零 */
        return true;
    }
    return true;
}

/**
 * @brief  从 TLS 流中读取指定长度的数据。
 * @param  stream  当前流的 TLS、加密和调度状态。
 * @param  data    接收数据的缓冲区。
 * @param  length  需要完整读取的字节数。
 * @return 完整读取成功返回 true；连接失效、解密失败或读取超时返回 false。
 * @details 备用工作连接在会话代际失效时退出；开始接收帧后采用 10 秒读取时限。
 */
static bool readExact(cloudStream *stream, unsigned char *data, size_t length)
{
    TickType_t started = xTaskGetTickCount();
    size_t done = 0U;
    while(done < length) {
        int result;
        if(stream->standby && stream->workEpoch != sEpoch) return false;                             		/* 控制会话代际已变化时放弃旧工作连接 */
        if(stream->encrypted && stream->receivedIv < 16U) {
            result = cloudTlsRead(stream->tls, stream->rxIv + stream->receivedIv, 16U - stream->receivedIv);
            if(result > 0) { stream->receivedIv += (size_t)result; continue; }
        } else {
            result = cloudTlsRead(stream->tls, data + done, length - done);
            if(result > 0) {
                if(stream->standby && done == 0U) started = xTaskGetTickCount();                     		/* 从备用状态收到首个帧字节后开始计时 */
                if(stream->encrypted && mbedtls_aes_crypt_cfb128(&stream->aes, MBEDTLS_AES_DECRYPT,
                    (size_t)result, &stream->rxOffset, stream->rxIv, data + done, data + done) != 0) return false;
                done += (size_t)result;
                continue;
            }
        }
        if(result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return false;		/* 除可重试的 TLS 等待状态外均视为读取失败 */
        (void)socketReadable(cloudTlsSocket(stream->tls), 25U);
        vTaskDelay(pdMS_TO_TICKS(1U));                                                               		/* 短暂让出 CPU，避免无数据时持续忙等 */
        if((!stream->standby || done != 0U) &&
           (TickType_t)(xTaskGetTickCount() - started) > pdMS_TO_TICKS(10000U)) return false;
    }
    return true;
}

/**
 * @brief  按当前流的加密模式发送指定长度的数据。
 * @param  stream  当前流的 TLS 和 AES-CFB 状态。
 * @param  data    待发送的数据。
 * @param  length  待发送的字节数。
 * @return 全部数据发送成功返回 true，否则返回 false。
 * @details 未启用 CFB 时直接写入 TLS；启用后按实际发送字节数连续推进加密偏移。
 */
static bool writeBytes(cloudStream *stream, const unsigned char *data, size_t length)
{
    unsigned char temporary[256];
    if(!stream->encrypted) return cloudTlsWrite(stream->tls, data, length);
    if(!stream->sentIv) {
        if(!cloudTlsWrite(stream->tls, stream->txIv, 16U)) return false;      		/* 先发送本条加密流使用的初始向量 */
        stream->sentIv = true;                                                		/* 标记初始向量已发送，后续不重复发送 */
    }
    while(length != 0U) {
        size_t part = length > sizeof(temporary) ? sizeof(temporary) : length;		/* 分块加密，限制临时缓冲区占用 */
        if(mbedtls_aes_crypt_cfb128(&stream->aes, MBEDTLS_AES_ENCRYPT, part, &stream->txOffset,
                                   stream->txIv, data, temporary) != 0 ||
           !cloudTlsWrite(stream->tls, temporary, part)) return false;
        length -= part;
        data += part;
    }
    return true;
}

/**
 * @brief  将控制消息的类型、长度和 JSON 正文写入流。
 * @param  stream  目标 TLS 流及其加密状态。
 * @param  type    控制消息类型字节。
 * @param  json    以零结尾的 JSON 正文。
 * @return 消息头和正文均发送成功返回 true，否则返回 false。
 * @details 正文长度必须小于 CLOUD_MESSAGE_LIMIT；消息头为 1 字节类型、6 字节保留位和 2 字节长度字段。
 */
static bool writeMessage(cloudStream *stream, char type, const char *json)
{
    unsigned char header[9] = {0};
    size_t length = strlen(json);
    if(length >= CLOUD_MESSAGE_LIMIT) return false;
    header[0] = (unsigned char)type;          		/* 写入控制消息类型字段 */
    header[7] = (unsigned char)(length >> 8U);		/* 写入正文长度的高字节 */
    header[8] = (unsigned char)length;        		/* 写入正文长度的低字节 */
    return writeBytes(stream, header, sizeof(header)) && writeBytes(stream, (const unsigned char *)json, length);
}

/**
 * @brief  从流中读取并验证一条有界控制消息。
 * @param  stream  消息来源流。
 * @param  type    接收消息类型字节的输出指针。
 * @param  json    接收正文的缓冲区，容量为 CLOUD_MESSAGE_LIMIT。
 * @return 消息头和正文均完整且格式受支持时返回 true，否则返回 false。
 * @details 非零保留字段、空正文、超长正文或读取失败均按无效消息处理。
 */
static bool readMessage(cloudStream *stream, char *type, char json[CLOUD_MESSAGE_LIMIT])
{
    unsigned char header[9];
    size_t length, index;
    if(!readExact(stream, header, sizeof(header))) return false;
    stream->standby = false;                                                  		/* 收到工作流首帧后结束备用等待状态 */
    for(index = 1U; index < 7U; ++index) if(header[index] != 0U) return false;		/* 检查协议要求为零的保留字段 */
    length = ((size_t)header[7] << 8U) | header[8];
    if(length == 0U || length >= CLOUD_MESSAGE_LIMIT ||
       !readExact(stream, (unsigned char *)json, length)) return false;
    json[length] = '\0';                                                      		/* 将正文转换为可安全解析的 C 字符串 */
    *type = (char)header[0];                                                  		/* 向调用方返回控制消息类型 */
    return true;
}

/**
 * @brief  派生控制流 AES 密钥并启用 AES-CFB 保护。
 * @param  stream  需要启用控制流加密的流上下文。
 * @return 密钥派生、AES 初始化及随机 IV 生成成功时返回 true，否则返回 false。
 * @details 使用当前协议适配所需的 PBKDF2-HMAC-SHA1 参数；派生出的临时密钥在使用后清零。
 */
static bool enableControlCrypto(cloudStream *stream)
{
    unsigned char key[16];
    int result = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA1,
        (const unsigned char *)sConfig.token, strlen(sConfig.token),
        (const unsigned char *)"frp", 3U, 64U, sizeof(key), key);
    mbedtls_aes_init(&stream->aes);            		/* 初始化当前流独占的 AES 上下文 */
    if(result == 0) result = mbedtls_aes_setkey_enc(&stream->aes, key, 128U);
    mbedtls_platform_zeroize(key, sizeof(key));		/* 派生密钥装入 AES 后立即清除临时副本 */
    if(result != 0 || cloudRandom(NULL, stream->txIv, sizeof(stream->txIv)) != 0) return false;
    stream->encrypted = true;                  		/* 后续控制流收发启用 AES-CFB */
    return true;
}

/**
 * @brief  处理一条云端工作连接，并双向转发本机 Web 数据。
 * @param  argument 指向该连接对应 cloudWork 工作槽的参数。
 * @details 工作任务拥有独立 TLS 流和本机 Socket；退出时关闭资源、清除临时正文并释放工作槽。
 */
static void workTask(void *argument)
{
    cloudWork *work = argument;
    cloudStream stream;
    char json[CLOUD_MESSAGE_LIMIT], error[160], proxy[80], expected[80], key[33], type;
    unsigned char bytes[1024];
    int local = -1, result;
    unsigned int phase = 0U, stopReason = 0U;
    int stopError = 0;
    uint32_t toBoard = 0U, toCloud = 0U;
    uint64_t timestamp = cloudUtcNow();
    TickType_t lastProgress = xTaskGetTickCount();
    memset(&stream, 0, sizeof(stream));
    taskENTER_CRITICAL();
    ++sWorkAttempts;                                                        		/* 统计已尝试创建的工作连接任务 */
    taskEXIT_CRITICAL();
    stream.tls = cloudTlsOpen(sConfig.server, sConfig.serverPort, sConfig.ca);
    if(stream.tls == NULL) goto finished;
    work->stackSpare = uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
    phase = 1U;
    work->stage = 1U;
    work->remoteFd = cloudTlsSocket(stream.tls);
    timestamp = cloudUtcNow();
    authKey(key, timestamp);
    (void)snprintf(json, sizeof(json), "{\"run_id\":\"%s\",\"privilege_key\":\"%s\",\"timestamp\":%llu}",
                   work->runId, key, (unsigned long long)timestamp);
    if(!writeMessage(&stream, 'w', json)) goto finished;
    work->stage = 11U;                                                      		/* 标记工作连接已发送请求并等待云端分配 */
    stream.standby = true;
    stream.workEpoch = work->epoch;                                         		/* 保存该工作连接所属的控制会话代际 */
    if(!readMessage(&stream, &type, json) || type != 's' ||
       !jsonString(json, "error", error, sizeof(error)) || error[0] != '\0' ||
       !jsonString(json, "proxy_name", proxy, sizeof(proxy))) goto finished;
    (void)snprintf(expected, sizeof(expected), "%s.web", sConfig.device);
    if(strcmp(proxy, expected) != 0 || work->epoch != sEpoch) goto finished;		/* 仅接受匹配本设备代理且仍属于当前会话的响应 */
    phase = 2U;
    work->stage = 2U;
    local = cloudSocketConnect("127.0.0.1", sConfig.localPort, 3000U);      		/* 连接本机 Web 服务，不直接暴露业务端口到公网 */
    if(local < 0) goto finished;
    taskENTER_CRITICAL();
    ++sWorkCount;
    taskEXIT_CRITICAL();
    phase = 3U;
    work->stage = 3U;                                                       		/* 标记双向数据转发阶段已开始 */
    work->localFd = local;
    while(work->epoch == sEpoch) {
        bool progress = false;
        bool pending = cloudTlsPending(stream.tls);
        fd_set readable;
        struct timeval wait = {0, pending ? 0 : 100000};
        int remoteFd = cloudTlsSocket(stream.tls);
        int maximum = remoteFd > local ? remoteFd : local;
        FD_ZERO(&readable);
        FD_SET(remoteFd, &readable);
        FD_SET(local, &readable);
        result = lwip_select(maximum + 1, &readable, NULL, NULL, &wait);    		/* 同时等待云端 TLS 流和本机 Web Socket 的数据 */
        if(result < 0) { stopReason = 1U; stopError = errno; goto finished; }
        if(pending || FD_ISSET(remoteFd, &readable)) {
            result = cloudTlsRead(stream.tls, bytes, sizeof(bytes));
            if(result > 0) {
                size_t sent = 0U;
                TickType_t start = xTaskGetTickCount();
                while(sent < (size_t)result && work->epoch == sEpoch) {
                    int count = lwip_send(local, bytes + sent, (size_t)result - sent, 0);
                    if(count > 0) sent += (size_t)count;
                    else if(count < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) vTaskDelay(pdMS_TO_TICKS(1U));
                    else { stopReason = 2U; stopError = errno; goto finished; }
                    if((TickType_t)(xTaskGetTickCount() - start) > pdMS_TO_TICKS(30000U)) { stopReason = 3U; goto finished; }
                }
                toBoard += sent;                                            		/* 累计从云端转发到本机 Web 的字节数 */
                work->toBoard = toBoard;
                progress = true;
            } else if(result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) { stopReason = 4U; stopError = result; goto finished; }
        }
        if(FD_ISSET(local, &readable)) {
            result = lwip_recv(local, bytes, sizeof(bytes), 0);
            if(result > 0) {
                if(!cloudTlsWrite(stream.tls, bytes, (size_t)result)) { stopReason = 5U; goto finished; }
                toCloud += (uint32_t)result;                                		/* 累计从本机 Web 转发到云端的字节数 */
                work->toCloud = toCloud;
                progress = true;
            } else if(result == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) { stopReason = 6U; stopError = result == 0 ? 0 : errno; goto finished; }
        }
        if(progress) lastProgress = xTaskGetTickCount();                    		/* 有数据成功转发时刷新活动超时计时 */
        if((TickType_t)(xTaskGetTickCount() - lastProgress) > pdMS_TO_TICKS(180000U)) { stopReason = 7U; break; }
    }
finished:
    /* 原因1/2/3为select/本机发送/发送超时，4/5为TLS读/写，6为本机关闭，7为活动超时。 */
    APP_LOG_INFO("云端隧道", "工作结束：阶段=%u，原因=%u，错误=%d，入板=%lu字节，出板=%lu字节",
        phase, stopReason, stopError, (unsigned long)toBoard, (unsigned long)toCloud);
    if(phase < 3U) APP_LOG_WARNING("云端隧道", "工作连接创建失败阶段=%u", phase);
    work->stage = 4U;
    if(local >= 0) (void)lwip_close(local);                                 		/* 释放本机 Web Socket */
    work->stage = 5U;
    cloudTlsClose(stream.tls);                                              		/* 关闭该工作连接独占的 TLS 会话 */
    work->stage = 6U;
    mbedtls_platform_zeroize(json, sizeof(json));                           		/* 清除可能包含认证字段的临时 JSON 缓冲区 */
    taskENTER_CRITICAL();
    work->used = false;                                                     		/* 归还工作槽，允许后续请求复用 */
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

/**
 * @brief  为新的云端工作请求分配工作槽并创建任务。
 * @param  runId 当前 FRP 控制会话分配的运行标识。
 * @return 任务创建成功返回 true；无空槽或任务创建失败时返回 false。
 */
static bool startWork(const char *runId)
{
    unsigned int index;
    for(index = 0U; index < CLOUD_WORK_LIMIT; ++index) {
        if(sWork[index].used) continue;
        sWork[index].stage = 0U;
        sWork[index].stackSpare = 0U;
        sWork[index].toBoard = 0U;
        sWork[index].toCloud = 0U;
        sWork[index].remoteFd = -1;
        sWork[index].localFd = -1;
        sWork[index].used = true;                                                        		/* 先占用槽位，避免其他任务重复分配 */
        sWork[index].epoch = sEpoch;                                                     		/* 将工作槽绑定到当前控制会话代际 */
        (void)snprintf(sWork[index].runId, sizeof(sWork[index].runId), "%s", runId);
        if(xTaskCreate(workTask, "CloudWeb", 2048U, &sWork[index], 1U, NULL) != pdPASS) {		/* 任务创建失败时必须撤销槽位占用 */
            sWork[index].used = false;
            return false;
        }
        return true;
    }
    return false;
}

/**
 * @brief  检查所有工作槽是否都处于等待云端分配的阶段。
 * @return 所有槽均已使用且阶段为等待分配时返回 true，否则返回 false。
 */
static bool onlyStandbyWorkers(void)
{
    unsigned int index;
    for(index = 0U; index < CLOUD_WORK_LIMIT; ++index) {
        if(!sWork[index].used || sWork[index].stage != 11U) return false;		/* 存在空槽或非等待状态时，不能认定所有连接均在待分配 */
    }
    return true;
}

/**
 * @brief  执行一次 FRP 控制会话，包括登录、代理注册、心跳和工作请求调度。
 * @param  stream 已建立 TLS 连接的控制流上下文。
 * @details 会话失败时直接返回，由 cloudTask() 统一关闭连接并执行退避重连。
 */
static void controlSession(cloudStream *stream)
{
    char json[CLOUD_MESSAGE_LIMIT], runId[80], error[160], version[24], key[33], type;
    unsigned int pendingWork = 0U;
    uint64_t timestamp = cloudUtcNow();
    TickType_t pingTick = xTaskGetTickCount(), pongTick = pingTick, blockedTick = 0U;
    bool blocked = false;
    sState = 3U;                                                                 		/* 更新对外可见的客户端阶段为 FRP 登录 */
    authKey(key, timestamp);
    (void)snprintf(json, sizeof(json),
        "{\"version\":\"0.71.1\",\"hostname\":\"%s\",\"os\":\"freertos\",\"arch\":\"cortex-m7\","
        "\"user\":\"%s\",\"client_id\":\"%s\",\"privilege_key\":\"%s\",\"timestamp\":%llu,\"pool_count\":0,"
        "\"metas\":{\"device_id\":\"%s\",\"device_token\":\"%s\"}}",
        sConfig.device, sConfig.device, sConfig.device, key, (unsigned long long)timestamp, sConfig.device, sConfig.deviceSecret);
    if(!writeMessage(stream, 'o', json) || !readMessage(stream, &type, json) || type != '1') {
        APP_LOG_WARNING("云端隧道", "FRP登录响应读取失败");
        return;
    }
    if(!jsonString(json, "error", error, sizeof(error)) || error[0] != '\0') {
        APP_LOG_WARNING("云端隧道", "FRP登录被拒绝");
        return;
    }
    if(!jsonString(json, "run_id", runId, sizeof(runId)) || runId[0] == '\0' ||
       !jsonString(json, "version", version, sizeof(version)) || strcmp(version, "0.71.1") != 0) {
        APP_LOG_WARNING("云端隧道", "FRP登录响应字段或版本不兼容");
        return;
    }
    if(!enableControlCrypto(stream)) {                                           		/* 登录响应通过后才初始化控制流的独立加密层 */
        APP_LOG_WARNING("云端隧道", "FRP控制流密码上下文初始化失败");
        return;
    }
    ++sLoginCount;
    sState = 4U;                                                                 		/* 进入代理注册阶段 */
    (void)snprintf(json, sizeof(json),
        "{\"proxy_name\":\"%s.web\",\"proxy_type\":\"tcp\",\"remote_port\":%u,\"use_encryption\":false,\"use_compression\":false}",
        sConfig.device, sConfig.proxyPort);
    if(!writeMessage(stream, 'p', json)) return;                                 		/* 注册本设备的 Web TCP 代理 */
    for(;;) {
        TickType_t now = xTaskGetTickCount();
        (void)cloudUtcNow();
        while(pendingWork != 0U && startWork(runId)) --pendingWork;              		/* 将排队的工作请求分配给可用工作槽 */
        /* 服务端仍要求连接、但全部槽长期未获StartWorkConn时，回收失配会话而非永久占槽。 */
        if(pendingWork != 0U && onlyStandbyWorkers()) {
            if(!blocked) { blocked = true; blockedTick = now; }
            else if((TickType_t)(now - blockedTick) >= pdMS_TO_TICKS(20000U)) {
                APP_LOG_WARNING("云端隧道", "工作分配停滞20秒，重建隧道会话");
                return;
            }
        } else blocked = false;
        if((TickType_t)(now - pingTick) >= pdMS_TO_TICKS(10000U)) {
            timestamp = cloudUtcNow();
            authKey(key, timestamp);
            (void)snprintf(json, sizeof(json), "{\"privilege_key\":\"%s\",\"timestamp\":%llu}",
                           key, (unsigned long long)timestamp);
            if(!writeMessage(stream, 'h', json)) { APP_LOG_WARNING("云端隧道", "心跳发送失败"); return; }
            ++sPings;                                                            		/* 统计已发送的认证心跳数量 */
            APP_LOG_DEBUG("云端隧道", "已发送FRP心跳");
            pingTick = now;
        }
        if((TickType_t)(now - pongTick) > pdMS_TO_TICKS(45000U)) { APP_LOG_WARNING("云端隧道", "等待Pong超时，已发送=%lu，已接收=%lu", (unsigned long)sPings, (unsigned long)sPongs); return; }
        if(!cloudTlsPending(stream->tls) && !socketReadable(cloudTlsSocket(stream->tls), 100U)) continue;
        if(!readMessage(stream, &type, json)) { APP_LOG_WARNING("云端隧道", "读取控制消息失败"); return; }
        APP_LOG_DEBUG("云端隧道", "收到控制消息：类型=%c，长度=%lu", type, (unsigned long)strlen(json));
        if(type == 'r') { ++sRequestCount; if(pendingWork < 16U) ++pendingWork; }		/* 记录云端工作请求，并限制本地排队数量 */
        else if(type == '4') {
            if(!jsonString(json, "error", error, sizeof(error)) || error[0] != '\0') {
                APP_LOG_WARNING("云端隧道", "FRP心跳认证被拒绝");
                return;
            }
            ++sPongs;                                                            		/* 记录有效心跳响应 */
            pongTick = xTaskGetTickCount();
        }
        else if(type == '2') {
            if(!jsonString(json, "error", error, sizeof(error)) || error[0] != '\0') return;
            sState = 5U;                                                         		/* 标记代理已在线 */
            APP_LOG_INFO("云端隧道", "代理在线：设备=%s，云端回环端口=%u", sConfig.device, sConfig.proxyPort);

        }
    }
}

/**
 * @brief  云端客户端的后台任务入口。
 * @param  argument FreeRTOS 任务参数，本任务不使用该参数。
 * @details 依次完成认证对时、TLS 建连和控制会话；失败后清理资源并按上限 30 秒的退避间隔重试。
 */
static void cloudTask(void *argument)
{
    uint32_t backoff = 1U;
    (void)argument;
    for(;;) {
        cloudStream stream;
        memset(&stream, 0, sizeof(stream));
        sState = 1U;                               		/* 标记正在执行认证对时 */
        if(cloudSyncTime(sConfig.server, sConfig.timePort, sConfig.device, sConfig.deviceSecret,
                         sConfig.keyVersion, &sConfig.proxyPort)) {
            sState = 2U;                           		/* 对时成功后进入 TLS 建连阶段 */
            stream.tls = cloudTlsOpen(sConfig.server, sConfig.serverPort, sConfig.ca);
            if(stream.tls != NULL) controlSession(&stream);
        }
        if(sState == 5U) backoff = 1U;
        APP_LOG_WARNING("云端隧道", "会话结束阶段=%u", sState);
        ++sEpoch;                                  		/* 使旧控制会话关联的工作任务尽快退出 */
        cloudTlsClose(stream.tls);
        mbedtls_aes_free(&stream.aes);             		/* 释放控制流 AES 上下文资源 */
        sState = 6U;
        APP_LOG_WARNING("云端隧道", "连接结束，%lu秒后重试；采集任务继续运行", (unsigned long)backoff);
        vTaskDelay(pdMS_TO_TICKS(backoff * 1000U));		/* 退避等待期间不阻塞其他 FreeRTOS 任务 */
        if(backoff < 30U) backoff = backoff > 15U ? 30U : backoff * 2U;
    }
}

/**
 * @brief  初始化云端适配并按产品配置启动后台客户端任务。
 * @return 配置无效而安全禁用云功能，或任务创建成功时返回 true；任务创建失败时返回 false。
 * @details 本入口不改变设备固定业务 IP；配置或 UID 无效时保留本地业务运行。
 */
bool cloudClientStart(void)
{
    cloudPortInit();                		/* 初始化云端客户端依赖的端口适配 */
    if(!cloudConfigLoad(&sConfig)) {		/* 配置或 UID 无效时安全禁用可选云功能 */
        APP_LOG_INFO("云端隧道", "产品参数或UID无效，远程服务保持禁用");
        return true;
    }
    return xTaskCreate(cloudTask, "CloudControl", 2048U, NULL, 1U, NULL) == pdPASS;
}

/**
 * @brief  输出云端客户端的连接状态、公开端点和内存统计。
 * @details 日志不输出 token、认证字段或工作会话正文。
 */
void cloudClientShow(void)
{
    unsigned int index, active = 0U;
    size_t current, peak;
    for(index = 0U; index < CLOUD_WORK_LIMIT; ++index) if(sWork[index].used) ++active;
    cloudMemoryStats(&current, &peak);		      /* 获取密码库当前内存占用及峰值 */
    APP_LOG_INFO("云端隧道", "状态=%s，设备=%s，服务器=%s:%u，UTC=%llu，登录=%lu，工作=%lu，活动=%u，请求=%lu/尝试%lu，密码库内存=%lu/峰值%lu",
        sStateNames[sState <= 6U ? sState : 0U], sConfig.device, sConfig.server, sConfig.serverPort,
        (unsigned long long)cloudUtcNow(), (unsigned long)sLoginCount, (unsigned long)sWorkCount,
        active, (unsigned long)sRequestCount, (unsigned long)sWorkAttempts, (unsigned long)current, (unsigned long)peak);
    APP_LOG_INFO("云端隧道", "身份=配置v%lu，认证代理端口=%u",
                 (unsigned long)sConfig.keyVersion, sConfig.proxyPort);
    for(index = 0U; index < CLOUD_WORK_LIMIT; ++index) {
        if(sWork[index].used) APP_LOG_INFO("云端隧道", "槽%u：阶段=%u，入板=%lu，出板=%lu，Socket=%d/%d，栈余量=%lu",
            index, sWork[index].stage, (unsigned long)sWork[index].toBoard,
            (unsigned long)sWork[index].toCloud, sWork[index].remoteFd, sWork[index].localFd, (unsigned long)sWork[index].stackSpare);
    }
}
