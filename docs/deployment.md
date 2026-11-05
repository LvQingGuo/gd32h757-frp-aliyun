# 阿里云 ECS 部署

### 最低服务器前提

按本仓库代码建立 GD32 与 frps 的连接，ECS 需要以下条件。其他可达的 Linux 服务器也适用，不依赖阿里云专有 SDK 或云平台业务源码。

| 条件 | 必需内容 |
| --- | --- |
| 计算资源 | 可运行对应架构 frps 的 Linux 环境；下文命令以 Ubuntu 22.04 x86_64 为例 |
| 网络 | 板端可达的 IPv4，ECS 安全组和主机防火墙同时允许 TCP 7000、UDP 7000；这两个端口未被其他服务占用 |
| FRP 服务 | 官方 **frps 0.71.1**，关闭 tcpMux，强制 TLS，启用 `HeartBeats` 与 `NewWorkConns` 认证 |
| 认证引导 | Python **3.8+** 和本仓库 `bootstrap_demo.py`，使用标准库即可，无需额外 pip 包；为单个设备提供认证 UTC 和代理端口 |
| 时钟和 TLS | 正确的服务器 UTC；P-256 ECDSA 叶证书及私钥，IP SAN 与板端配置的服务器 IPv4 一致，板端持有其 CA 公钥证书 |
| 匹配配置 | 两端相同的 FRP token、设备 ID 和独立设备密钥；引导返回的代理端口在 frps `allowPorts` 中，本例为 18080 |
| 管理与运行 | 安装文件和启动服务的权限；本例用 systemd 常驻运行，也可自行用其他进程管理方式启动两个服务 |

**最小连通测试只需要 frps + 认证引导服务 + 两端匹配配置，以及板端的本机 TCP 服务。** 服务端代理监听 `127.0.0.1:18080`，可先在 ECS 内测试；Nginx、域名、浏览器 HTTPS 证书和入口账号仅在需要公开浏览器入口时再增加。

没有测得适用于所有应用的最低 vCPU、RAM、磁盘或带宽规格；下方的 2 vCPU/2 GiB/40 GiB 是已用环境，不是最低套餐要求。其他 CPU 架构必须换用对应 frps 发布包，其他发行版相应调整软件安装和服务管理命令。

在新建测试 ECS 上准备基础工具（如已有服务，先核对目录和服务名再安排安装）：

```sh
sudo apt-get update
sudo apt-get install -y python3 openssl ca-certificates curl
```

### 已验证的资源基线

| 项目 | 配置 |
| --- | --- |
| 云资源 | 阿里云 ECS，西南 1（成都） |
| 系统 | Ubuntu 22.04.5 LTS，x86_64 |
| CPU / 内存 | 2 vCPU / 2 GiB |
| 系统盘 | 40 GiB 云盘 |
| 公网 | 可达 IPv4；带宽峰值为 100 Mbps |
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

以下命令在 ECS 的独立下载目录中执行，适用于 **x86_64**：

```sh
mkdir -p ~/gd32-frp-download
cd ~/gd32-frp-download
curl -fLO https://github.com/fatedier/frp/releases/download/v0.71.1/frp_0.71.1_linux_amd64.tar.gz
curl -fLO https://github.com/fatedier/frp/releases/download/v0.71.1/frp_sha256_checksums.txt
sha256sum --check --ignore-missing frp_sha256_checksums.txt
tar -xzf frp_0.71.1_linux_amd64.tar.gz
sudo install -d -m 0755 /opt/frp
sudo install -m 0755 frp_0.71.1_linux_amd64/frps /opt/frp/frps
sudo install -m 0644 frp_0.71.1_linux_amd64/LICENSE /opt/frp/LICENSE
/opt/frp/frps --version
```

校验失败时先解决下载问题再解压；最终版本输出应为 `0.71.1`。

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

在 ECS 创建独立的无登录权限 `frps` 系统用户，将配置、叶证书和私钥设为 root 所有、frps 组可读。为便于操作，先将公开仓库下载到 ECS，再经安全通道传入自己生成的配置与叶证书，放在该副本的 `private/` 目录；只传 `frps.toml`、`bootstrap.json`、`server.crt`、`server.key`，CA 签发私钥留在离线签发环境。

以下安装命令在 ECS 的仓库根目录执行，针对前述新建测试环境：

```sh
if ! id frps >/dev/null 2>&1; then
    sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin frps
fi
sudo install -d -o root -g frps -m 0750 /etc/frp /etc/frp/tls
sudo install -d -o root -g root -m 0755 /opt/gd32-frp
sudo install -o root -g frps -m 0640 private/frps.toml /etc/frp/frps.toml
sudo install -o root -g frps -m 0640 private/bootstrap.json /etc/frp/bootstrap.json
sudo install -o root -g frps -m 0640 private/server.crt /etc/frp/tls/server.crt
sudo install -o root -g frps -m 0640 private/server.key /etc/frp/tls/server.key
sudo install -o root -g root -m 0644 deploy/bootstrap_demo.py /opt/gd32-frp/bootstrap_demo.py
sudo install -o root -g root -m 0644 deploy/frps.service /etc/systemd/system/frps.service
sudo install -o root -g root -m 0644 deploy/bootstrap-demo.service /etc/systemd/system/bootstrap-demo.service
```

```sh
sudo /opt/frp/frps verify -c /etc/frp/frps.toml
sudo systemctl daemon-reload
sudo systemctl enable --now frps bootstrap-demo
sudo systemctl status frps bootstrap-demo --no-pager
```

示例引导服务仅接受配置的一个设备，校验固定长度、版本、设备号、保留字段和 HMAC，再返回带挑战认证的 UTC/端口。它不是原云平台，也不是 FRP 官方服务；可用自己的安全认证时钟与端口分配实现替换。

到此先完成板端固件配置与启动，再在 ECS 执行：

```sh
curl --fail --max-time 15 http://127.0.0.1:18080/
```

返回板端实际 HTTP 页面后，已验证板端—frps—服务器回环转发链路。此时再按需增加下面的公网浏览器入口。

### HTTPS 浏览器入口（可选扩展）

需要从电脑浏览器访问时，在 ECS 安装 `nginx` 和提供 `htpasswd` 的 `apache2-utils`，准备自己的 HTTPS 域名证书或客户端已信任的测试证书，并在安全组/防火墙放行 TCP 443。用于浏览器的域名证书与板端连接 frps 所用的 IP SAN 证书可以分别管理。

```sh
sudo apt-get install -y nginx apache2-utils
```

将 `deploy/nginx.example.conf` 作为 Nginx `http {}` 下的配置，改成自己的域名和浏览器证书路径。用 `htpasswd -c /etc/nginx/gd32-demo.htpasswd demo` 交互设置新的入口密码，保持密码文件权限；不要复用板端或生产平台密码。

```sh
sudo nginx -t
sudo systemctl reload nginx
```

Nginx 将已认证的 HTTPS 请求转到 `127.0.0.1:18080`，frps 再经 TLS 工作连接到板端 Web。此入口只展示 FRP 连通链路，未复刻原平台的账户管理/授权界面。板端服务如需自己的登录，应保留。示例最多三条工作连接，持续流或并发页面可能占满，需按实际负载验证。

### 排查顺序

先验证板端本机 Web，再检查路由、安全组、UDP 引导、认证 UTC、TLS 证书/IP SAN、FRP 登录、代理注册和回环 HTTP，最后测试跨 LAN 浏览器。确认 0.71.1、tcpMux=false、两个认证 scope、相同 FRP token 及独立设备密钥。检查 `journalctl -u frps -u bootstrap-demo`，避免把带凭据的配置或业务响应粘贴到公开 issue。
