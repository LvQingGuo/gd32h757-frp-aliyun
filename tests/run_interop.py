#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build real firmware authentication code against an external Mbed TLS 3.6.7 tree."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mbedtls', required=True, type=Path)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    tls = args.mbedtls.resolve()
    spec = importlib.util.spec_from_file_location('bootstrap', root / 'deploy/bootstrap_demo.py')
    demo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(demo)
    with tempfile.TemporaryDirectory() as temporary:
        out = Path(temporary)
        executable = out / 'bootstrap_interop.exe'
        command = [args.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-DWIN32_LEAN_AND_MEAN', '-DMBEDTLS_CONFIG_FILE="bootstrap_config.h"',
                   '-I' + str(root / 'tests'), '-I' + str(root / 'firmware'), '-I' + str(tls / 'include'),
                   str(root / 'tests/bootstrap_interop.c'), str(root / 'firmware/cloud_bootstrap.c')]
        command += [str(tls / 'library' / name) for name in ['platform.c', 'md.c', 'sha256.c', 'platform_util.c']]
        subprocess.run(command + ['-o', str(executable)], check=True)
        request_file = out / 'request.bin'
        reply_file = out / 'reply.bin'
        subprocess.run([str(executable), 'request', str(request_file)], check=True)
        config = {'device': 'cmu-000000000000000000000001', 'secret': 'a' * 64, 'proxy_port': 18080}
        reply = demo.make_reply(request_file.read_bytes(), config, 1793664000)
        if reply is None:
            raise RuntimeError('Firmware request rejected by Python companion')
        reply_file.write_bytes(reply)
        subprocess.run([str(executable), 'reply', str(reply_file)], check=True)


if __name__ == '__main__':
    main()
