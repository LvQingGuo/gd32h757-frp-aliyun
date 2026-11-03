# 阿里云 ECS 部署 / Alibaba Cloud ECS deployment

## 中文

### 已验证的资源基线

| 项目 | 配置 |
| --- | --- |
| 云资源 | 阿里云 ECS，西南 1（成都） |
| 系统 | Ubuntu 22.04.5 LTS，x86_64 |
| CPU / 内存 | 2 vCPU / 2 GiB |
| 系统盘 | 40 GiB 云盘 |
| 公网 | 可达 IPv4；截图中的带宽峰值为 100 Mbps |
| 服务 | 官方 frps 0.71.1、Nginx HTTPS、独立云平台 |

以上为原系统的部署环境，不是最低资源要求、费用报价或吞吐能力结论。复现可使用自己的地域与资源规格。公网 IPv4 改变后，必须同步更新固件地址及证书 IP SAN；本示例不实现 DNS 解析。

### 网络与安全组

| 方向/协议 | 用途 | 规则 |
| --- | --- | --- |
| 入站 TCP 443 | 浏览器 HTTPS | 按访问需求允许来源 |
| 入站 TCP 7000 | 板端 TLS/FRP | 允许板卡出口；出口变化时调整来源 |
| 入站 UDP 7000 | 自定义认证对时/端口引导 | 允许板卡出口；不是 FRP 的 KCP/QUIC |
| 入站 TCP 22 | 自行管理 ECS | 仅管理来源，或使用 Workbench |
| 服务器回环 TCP 18080 | frps 到 Nginx 的板端代理 | 不开放公网入站 |

同时检查 ECS 安全组、主机防火墙与板端出站路由。浏览器无法直接连接 MCU；请求通过 ECS 的 HTTPS 和回环代理进入 FRP 工作流。原云平台支持动态分配端口；公开示例只预配置单设备的 18080，不复刻账户管理或设备登记业务。

### 准备 frps 与证书

从 [frp v0.71.1 官方发布页](https://github.com/fatedier/frp/releases/tag/v0.71.1) 下载 Linux amd64 包，并核对该发布的 SHA-256 校验值。将 `frps` 安装为 `/opt/frp/frps`，保留官方 LICENSE。不要将本地现有生产配置或二进制固件直接上传到仓库。

TLS 固件仅启用 ECDHE-ECDSA/P-256，请使用 P-256 EC 叶证书。以下是全新测试 PKI 的离线生成方式，在自己的私有目录执行；将示例 IPv4 替换成自己的地址：

```sh
umask 077
mkdir -p ~/gd32-frp-pki
cd ~/gd32-frp-pki
FRP_SERVER_IPV4=192.0.2.10
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -new -x509 -sha256 -key ca.key -days 3650 -out ca.crt -subj '/CN=GD32 Demo CA' -addext 'basicConstraints=critical,CA:TRUE' -addext 'keyUsage=critical,keyCertSign,cRLSign'
openssl ecparam -name prime256v1 -genkey -noout -out server.key
openssl req -new -sha256 -key server.key -out server.csr -subj '/CN=GD32 FRP Demo'
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\nsubjectAltName=IP:%s\n' "$FRP_SERVER_IPV4" > server.ext
openssl x509 -req -sha256 -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 365 -extfile server.ext -out server.crt
```

CA 签发私钥 `ca.key` 应离线保管；只将 `server.crt`、`server.key` 安装到 ECS 的 `/etc/frp/tls/`。CA 公钥证书提供给固件，不能把 CA 私钥放入固件、ECS 运行配置或 GitHub。浏览器入口可使用独立的受信任域名证书；示例域名应解析到自己的 ECS。

### 生成全新配置并启动服务

在仓库本地副本运行（Python 3.8+，使用者自行安装运行依赖）：

```sh
python tools/provision.py --server 192.0.2.10 --ca /path/to/ca.crt
```

此工具产生互不相同的随机 FRP token 与设备密钥，只保存在忽略目录 `private/`。将 `private/cloud_demo_config.h` 复制到 `firmware/`；`private/frps.toml` 和 `private/bootstrap.json` 分别部署为 ECS 的 `/etc/frp/frps.toml`、`/etc/frp/bootstrap.json`。引导密钥按 64 字符 ASCII 文本参与 HMAC，不能先做 hex 解码。

在 ECS 创建独立的无登录权限 `frps` 系统用户，将配置、叶证书和私钥设为 root 所有、frps 组可读（目录 0750、文件 0640）。安装 `deploy/bootstrap_demo.py` 为 `/opt/gd32-frp/bootstrap_demo.py`；安装两个 `.service` 到 `/etc/systemd/system/`。自行确认 Python 3 可用。

```sh
sudo /opt/frp/frps verify -c /etc/frp/frps.toml
sudo systemctl daemon-reload
sudo systemctl enable --now frps bootstrap-demo
sudo systemctl status frps bootstrap-demo --no-pager
```

示例引导服务仅接受配置的一个设备，校验固定长度、版本、设备号、保留字段和 HMAC，再返回带挑战认证的 UTC/端口。它不是原云平台，也不是 FRP 官方服务；可用自己的安全认证时钟与端口分配实现替换。

### HTTPS 浏览器入口

将 `deploy/nginx.example.conf` 作为 Nginx `http {}` 下的配置，改成自己的域名和浏览器证书路径。用 `htpasswd -c /etc/nginx/gd32-demo.htpasswd demo` 交互设置新的入口密码，保持密码文件权限；不要复用板端或生产平台密码。

```sh
sudo nginx -t
sudo systemctl reload nginx
```

Nginx 将已认证的 HTTPS 请求转到 `127.0.0.1:18080`，frps 再经 TLS 工作连接到板端 Web。此入口只展示 FRP 连通链路，未复刻原平台的账户管理/授权界面。板端服务如需自己的登录，应保留。示例最多三条工作连接，持续流或并发页面可能占满，需按实际负载验证。

### 排查顺序

先验证板端本机 Web，再检查路由、安全组、UDP 引导、认证 UTC、TLS 证书/IP SAN、FRP 登录、代理注册和回环 HTTP，最后测试跨 LAN 浏览器。确认 0.71.1、tcpMux=false、两个认证 scope、相同 FRP token 及独立设备密钥。检查 `journalctl -u frps -u bootstrap-demo`，避免把带凭据的配置或业务响应粘贴到公开 issue。

## English

### Verified resource baseline

| Item | Configuration |
| --- | --- |
| Cloud resource | Alibaba Cloud ECS, Southwest 1 (Chengdu) |
| OS | Ubuntu 22.04.5 LTS, x86_64 |
| CPU / RAM | 2 vCPU / 2 GiB |
| System disk | 40 GiB cloud disk |
| Public networking | Reachable IPv4; the source screenshot shows a 100 Mbps peak bandwidth setting |
| Services | Official frps 0.71.1, Nginx HTTPS and a separate cloud platform |

This describes the original deployment, not minimum requirements, pricing or measured throughput. Use your own region and instance size. If the public IPv4 changes, update both firmware configuration and the certificate IP SAN. This example does not implement DNS resolution.

### Networking and security groups

| Direction / protocol | Purpose | Rule |
| --- | --- | --- |
| Inbound TCP 443 | Browser HTTPS | Allow intended visitors |
| Inbound TCP 7000 | Board TLS/FRP | Allow the board's egress address; update if it changes |
| Inbound UDP 7000 | Custom authenticated clock/port bootstrap | Allow board egress; this is not FRP KCP/QUIC |
| Inbound TCP 22 | ECS administration | Restrict to management sources, or use Workbench |
| Loopback TCP 18080 | Board proxy between frps and Nginx | No public inbound rule |

Check the ECS security group, host firewall and board outbound route. The browser does not connect directly to the MCU: HTTPS and the loopback proxy on ECS feed the FRP work stream. The original platform dynamically assigns ports; this public example preconfigures port 18080 for one device without reproducing account or device registry logic.

### frps and certificates

Download Linux amd64 frps from the [official v0.71.1 release](https://github.com/fatedier/frp/releases/tag/v0.71.1), verify its published SHA-256 checksum, and install it as `/opt/frp/frps`. Retain its official LICENSE. Do not upload existing production configuration or firmware binaries.

The firmware enables ECDHE-ECDSA/P-256 only, so provision a P-256 EC leaf certificate. The shell block in the Chinese section above creates a fresh test PKI in a private directory: replace `192.0.2.10` with your ECS address, generate EC CA/server keys, and sign a leaf with `serverAuth` and the matching IP SAN. The commands are shared by both language versions.

Keep the CA signing key `ca.key` offline. Install only `server.crt` and `server.key` under `/etc/frp/tls/` on ECS. The firmware receives the public CA certificate; never put the CA private key in firmware, runtime configuration or GitHub. The browser entry can use a separate trusted domain certificate, with your domain resolving to your ECS.

### Fresh configuration and services

In a local checkout, with Python 3.8+ and dependencies installed by the operator, run:

```sh
python tools/provision.py --server 192.0.2.10 --ca /path/to/ca.crt
```

This creates distinct random FRP and device secrets under ignored `private/`. Copy `private/cloud_demo_config.h` to `firmware/`. Deploy `private/frps.toml` and `private/bootstrap.json` as `/etc/frp/frps.toml` and `/etc/frp/bootstrap.json` on ECS. The bootstrap HMAC key is the 64-character ASCII secret, not hex-decoded bytes.

Create an unprivileged `frps` system user without login access. Keep configuration and TLS files owned by root and readable by group frps: directories 0750, files 0640. Install `deploy/bootstrap_demo.py` as `/opt/gd32-frp/bootstrap_demo.py`, and both service files under `/etc/systemd/system/`. Ensure Python 3 is installed.

```sh
sudo /opt/frp/frps verify -c /etc/frp/frps.toml
sudo systemctl daemon-reload
sudo systemctl enable --now frps bootstrap-demo
sudo systemctl status frps bootstrap-demo --no-pager
```

The demo bootstrap accepts one configured device, validates length, version, device identifier, reserved bytes and HMAC, and returns nonce-authenticated UTC and a port. It is neither the original platform nor an official FRP service. It can be replaced by your own secure clock and port allocation implementation.

### Browser HTTPS entry

Include `deploy/nginx.example.conf` under Nginx `http {}`, replacing the domain and browser certificate paths. Use `htpasswd -c /etc/nginx/gd32-demo.htpasswd demo` to interactively create a new entry password, protect the file and avoid reusing board or production passwords.

```sh
sudo nginx -t
sudo systemctl reload nginx
```

Nginx forwards authenticated HTTPS requests to `127.0.0.1:18080`; frps carries them over a TLS worker to the board Web service. This demonstrates the FRP path without recreating the original account/authorization interface. Retain the board's own authentication where required. With three workers, continuous streams or concurrent pages may exhaust capacity; validate your workload.

### Troubleshooting

Check local board HTTP first, then routing/security groups, UDP bootstrap, authenticated UTC, TLS certificate/IP SAN, FRP login, proxy registration, loopback HTTP and finally the cross-LAN browser. Confirm version 0.71.1, tcpMux=false, both authentication scopes, matching FRP tokens and a separate device secret. Inspect `journalctl -u frps -u bootstrap-demo`; do not paste credential-bearing configuration or business responses into public issues.
