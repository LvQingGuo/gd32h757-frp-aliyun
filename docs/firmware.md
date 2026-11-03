# 固件集成 / Firmware integration

## 中文

### 平台与文件

实际目标为 **GD32H757VMT6 / Cortex-M7**。原工作目录中的 H759 字样不代表本项目实测芯片。原工程使用 Keil MDK Arm Compiler 6.21；本仓库不包含其私有工程文件、启动代码、ENET/PHY 驱动或完整 BSP。

外部依赖：FreeRTOS、lwIP 2.1.2、Mbed TLS 3.6.7，以及 GD32H7 CMSIS/标准外设库。使用者应从各自官方来源取得依赖，保留许可文本。版本为已应用系统的集成基线，不代表最新版本或任意版本兼容承诺。

| 文件 | 职责 |
| --- | --- |
| `cloud_client.c/.h` | FRP v1 帧、登录、代理注册、心跳、工作任务和退避重连 |
| `cloud_port.c/.h` | GD32 TRNG、FreeRTOS 内存与时间、lwIP Socket、TLS |
| `cloud_bootstrap.c/.h` | 固定长度 HMAC-SHA256 认证引导请求/回包 |
| `cmu_mbedtls_config.h` | TLS 1.2/P-256/ECDHE-ECDSA/AES-GCM 配置及 FRP v1 兼容算法 |
| `cloud_config.c/.h` | 首版独立示例的人工配置加载；替代原产品 UID/根密钥流程 |
| `cloud_log.h` | 可覆盖的线程安全日志入口，默认不输出 |

提取时保留核心协议与 TLS 实现；将私有日志依赖替换为 `cloud_log.h`，移除 `.bss.cloud_axi` 专有段属性，将配置改为独立示例，并同步调整相关日志和注释。原文件作者 Qinn 保留。引导报文中的 `CMU2`、`CMR2`、`cmu-` 仅为兼容格式标记，不包含真实产品身份或凭据。

### 加入已有工程

1. 加入 `cloud_client.c`、`cloud_port.c`、`cloud_bootstrap.c`、`cloud_config.c`，并将 `firmware/` 加入头文件搜索路径。
2. 编译 Mbed TLS 3.6.7 所需模块时统一定义 `MBEDTLS_CONFIG_FILE="cmu_mbedtls_config.h"`，不能让库和应用使用不同配置。其 `mbedtls_platform_gmtime_r`、`mbedtls_ms_time` 与分配器由 `cloud_port.c` 提供，避免重复实现。
3. 配置 lwIP：`NO_SYS=0`、`LWIP_IPV4=1`、`LWIP_TCP=1`、`LWIP_UDP=1`、`LWIP_SOCKET=1`、`LWIP_NETCONN=1`、`LWIP_SO_RCVTIMEO=1`、`LWIP_SO_SNDTIMEO=1`、`LWIP_NETIF_LOOPBACK=1`。必须验证从任务内访问 `127.0.0.1:8080` 能到达板端 Web。按自己的 sys_arch 实现提供 `errno`、`fcntl`、`select` 及 FreeRTOS 适配。
4. 让本机 Web 服务监听所有本机地址或能接收回环连接的地址；配置有效的板端 IP、网关与出站路由。PC 与板卡应处于不同 LAN，均可访问 ECS。
5. 按部署文档生成配置，复制 `cloud_demo_config.h` 到 `firmware/`。模板默认禁用且无可用凭据。`server` 当前只支持 IPv4 字面地址；服务器叶证书必须含对应 IP SAN。CA PEM 长度连同结尾零不得超过 2048 字节。
6. 在调度器已运行、网络和本机服务就绪后，仅调用一次 `cloudClientStart()`。状态可通过 `cloudClientShow()` 输出；默认日志关闭，需将 `CLOUD_LOG` 接入自己的线程安全日志。启动返回 true 也可能表示配置无效后安全禁用，应检查配置和在线状态。

### 资源与协议限制

控制任务和每个工作任务的栈深度均为 2048 个 `StackType_t`（32 位平台约 8 KiB），最多三条工作连接加一条控制 TLS 连接。密码库动态分配上限 192 KiB，公共堆至少保留 48 KiB；这些不等于系统总 RAM 需求。lwIP PCB、Netconn、pbuf、RTOS 栈及 Web 服务仍需额外内存和实板测量。注意连续事件流会占用工作连接。

必须保留 `cloudRandom()` 的 GD32 TRNG 健康检测和失败处理；TRNG 时钟/驱动须匹配自己的芯片与外设库。TLS 证书有效期基于已认证 UTC，引导服务通过设备独立密钥验证 nonce、版本与时间/端口，不应通过关闭证书验证绕过时间问题。

FRP v1 消息头按 1 字节类型 + 8 字节大端长度解释，本实现限制正文小于 2048 字节，因此高 6 个长度字节必须为零。MD5、PBKDF2-HMAC-SHA1 和 AES-CFB 是锁定版本的协议兼容要求；工作流已有 TLS 保护，不额外启用代理 `use_encryption` 或压缩。服务端版本响应必须为 0.71.1。

## English

### Platform and files

The actual target is **GD32H757VMT6 / Cortex-M7**. H759 in the original working directory does not identify the tested chip. The original firmware uses Keil MDK Arm Compiler 6.21. Private project files, startup code, ENET/PHY drivers and the full BSP are not included.

External dependencies are FreeRTOS, lwIP 2.1.2, Mbed TLS 3.6.7 and the GD32H7 CMSIS/standard peripheral library. Obtain these from their official sources and retain their licenses. Versions describe the applied system's integration baseline, not the latest release or compatibility with arbitrary versions.

| File | Responsibility |
| --- | --- |
| `cloud_client.c/.h` | FRP v1 framing, login, proxy registration, heartbeats, workers and reconnect backoff |
| `cloud_port.c/.h` | GD32 TRNG, FreeRTOS allocation/time, lwIP sockets and TLS |
| `cloud_bootstrap.c/.h` | Fixed-length HMAC-SHA256 bootstrap requests and replies |
| `cmu_mbedtls_config.h` | TLS 1.2/P-256/ECDHE-ECDSA/AES-GCM and FRP v1 compatibility algorithms |
| `cloud_config.c/.h` | Manual standalone configuration, replacing private UID/root-key provisioning |
| `cloud_log.h` | Overridable thread-safe logging entry, silent by default |

Extraction preserves the core protocol and TLS implementation. Private logging includes are replaced by `cloud_log.h`; the private `.bss.cloud_axi` placement attribute is removed; configuration is replaced with a standalone example, with corresponding log/comment adjustments. Existing author Qinn is retained. `CMU2`, `CMR2` and `cmu-` are wire-format markers, not live product identities or credentials.

### Integrating into an existing project

1. Add `cloud_client.c`, `cloud_port.c`, `cloud_bootstrap.c` and `cloud_config.c`; include `firmware/` in the header search path.
2. Build the required Mbed TLS 3.6.7 modules with `MBEDTLS_CONFIG_FILE="cmu_mbedtls_config.h"` consistently across the library and application. `cloud_port.c` supplies `mbedtls_platform_gmtime_r`, `mbedtls_ms_time` and allocation callbacks; avoid duplicate implementations.
3. Configure lwIP with `NO_SYS=0`, `LWIP_IPV4=1`, `LWIP_TCP=1`, `LWIP_UDP=1`, `LWIP_SOCKET=1`, `LWIP_NETCONN=1`, `LWIP_SO_RCVTIMEO=1`, `LWIP_SO_SNDTIMEO=1` and `LWIP_NETIF_LOOPBACK=1`. Verify that a task can reach the board Web service at `127.0.0.1:8080`. Supply `errno`, `fcntl`, `select` and the FreeRTOS integration appropriate to your sys_arch port.
4. Bind the local Web service to all local addresses or an address accepting loopback connections. Configure a valid board IP, gateway and outbound route. The PC and board should be on different LANs, both able to reach ECS.
5. Generate configuration following the deployment guide and copy `cloud_demo_config.h` into `firmware/`. The template is disabled and has no usable credentials. `server` currently accepts an IPv4 literal only; the leaf certificate needs the matching IP SAN. The CA PEM including its terminator must fit in 2048 bytes.
6. Call `cloudClientStart()` once, from task context after the scheduler, network and local service are ready. `cloudClientShow()` reports status. Connect `CLOUD_LOG` to your thread-safe logger to enable output. A true startup result may also mean invalid configuration safely disabled the tunnel; check configuration and actual online status.

### Resource and protocol limits

The control task and each worker request 2048 `StackType_t` entries, approximately 8 KiB each on a 32-bit target. Up to three workers and one control TLS connection may exist. Crypto allocation is capped at 192 KiB with 48 KiB reserved in the shared heap. These are not total system RAM figures: lwIP PCBs, Netconns, pbufs, RTOS stacks and the Web service need additional budgeting and hardware measurements. Event streams can occupy workers continuously.

Retain GD32 TRNG health checks and failure handling in `cloudRandom()`. Match clocks and peripheral drivers to your hardware. TLS validity checks use authenticated UTC; the bootstrap verifies a device-specific key, nonce, version and time/port. Do not bypass time problems by disabling certificate verification.

FRP v1 framing is one type byte plus an eight-byte big-endian length. This implementation restricts bodies to less than 2048 bytes, requiring the upper six length bytes to be zero. MD5, PBKDF2-HMAC-SHA1 and AES-CFB are compatibility requirements of the pinned protocol. Work streams already use TLS; proxy `use_encryption` and compression are disabled. The server response version must be 0.71.1.
