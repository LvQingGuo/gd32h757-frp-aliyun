# 验证记录与边界 / Validation and scope

## 中文

### 原系统已有的验证

项目作者已在不同局域网间完成以下链路：电脑浏览器通过 HTTPS 进入阿里云 ECS 承载的云平台，云平台经 frps 回环 TCP 代理、板端主动建立的 FRP/TLS 工作连接，访问 GD32H757VMT6 的本机 Web 服务并取得响应。这是本项目要展示的工程成果。

本地保留的原系统记录包含 2026-10-23 云端板 Web/API/持续事件流联调及 2026-11-03 代理端口相关验收。公开说明只摘录与 FRP 连通有关的事实；原日志、地址、设备 UID、页面数据和截图不分发。跨 LAN 的网络拓扑由作者确认，公开包不包含可供第三方独立审计的原始网络抓包或脱敏截图。

### 2026-11-03 提取包检查

| 检查 | 结果与边界 |
| --- | --- |
| ECS 只读核验 | Ubuntu 22.04.5 LTS x86_64；frps 0.71.1、Nginx、原云平台服务 active；TLS 强制开启、tcpMux=false、回环代理 |
| 当前板端回环 HTTP 探测 | 两个已监听的代理均未取得可用响应；本次不确认当前板卡在线或端到端链路可用，不以历史结果替代当前验收 |
| ARM 编译检查 | 四个公开 `.c` 文件使用 Arm Compiler 6.21、Cortex-M7 目标与现有 SDK 头文件通过 `-fsyntax-only -Wall -Wextra -Werror`；未完整链接或生成公开固件 |
| Python 主机回归 | 8 项测试通过，覆盖请求/回包认证、错误密钥/身份、时间范围、占位符拒绝、真实回环 UDP、配置一致性、禁止覆盖与敏感信息检测 |
| C/Python 互通 | 真实 `cloud_bootstrap.c` 与 Mbed TLS 3.6.7 编译运行；Python 接受 C 请求，C 接受 Python 回包，68 个回包逐字节篡改、挑战重放和错误长度均拒绝 |
| 公开文件检查 | 通过白名单发布；排除凭据、真实 IP/UID、原证书、私钥、数据库、固件镜像、截图及原 Git 历史；检查只报告路径/行号 |

此次没有重启/修改 ECS 服务，没有修改或烧录原固件，没有进行真实业务控制。独立配置层和最小配套服务是为本次开源整理的示例，尚未在真实 GD32H757VMT6 上重新完成完整验证。主机回归和语法检查不证明 TRNG 电气行为、实板内存峰值、断网恢复或浏览器完整交互已经在独立包中复现。

### 如何复现跨局域网验证

1. 在板端 LAN B 先确认本机 Web 可用、网关正确；在 LAN A 准备电脑浏览器。记录两个网络使用不同出口，记录留在自己的私有验收资料中。
2. 按部署文档运行自己的 ECS/frps、认证引导和 HTTPS 入口。检查固件版本响应、证书/IP SAN 与凭据一致。
3. 通过串口日志确认认证对时成功、TLS 建连、FRP 登录及代理在线；在 ECS 用 `curl --fail http://127.0.0.1:18080/` 检查板端响应。
4. 在 LAN A 浏览器访问自己的 HTTPS 域名并登录，验证返回的是板端实际页面/只读数据；不要仅以云端登录页可打开作为板端贯通证据。
5. 如需验证恢复，单独安排板端断网/恢复并记录重连及本地服务状态；不要对运行中的业务设备随意做破坏性测试。

主机复核命令：

```sh
python -m unittest discover -s tests -v
python tests/run_interop.py --cc gcc --mbedtls /path/to/mbedtls-3.6.7
python tools/check_public.py
```

性能、长期稳定性、设备容量、并发吞吐、完整官方 frpc 功能兼容和生产安全性均不从这组连接测试推导。

## English

### Existing original-system evidence

The author successfully tested the following path across different LANs: a PC browser reached the cloud platform hosted on Alibaba Cloud ECS through HTTPS; the platform used a loopback frps TCP proxy and a board-initiated FRP/TLS work connection to access the GD32H757VMT6 local Web service and receive responses. This is the engineering result highlighted by the project.

Private local records include cloud board Web/API/event-stream integration on 2026-10-23 and proxy-port acceptance on 2026-11-03. Only FRP connectivity facts are summarized publicly; original logs, addresses, device UIDs, page data and screenshots are excluded. The cross-LAN topology is confirmed by the author; this package does not include raw packet captures or redacted screenshots for independent third-party auditing.

### Extracted-package checks on 2026-11-03

| Check | Result and boundary |
| --- | --- |
| Read-only ECS inventory | Ubuntu 22.04.5 LTS x86_64; frps 0.71.1, Nginx and the original cloud platform active; forced TLS, tcpMux=false, loopback proxies |
| Current board loopback HTTP probe | Neither of two listening proxies returned a usable response; this check does not establish current board or end-to-end availability, and historical results do not substitute for present acceptance |
| ARM compilation check | All four published C files passed Arm Compiler 6.21 Cortex-M7 `-fsyntax-only -Wall -Wextra -Werror` with existing SDK headers; no complete link or public firmware image was produced |
| Python host regression | Eight tests passed, covering authentication, wrong keys/identity, time bounds, placeholder rejection, real loopback UDP, provisioning consistency, overwrite prevention and sensitive-data detection |
| C/Python interop | Real `cloud_bootstrap.c` compiled with Mbed TLS 3.6.7; Python accepted C requests and C accepted Python replies; all 68 single-byte reply mutations, nonce replay and wrong length were rejected |
| Public-file inspection | Allowlist publication excludes credentials, real IPs/UIDs, original certificates, keys, databases, firmware images, screenshots and original Git history; findings report only paths/lines |

No ECS service was restarted or modified; no original firmware was changed or flashed; no business control commands were issued. The standalone configuration layer and minimal companion are examples prepared for publication and have not completed fresh end-to-end validation on a real GD32H757VMT6. Host regression and syntax checks do not establish hardware TRNG behavior, board peak memory, network recovery or complete browser interaction for this extracted package.

### Reproducing cross-LAN validation

1. Confirm local board HTTP and a valid gateway on LAN B. Prepare a browser on LAN A. Verify that the networks use different egress paths; retain those records privately.
2. Deploy your own ECS/frps, authenticated bootstrap and HTTPS entry. Check version, certificate/IP SAN and matching credentials.
3. Use serial logs to confirm authenticated time, TLS, FRP login and proxy registration. On ECS, run `curl --fail http://127.0.0.1:18080/` to check the board response.
4. From LAN A, open your HTTPS domain and authenticate. Verify actual board pages/read-only data. A reachable cloud login page alone does not establish board connectivity.
5. If recovery testing is needed, schedule board network disconnection/recovery separately and record reconnect/local-service state. Avoid disruptive tests on operating business equipment.

Host verification commands:

```sh
python -m unittest discover -s tests -v
python tests/run_interop.py --cc gcc --mbedtls /path/to/mbedtls-3.6.7
python tools/check_public.py
```

These connection tests do not establish performance, long-term stability, device capacity, concurrent throughput, complete official frpc compatibility or production security.
