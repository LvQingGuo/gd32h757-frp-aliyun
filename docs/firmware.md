# 固件集成

### 最低板端前提

本仓库提供可集成到已有工程的 FRP 客户端模块。按当前实现复现，需要先具备以下基础：

| 条件 | 准备要求 |
| --- | --- |
| 可运行的 GD32 工程 | 已完成启动、系统时钟、编译链接、烧录和调试；当前 `cloud_port.c` 直接使用 GD32H7 TRNG/时钟寄存器与外设库 |
| 可用网络 | 网络接口及驱动、lwIP 与 RTOS 的 `sys_arch` 移植已工作；板端 IPv4、掩码、网关正确，能向 ECS IPv4 发起 TCP 7000 和 UDP 7000 连接 |
| FreeRTOS | 调度器、Tick、临界区、任务创建/删除、任务延时、动态内存分配/释放和剩余堆查询可用；启用 `uxTaskGetStackHighWaterMark()` 支持 |
| lwIP | 使用 RTOS 模式，开启 IPv4、TCP、UDP、Netconn、Socket、`select`、非阻塞操作、收发超时及本机回环；关键开关见下文 |
| Mbed TLS | 以 `cmu_mbedtls_config.h` 统一编译库与应用，支持 TLS 1.2、P-256/ECDHE-ECDSA/AES-GCM，以及 FRP v1 所需的 MD5/PBKDF2/AES-CFB |
| 随机源与内存 | GD32H7 TRNG 可用，或为其他芯片移植等效可靠熵源；为 TLS、lwIP、各任务栈和本机服务共同安排 RAM/Flash |
| 本机被访问服务 | 完整验证工作连接时需要可从回环地址访问的 TCP 服务；当前示例为 `127.0.0.1:8080` 的 HTTP 服务 |
| 配置与证书 | `cloud_demo_config.h` 中写入自己的服务器 IPv4、CA 公钥证书、FRP token、设备 ID 和独立设备密钥；与 ECS 配置一致 |

板卡无需公网 IP、路由器端口映射或阿里云 SDK。仅建立板端与 frps 的连接不要求电脑处于另一局域网；跨局域网浏览器访问是后续验收场景。

当前客户端启动顺序为：**认证 UDP 引导取得 UTC/代理端口 → TLS 证书验证 → FRP 登录 → 代理注册 → 按需连接本机服务并转发**。因此，`deploy/bootstrap_demo.py` 或与之兼容的服务必须先在服务器运行；只有官方 frps、没有引导服务时，当前代码不会进入 FRP 登录阶段。

### 其他 GD32 型号的移植范围

可复用 `cloud_client.c` 的协议和工作流、`cloud_bootstrap.c` 的报文处理及配置模板。`cloud_port.c` 中的 `gd32h7xx.h`、TRNG 操作、时钟和熵回调需要按芯片更换；同时检查 FreeRTOS/lwIP 移植层、编译器和链接布局。没有同类 TRNG 的芯片需要另行提供可靠熵源，不能简单返回固定或弱随机数据。若改用其他 RTOS/网络栈，还需要替换核心中的任务和 Socket 调用。

### 平台与文件

实际目标为 **GD32H757VMT6 / Cortex-M7**。原工程使用 Keil MDK Arm Compiler 6.21；本仓库不包含其私有工程文件、启动代码、ENET/PHY 驱动或完整 BSP。

外部依赖：FreeRTOS、lwIP 2.1.2、Mbed TLS 3.6.7，以及 GD32H7 CMSIS/标准外设库。使用者应从各自官方来源取得依赖，保留许可文本。版本为已应用系统的集成基线，不代表最新版本或任意版本兼容承诺。

| 文件 | 职责 |
| --- | --- |
| `cloud_client.c/.h` | FRP v1 帧、登录、代理注册、心跳、工作任务和退避重连 |
| `cloud_port.c/.h` | GD32 TRNG、FreeRTOS 内存与时间、lwIP Socket、TLS |
| `cloud_bootstrap.c/.h` | 固定长度 HMAC-SHA256 认证引导请求/回包 |
| `cmu_mbedtls_config.h` | TLS 1.2/P-256/ECDHE-ECDSA/AES-GCM 配置及 FRP v1 兼容算法 |
| `cloud_config.c/.h` | 首版独立示例的人工配置加载；替代原产品 UID/根密钥流程 |
| `cloud_log.h` | 可覆盖的线程安全日志入口，默认不输出 |

提取时保留核心协议与 TLS 实现；将私有日志依赖替换为 `cloud_log.h`，移除 `.bss.cloud_axi` 专有段属性，将配置改为独立示例，并同步调整相关日志和注释。引导报文中的 `CMU2`、`CMR2`、`cmu-` 仅为兼容格式标记，不包含真实产品身份或凭据。

### 加入已有工程

1. 加入 `cloud_client.c`、`cloud_port.c`、`cloud_bootstrap.c`、`cloud_config.c`，并将 `firmware/` 加入头文件搜索路径。
2. 编译 Mbed TLS 3.6.7 所需模块时统一定义 `MBEDTLS_CONFIG_FILE="cmu_mbedtls_config.h"`，不能让库和应用使用不同配置。其 `mbedtls_platform_gmtime_r`、`mbedtls_ms_time` 与分配器由 `cloud_port.c` 提供，避免重复实现。
3. 配置 lwIP：`NO_SYS=0`、`LWIP_IPV4=1`、`LWIP_TCP=1`、`LWIP_UDP=1`、`LWIP_SOCKET=1`、`LWIP_NETCONN=1`、`LWIP_SO_RCVTIMEO=1`、`LWIP_SO_SNDTIMEO=1`、`LWIP_NETIF_LOOPBACK=1`。必须验证从任务内访问 `127.0.0.1:8080` 能到达板端 Web。按自己的 sys_arch 实现提供 `errno`、`fcntl`、`select` 及 FreeRTOS 适配。
4. 让本机 Web 服务监听所有本机地址或能接收回环连接的地址；配置有效的板端 IP、网关与出站路由。PC 与板卡应处于不同 LAN，均可访问 ECS。
5. 按部署文档生成配置，复制 `cloud_demo_config.h` 到 `firmware/`。模板默认禁用且无可用凭据。`server` 当前只支持 IPv4 字面地址；服务器叶证书必须含对应 IP SAN。CA PEM 长度连同结尾零不得超过 2048 字节。
6. 在调度器已运行、网络和本机服务就绪后，仅调用一次 `cloudClientStart()`。状态可通过 `cloudClientShow()` 输出；默认日志关闭，需将 `CLOUD_LOG` 接入自己的线程安全日志。启动返回 true 也可能表示配置无效后安全禁用，应检查配置和在线状态。

建议先用 [配置生成工具](../tools/provision.py) 一次生成两端配置，再集成模块。默认设备号只是单设备示例；多板复用时应为每板分配身份、密钥和代理端口，并扩展当前只支持一个设备的服务器示例。

需要转发其他本机 TCP 服务时，可调整 `CLOUD_DEMO_LOCAL_PORT`；转发层按字节搬运数据，但现有 `.web` 代理名及 Nginx/curl 示例围绕 HTTP 编写，其他应用协议应使用相应客户端验证。

### 资源与协议限制

控制任务和每个工作任务的栈深度均为 2048 个 `StackType_t`（32 位平台约 8 KiB），最多三条工作连接加一条控制 TLS 连接。密码库动态分配上限 192 KiB，公共堆至少保留 48 KiB；这些不等于系统总 RAM 需求。lwIP PCB、Netconn、pbuf、RTOS 栈及 Web 服务仍需额外内存和实板测量。注意连续事件流会占用工作连接。

以上是当前代码的资源配置，不是“最低 192 KiB RAM”或“某个固定 Flash 容量即可运行”的结论。本项目尚无适用于各 GD32 型号的统一最低 RAM/Flash 实测值；选型时必须结合自己的 BSP、库裁剪、本机服务和并发连接预算，在板上检查剩余堆、任务栈和 TLS 握手峰值。

必须保留 `cloudRandom()` 的 GD32 TRNG 健康检测和失败处理；TRNG 时钟/驱动须匹配自己的芯片与外设库。TLS 证书有效期基于已认证 UTC，引导服务通过设备独立密钥验证 nonce、版本与时间/端口，不应通过关闭证书验证绕过时间问题。

FRP v1 消息头按 1 字节类型 + 8 字节大端长度解释，本实现限制正文小于 2048 字节，因此高 6 个长度字节必须为零。MD5、PBKDF2-HMAC-SHA1 和 AES-CFB 是锁定版本的协议兼容要求；工作流已有 TLS 保护，不额外启用代理 `use_encryption` 或压缩。服务端版本响应必须为 0.71.1。
