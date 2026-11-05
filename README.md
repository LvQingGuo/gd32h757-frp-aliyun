# GD32H757 FRP · Alibaba Cloud ECS

[平台适配与板端前提](docs/firmware.md) · [ECS 部署与服务器前提](docs/deployment.md) · [验证步骤](docs/validation.md) · [来源与许可证](NOTICE.md)

**将 FRP v1 协议适配到 GD32H757VMT6，在阿里云 ECS 部署 frps，实现电脑浏览器跨局域网访问远程 MCU 板卡。**

本项目重点是嵌入式协议实现、云端部署和跨网络联调。原系统已经完成电脑浏览器 → 阿里云云平台 → GD32H757VMT6 的端到端连接；板卡主动建立出站隧道，无需给板卡所在局域网配置公网入口。

开源的另一个目标是帮助开发者在 GD32 上实现类似 frpc 的客户端，并与阿里云 ECS 或其他服务器上的 frps 建立连接。你可以复用这里的协议代码、平台接口和配置模板，在已有的联网 GD32 工程中完成集成；独立示例使用人工配置的设备身份与最小配套服务即可运行。

*A C FRP client adaptation for GD32H757, with server configuration and integration guides for Alibaba Cloud ECS or other Linux servers. Documentation is primarily in Chinese.*

<p align="center">
  <img
    src="https://github.com/user-attachments/assets/92ac51b3-35ca-4997-a0b9-95c69b8e1dca"
    alt="云端边缘网络架构图"
    width="100%"
  />
</p>

## 主要贡献

- 在 GD32H757VMT6 上实现受限资源环境的 C 语言 FRP 客户端：登录、TCP 代理注册、认证心跳、独立工作连接和双向转发。
- 适配 FreeRTOS 任务、lwIP Socket、GD32 TRNG 和 mbedTLS，保留证书链、目标身份及有效期验证。
- 完成阿里云 ECS 上 frps、HTTPS 入口与云端平台的部署、配置和联调。
- 在不同局域网之间完成浏览器、云平台和真实板卡的连通验证。

FRP 协议及 frps 来自 [fatedier/frp](https://github.com/fatedier/frp)。本项目并非对 Go 版官方 frpc 的直接移植，公开的板端实现使用 FreeRTOS/lwIP/mbedTLS。

## 公开内容

| 目录 | 内容 |
| --- | --- |
| `firmware/` | 从已应用的板端模块提取的 FRP/TLS/硬件熵/认证对时实现；独立配置与日志接口 |
| `deploy/` | frps、systemd、Nginx 配置模板；单设备认证对时和端口示例 |
| `tools/` | 本地配置生成与公开文件检查 |
| `tests/` | 最小配套服务与固件认证报文的主机测试 |
| `docs/` | 完整平台适配、ECS 配置和验证说明 |

## 最低准备条件

以下是按本仓库当前代码复现的功能前提；硬件和软件版本基线详见两个集成文档。

| 位置 | 必须准备 | 在本实现中的用途 |
| --- | --- | --- |
| GD32 板端 | 已能启动并联网的 BSP、网络接口驱动和有效 IPv4/网关 | 能向服务器发起 TCP/UDP 出站连接 |
| GD32 板端 | FreeRTOS、lwIP Socket/回环、Mbed TLS、可靠硬件随机源及足够的 RAM/Flash | 任务调度、TLS、协议处理、工作连接转发；当前硬件端口针对 GD32H7 |
| GD32 板端 | 可从 `127.0.0.1` 访问的本机 TCP 服务 | 示例为 HTTP `8080`；完整验证转发时需要该服务 |
| 服务器 | 可被板端访问的 IPv4、兼容的 frps **0.71.1**、服务器时钟、P-256 ECDSA 证书及私钥 | 运行 FRP/TLS 服务；固件内配置对应 CA 公钥证书与服务器 IPv4 |
| 服务器 | Python 3.8+ 与 `deploy/bootstrap_demo.py`，放行 TCP **7000**、UDP **7000** | 当前客户端必须先取得认证 UTC 和代理端口，再连接 frps |
| 两端共同 | 一致的 FRP token，以及匹配的设备 ID/独立设备密钥 | 两套凭据分别用于 FRP 认证和自定义引导认证 |
| 浏览器访问（扩展） | Nginx、HTTPS 证书和入口认证，放行 TCP **443** | 将浏览器请求转到 ECS 上的回环代理 `127.0.0.1:18080` |

**仅安装 frps 不足以按当前实现建连：认证引导服务也是必需组件。** UDP 7000 是本项目的认证引导端口，不是 frps 的 KCP/QUIC 端口。仅验证板端与 frps 的连接及服务器回环转发时，无需部署 Nginx、域名或原云平台；公开浏览器入口再增加 HTTPS 层。

服务器侧不依赖阿里云专有 SDK；其他 Linux 云服务器也可按相同协议和端口配置，选择匹配 CPU 架构的 frps 包。板端已验证平台为 **GD32H757VMT6**；其他 GD32 型号需要移植 `cloud_port.c` 中的随机数/时钟等硬件接口，重新评估内存并验证，不能直接视为全系列适用。

## 兼容范围与快速开始

当前客户端严格检查 **frps 0.71.1** 的版本响应，使用 **FRP v1 + TCP 代理 + TLS 1.2**。服务端必须设置 `transport.tcpMux = false`，并启用 `HeartBeats`、`NewWorkConns` token 认证。控制流登录后使用 FRP v1 所需的 PBKDF2/AES-CFB；TLS 使用 ECDHE-ECDSA/AES-128-GCM。最多三条工作连接。

1. 先按 [板端前提](docs/firmware.md) 准备已有 GD32 联网工程，验证本机 HTTP 服务和回环访问。
2. 按 [ECS 部署文档](docs/deployment.md) 准备自己的 IPv4、证书、frps、认证引导服务和安全组。
3. 在本地仓库运行 `python tools/provision.py --server 192.0.2.10 --ca /path/to/your/ca.crt`，将示例地址替换为自己的 ECS 公网 IPv4。一次生成匹配的三份配置：板端 `cloud_demo_config.h`、服务端 `frps.toml` 和 `bootstrap.json`，结果在忽略目录 `private/`，包含全新凭据。
4. 将生成的头文件放入 `firmware/`，将另外两份配置部署到 ECS；按 [固件集成文档](docs/firmware.md) 加入四个 C 文件，在网络和本机服务就绪后调用一次 `cloudClientStart()`。
5. 先在 ECS 通过 `curl --fail http://127.0.0.1:18080/` 验证回环转发；成功后如需浏览器访问，再部署 Nginx HTTPS 入口，从另一局域网按 [验证说明](docs/validation.md) 验收。

`192.0.2.10`、`board.example.com` 均为文档占位符，复现时使用自己的地址、证书与新生成凭据。

## 验证边界

本仓库不是可直接烧录的完整 BSP/固件工程。

首版自有代码采用 **GPL-3.0-only**；第三方依赖分别遵守各自许可证，见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE.md)。
