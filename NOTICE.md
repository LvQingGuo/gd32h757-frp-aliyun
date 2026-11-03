# 来源、贡献与许可证 / Provenance, contributions and licenses

## 中文

本项目由 QingGuo Lv（Qinn）整理并公开。主要贡献是 GD32H757VMT6 上的 FRP 客户端实现与平台适配、阿里云 ECS/frps 部署，以及电脑浏览器—云平台—实板之间跨局域网连接的集成验证。原文件中的 Qinn 作者信息予以保留。FRP 协议、官方 frps 以及第三方密码库/网络栈并非本项目原创。

### frp

[fatedier/frp](https://github.com/fatedier/frp) 提供协议与官方 Go 服务端/客户端。集成基线为 [v0.71.1](https://github.com/fatedier/frp/tree/v0.71.1)，上游采用 Apache-2.0。本仓库不包含 frps/frpc 二进制或 Go 源码；使用者自行获取并遵守其许可证。

协议参考：[消息定义](https://github.com/fatedier/frp/blob/v0.71.1/pkg/msg/msg.go)、[token 认证](https://github.com/fatedier/frp/blob/v0.71.1/pkg/auth/token.go)、[服务端配置](https://github.com/fatedier/frp/blob/v0.71.1/conf/frps_full_example.toml)。本仓库中的部署模板为该兼容子集整理。

### xfrpc：历史参考关系待核实

作者无法确认历史开发中是否参考或改写过 [liudf0716/xfrpc](https://github.com/liudf0716/xfrpc)。其控制实现使用 libevent/bufferevent 与 json-c，当前板端使用 FreeRTOS 任务、lwIP 和 mbedTLS，结构明显不同；这种差异本身不能证明从未参考或借用代码。因此，本项目不作“全部完全独立原创”的断言，也不在缺乏证据时将整个实现认定为 xfrpc 的直接移植。

2026-11-03 核对的上游提交为 `e87e06ed1d0b7d7579dc9044a4ea1401591780c7`，相关文件可查看 [crypto.c](https://github.com/liudf0716/xfrpc/blob/e87e06ed1d0b7d7579dc9044a4ea1401591780c7/crypto.c)、[msg.c](https://github.com/liudf0716/xfrpc/blob/e87e06ed1d0b7d7579dc9044a4ea1401591780c7/msg.c)、[login.c](https://github.com/liudf0716/xfrpc/blob/e87e06ed1d0b7d7579dc9044a4ea1401591780c7/login.c)。这些文件注明 GPL-3.0-only 和 Dengfeng Liu 的版权；本次整理未将它们复制进仓库。如后续找到实际借用片段或历史记录，应补充相应来源、原作者和版权说明。

### 其他依赖

| 组件 | 用途 | 上游许可证 |
| --- | --- | --- |
| [FreeRTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | 任务与内存 | MIT；以实际取得版本的许可文本为准 |
| [lwIP 2.1.2](https://git.savannah.nongnu.org/cgit/lwip.git/tree/?h=STABLE-2_1_2_RELEASE) | TCP/IP、Socket、回环 | BSD 风格；保留上游文本 |
| [Mbed TLS 3.6.7](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.7) | TLS 与密码算法 | Apache-2.0 或 GPL-2.0-or-later；以该发行版文本为准 |
| GD32H7 CMSIS/标准外设库 | 芯片定义和 TRNG | 遵守实际 SDK 附带的厂商/Arm 许可 |

上述依赖的完整源码不随首版分发。本仓库自有源码、脚本与文档采用 GPL-3.0-only，完整文本见 LICENSE。该选择不表示已经证实或排除了 xfrpc 衍生关系；历史来源仍需如实补充。

## English

Prepared and published by QingGuo Lv (Qinn). The contributions are the FRP client and platform adaptation on GD32H757VMT6, Alibaba Cloud ECS/frps deployment, and cross-LAN integration testing between a PC browser, cloud platform and real board. Existing Qinn author fields are retained. FRP, official frps, and third-party cryptographic/network components are not original work of this project.

### frp

[fatedier/frp](https://github.com/fatedier/frp) provides the protocol and official Go server/client. The integration baseline is [v0.71.1](https://github.com/fatedier/frp/tree/v0.71.1), licensed upstream under Apache-2.0. This repository does not redistribute its binaries or Go sources; obtain them separately and follow their license.

Protocol references are the [message definitions](https://github.com/fatedier/frp/blob/v0.71.1/pkg/msg/msg.go), [token authentication](https://github.com/fatedier/frp/blob/v0.71.1/pkg/auth/token.go), and [server configuration](https://github.com/fatedier/frp/blob/v0.71.1/conf/frps_full_example.toml). Deployment templates here describe the implemented subset.

### xfrpc: historical relationship remains unconfirmed

The author cannot confirm whether [liudf0716/xfrpc](https://github.com/liudf0716/xfrpc) was historically consulted or adapted. Its control implementation uses libevent/bufferevent and json-c; this board client uses FreeRTOS tasks, lwIP and mbedTLS. The architectures differ, but that alone cannot establish that no code was ever referenced or reused. This project therefore does not claim complete independent originality, nor classify the entire implementation as a direct xfrpc port without evidence.

The upstream revision inspected on 2026-11-03 is `e87e06ed1d0b7d7579dc9044a4ea1401591780c7`; see [crypto.c](https://github.com/liudf0716/xfrpc/blob/e87e06ed1d0b7d7579dc9044a4ea1401591780c7/crypto.c), [msg.c](https://github.com/liudf0716/xfrpc/blob/e87e06ed1d0b7d7579dc9044a4ea1401591780c7/msg.c), and [login.c](https://github.com/liudf0716/xfrpc/blob/e87e06ed1d0b7d7579dc9044a4ea1401591780c7/login.c). These identify GPL-3.0-only and Dengfeng Liu's copyright. They were not copied into this repository during extraction. If actual reused sections or historical records are found, add the appropriate provenance, original author and copyright notices.

### Other dependencies

| Component | Use | Upstream license |
| --- | --- | --- |
| [FreeRTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | Tasks and allocation | MIT; check the actual version's license |
| [lwIP 2.1.2](https://git.savannah.nongnu.org/cgit/lwip.git/tree/?h=STABLE-2_1_2_RELEASE) | TCP/IP, sockets and loopback | BSD-style; retain the upstream text |
| [Mbed TLS 3.6.7](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.7) | TLS and cryptography | Apache-2.0 or GPL-2.0-or-later; check that release's texts |
| GD32H7 CMSIS/standard peripheral library | Chip definitions and TRNG | Vendor/Arm terms supplied with the actual SDK |

Complete dependency sources are not bundled. First-party source, scripts and documentation are GPL-3.0-only; see LICENSE. This choice neither proves nor rules out an xfrpc-derived relationship. Historical provenance must remain accurate as evidence becomes available.
