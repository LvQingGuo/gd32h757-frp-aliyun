#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate fresh local demo configuration; never prints secrets.
生成全新本地配置，不输出任何密钥。生成目录已被 Git 忽略。
"""
import argparse
import ipaddress
import json
from pathlib import Path
import secrets


def generate(server, ca, device, destination):
    """Create three matching configurations without overwriting existing files."""
    address = ipaddress.ip_address(server)
    if address.version != 4 or address.is_unspecified or address.is_multicast or address.is_loopback:
        raise ValueError('Provide the ECS IPv4 literal')
    import re
    if not re.fullmatch(r'cmu-[0-9a-f]{24}', device):
        raise ValueError('Device format: cmu- followed by 24 lowercase hex characters')
    if not ca.startswith('-----BEGIN CERTIFICATE-----') or len(ca.encode('ascii')) + 1 > 2048:
        raise ValueError('Provide one PEM CA certificate fitting the 2048-byte firmware buffer')
    if 'PRIVATE KEY' in ca or '-----END CERTIFICATE-----' not in ca:
        raise ValueError('Expected a public CA certificate, never a private key')
    root = Path(__file__).resolve().parents[1]
    destination = Path(destination)
    files = ['cloud_demo_config.h', 'frps.toml', 'bootstrap.json']
    if any((destination / name).exists() for name in files):
        raise FileExistsError('Configuration exists; choose a fresh directory')
    token = secrets.token_hex(32)
    secret = secrets.token_hex(32)
    header = (root / 'firmware/cloud_demo_config.example.h').read_text(encoding='utf-8')
    header = header.replace('#define CLOUD_DEMO_CONFIGURED 0', '#define CLOUD_DEMO_CONFIGURED 1')
    header = header.replace('"192.0.2.10"', json.dumps(server))
    header = header.replace('"cmu-000000000000000000000001"', json.dumps(device))
    header = header.replace('REPLACE_WITH_64_LOWERCASE_HEX_CHARACTERS', token)
    header = header.replace('REPLACE_WITH_DIFFERENT_64_LOWERCASE_HEX_CHARACTERS', secret)
    header = header.replace('"REPLACE_WITH_YOUR_OWN_PEM_CA_CERTIFICATE"', json.dumps(ca))
    frps = (root / 'deploy/frps.example.toml').read_text(encoding='utf-8')
    frps = frps.replace('REPLACE_WITH_64_LOWERCASE_HEX_CHARACTERS', token)
    bootstrap = {'listen': '0.0.0.0', 'port': 7000, 'device': device, 'secret': secret, 'proxy_port': 18080}
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    for name, content in zip(files, [header, frps, json.dumps(bootstrap, indent=2) + '\n']):
        file = destination / name
        with file.open('x', encoding='utf-8', newline='\n') as output:
            file.chmod(0o600)
            output.write(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True)
    parser.add_argument('--ca', required=True, type=Path)
    parser.add_argument('--device', default='cmu-000000000000000000000001')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / 'private')
    args = parser.parse_args()
    generate(args.server, args.ca.read_text(encoding='ascii').strip() + '\n', args.device, args.out)
    print('Fresh configuration created in the selected local directory. Keep it private.')


if __name__ == '__main__':
    main()
