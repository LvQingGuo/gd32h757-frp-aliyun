# 来源、贡献与许可证

本项目主要贡献是 GD32H757VMT6 上的 FRP 客户端实现与平台适配、阿里云 ECS/frps 部署，以及电脑浏览器—云平台—实板之间跨局域网连接的集成验证。FRP 协议、官方 frps 以及第三方密码库/网络栈并非本项目原创。

### frp

[fatedier/frp](https://github.com/fatedier/frp) 提供协议与官方 Go 服务端/客户端。集成基线为 [v0.71.1](https://github.com/fatedier/frp/tree/v0.71.1)，上游采用 Apache-2.0。本仓库不包含 frps/frpc 二进制或 Go 源码；使用者自行获取并遵守其许可证。

协议参考：[消息定义](https://github.com/fatedier/frp/blob/v0.71.1/pkg/msg/msg.go)、[token 认证](https://github.com/fatedier/frp/blob/v0.71.1/pkg/auth/token.go)、[服务端配置](https://github.com/fatedier/frp/blob/v0.71.1/conf/frps_full_example.toml)。本仓库中的部署模板为该兼容子集整理。

### 其他依赖

| 组件 | 用途 | 上游许可证 |
| --- | --- | --- |
| [FreeRTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) | 任务与内存 | MIT；以实际取得版本的许可文本为准 |
| [lwIP 2.1.2](https://git.savannah.nongnu.org/cgit/lwip.git/tree/?h=STABLE-2_1_2_RELEASE) | TCP/IP、Socket、回环 | BSD 风格；保留上游文本 |
| [Mbed TLS 3.6.7](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.7) | TLS 与密码算法 | Apache-2.0 或 GPL-2.0-or-later；以该发行版文本为准 |
| GD32H7 CMSIS/标准外设库 | 芯片定义和 TRNG | 遵守实际 SDK 附带的厂商/Arm 许可 |

上述依赖的完整源码不分发。本仓库自有源码、脚本与文档采用 GPL-3.0-only，完整文本见 LICENSE。
