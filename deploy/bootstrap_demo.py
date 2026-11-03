#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Single-device authenticated clock/port demo. Not the original cloud platform.
单设备认证对时/端口示例，不包含原平台的账户、授权或自动登记业务。
"""
import argparse
import hashlib
import hmac
import json
from pathlib import Path
import re
import socket
import struct
import time

def validate(config):
    """Reject placeholder credentials and invalid single-device settings."""
    if not re.fullmatch(r'cmu-[0-9a-f]{24}', config['device']):
        raise ValueError('Invalid device identifier')
    if not re.fullmatch(r'[0-9a-f]{64}', config['secret']):
        raise ValueError('Provision a fresh 64-character secret')
    if not 1024 <= config['proxy_port'] <= 65535 or not 1 <= config['port'] <= 65535:
        raise ValueError('Invalid port')

def make_reply(data, config, now=None):
    """Authenticate an 88-byte request; return a nonce-bound 68-byte reply."""
    if len(data) != 88 or data[:4] != b'CMU2':
        return None
    if struct.unpack('!I', data[4:8])[0] != 1:
        return None
    if data[8:36] != config['device'].encode('ascii') or data[36:40] != bytes(4):
        return None
    key = config['secret'].encode('ascii')
    if not hmac.compare_digest(hmac.digest(key, data[:56], 'sha256'), data[56:]):
        return None
    seconds = int(time.time()) if now is None else now
    if not 1767225600 <= seconds <= 4102444800:
        return None
    body = b'CMR2' + data[40:56] + struct.pack('!QII', seconds, config['proxy_port'], 1)
    return body + hmac.digest(key, body, 'sha256')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding='utf-8'))
    validate(config)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind((config['listen'], config['port']))
        print('Authenticated bootstrap demo ready', flush=True)
        # Bound total response rate; invalid packets are silently discarded.
        last = 0.0
        while True:
            data, peer = server.recvfrom(89)
            now = time.monotonic()
            if now - last < 0.05:
                continue
            reply = make_reply(data, config)
            if reply is not None:
                server.sendto(reply, peer)
                last = now

if __name__ == '__main__':
    main()
