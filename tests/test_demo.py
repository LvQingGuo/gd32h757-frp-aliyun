# SPDX-License-Identifier: GPL-3.0-only
"""Host checks of authenticated bootstrap, provisioning, and public-data detection."""
import importlib.util
import hmac
import json
from pathlib import Path
import socket
import struct
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


demo = load('bootstrap', 'deploy/bootstrap_demo.py')
provision = load('provision', 'tools/provision.py')
scanner = load('scanner', 'tools/check_public.py')


class DemoTests(unittest.TestCase):
    def setUp(self):
        self.config = {'device': 'cmu-000000000000000000000001', 'secret': 'a' * 64,
                       'proxy_port': 18080, 'port': 7000, 'listen': '127.0.0.1'}
        self.nonce = bytes(range(16))
        body = b'CMU2' + struct.pack('!I', 1) + self.config['device'].encode() + bytes(4) + self.nonce
        self.request = body + hmac.digest(self.config['secret'].encode(), body, 'sha256')

    def test_authenticated_reply(self):
        reply = demo.make_reply(self.request, self.config, 1793664000)
        self.assertEqual(len(reply), 68)
        self.assertEqual(reply[:20], b'CMR2' + self.nonce)
        self.assertEqual(struct.unpack('!QII', reply[20:36]), (1793664000, 18080, 1))
        self.assertTrue(hmac.compare_digest(reply[36:], hmac.digest(self.config['secret'].encode(), reply[:36], 'sha256')))

    def test_request_tampering_and_length(self):
        for index in range(len(self.request)):
            damaged = bytearray(self.request)
            damaged[index] ^= 1
            self.assertIsNone(demo.make_reply(bytes(damaged), self.config, 1793664000))
        for data in [self.request[:-1], self.request + b'x', b'']:
            self.assertIsNone(demo.make_reply(data, self.config))

    def test_wrong_identity_and_key(self):
        for field, value in [('secret', 'b' * 64), ('device', 'cmu-' + '0' * 23 + '2')]:
            self.assertIsNone(demo.make_reply(self.request, dict(self.config, **{field: value})))

    def test_time_range(self):
        for value in [0, 1767225599, 4102444801]:
            self.assertIsNone(demo.make_reply(self.request, self.config, value))

    def test_actual_loopback_udp(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0))
            server.settimeout(2)
            def respond():
                data, peer = server.recvfrom(89)
                server.sendto(demo.make_reply(data, self.config, 1793664000), peer)
            worker = threading.Thread(target=respond)
            worker.start()
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                client.settimeout(2)
                client.sendto(self.request, server.getsockname())
                reply = client.recv(100)
            worker.join(2)
            self.assertEqual(reply, demo.make_reply(self.request, self.config, 1793664000))

    def test_placeholder_config_rejected(self):
        config = json.loads((ROOT / 'deploy/bootstrap.example.json').read_text())
        with self.assertRaises(ValueError):
            demo.validate(config)

    def test_provision_fresh_matching_credentials_and_no_overwrite(self):
        # Synthetic public CA text tests serialization only, not certificate validity.
        ca = '-----BEGIN CERTIFICATE-----\nTESTONLY\n-----END CERTIFICATE-----\n'
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            provision.generate('192.0.2.10', ca, self.config['device'], out)
            config = json.loads((out / 'bootstrap.json').read_text())
            demo.validate(config)
            header = (out / 'cloud_demo_config.h').read_text(encoding='utf-8')
            frps = (out / 'frps.toml').read_text(encoding='utf-8')
            self.assertIn(config['secret'], header)
            token = frps.split('auth.token = "')[1].split('"')[0]
            self.assertIn(token, header)
            self.assertNotEqual(token, config['secret'])
            self.assertNotIn('REPLACE_WITH', header)
            with self.assertRaises(FileExistsError):
                provision.generate('192.0.2.10', ca, self.config['device'], out)

    def test_scanner_documentation_and_sensitive_fixtures(self):
        self.assertEqual(list(scanner.findings('192.0.2.10 127.0.0.1 0.0.0.0')), [])
        self.assertTrue(list(scanner.findings('.'.join(['10', '1', '2', '3']))))
        self.assertTrue(list(scanner.findings('-----BEGIN ' + 'PRIVATE KEY-----')))
        self.assertTrue(list(scanner.findings('b' * 64)))


if __name__ == '__main__':
    unittest.main()
