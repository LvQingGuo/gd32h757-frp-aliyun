// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file    cloud_log.h
 * @author  Qinn
 * @brief   将原工程日志接口替换为可由使用者覆盖的独立日志适配。
 * @details 默认不输出日志；自定义日志应支持多任务调用，并禁止输出凭据。
 */
#ifndef CLOUD_DEMO_LOG_H
#define CLOUD_DEMO_LOG_H
/** @brief 用自己的线程安全日志实现覆盖此宏；默认在不执行的分支检查格式和参数。 */
#include <stdio.h>
#ifndef CLOUD_LOG
#define CLOUD_LOG(level, module, ...) do { if(0) { (void)printf(__VA_ARGS__); } } while(0)
#endif
#define APP_LOG_DEBUG(module, ...) CLOUD_LOG("debug", module, __VA_ARGS__)
#define APP_LOG_INFO(module, ...) CLOUD_LOG("info", module, __VA_ARGS__)
#define APP_LOG_WARNING(module, ...) CLOUD_LOG("warning", module, __VA_ARGS__)
#endif
