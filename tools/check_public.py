#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the exact public allowlist; findings report paths/lines, never values.
检查公开白名单，只报告文件与行号，不回显敏感匹配内容。
"""
import ipaddress
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTATION_NETWORKS = [ipaddress.ip_network(value) for value in
                          ['192.0.2.0/24', '198.51.100.0/24', '203.0.113.0/24', '127.0.0.0/8']]


def findings(text):
    """Yield category/line only; this is a focused check, not a universal secret detector."""
    for number, line in enumerate(text.splitlines(), 1):
        if re.search(r'-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----|\b(?:gh[pousr]_|github_pat_)[A-Za-z0-9_]{15,}|\bLTAI[A-Za-z0-9]{12,}', line):
            yield 'private-key-or-access-token', number
        if re.search(r'(?<![0-9a-f])[0-9a-f]{64}(?![0-9a-f])', line):
            yield 'unreviewed-64-hex-value', number
        for value in re.findall(r'(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])', line):
            try:
                address = ipaddress.ip_address(value)
            except ValueError:
                continue
            if str(address) != '0.0.0.0' and not any(address in network for network in DOCUMENTATION_NETWORKS):
                yield 'non-documentation-ipv4', number
        for value in re.findall(r'cmu-[0-9a-f]{24}', line):
            if value != 'cmu-000000000000000000000001':
                yield 'non-example-device-id', number
        if re.search(r'-----BEGIN CERTIFICATE-----\\n[A-Za-z0-9+/]{20,}', line):
            yield 'embedded-real-certificate', number


def main():
    names = json.loads((ROOT / 'PUBLIC_FILES.json').read_text(encoding='utf-8'))
    errors = []
    if len(names) != len(set(names)):
        errors.append('Duplicate public paths')
    for name in names:
        file = (ROOT / name).resolve()
        if ROOT not in file.parents or not file.is_file() or file.is_symlink():
            errors.append('Invalid public path: ' + name)
            continue
        if file.suffix.lower() in {'.png', '.jpg', '.zip', '.bin', '.db', '.key', '.pem', '.crt', '.exe'}:
            errors.append('Unexpected public artifact type: ' + name)
            continue
        for category, line in findings(file.read_text(encoding='utf-8')):
            errors.append('{}:{}: {}'.format(name, line, category))
    if errors:
        print('\n'.join(errors))
        return 1
    print('PASS: {} allowlisted UTF-8 files; no targeted secret patterns found.'.format(len(names)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
