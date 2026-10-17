"""Explicit synthetic ABI fixture integration test (not game/hardware validation).
Run: python tools/AnySlider-Viewer/test_bridge.py
Requires Python websockets. Starts/stops its own .NET service.
"""
import asyncio
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import urllib.request
import websockets

ROOT = Path(__file__).resolve().parent
PORT = 18766

def fixture():
    data = bytearray(6368)
    struct.pack_into('<IHHIIq', data, 0, 0x4F494D4D, 3, 0, 6368, 1, 1)
    struct.pack_into('<q', data, 24, 1)
    data[32 + 8 // 8] |= 1 << (8 % 8)
    data[32 + 160 // 8] |= 1 << (160 % 8)
    struct.pack_into('<IIQ', data, 56, 0x80000001, 1, 1234)
    struct.pack_into('<qQI', data, 6248, 2, 1234, (1 << 3) | (1 << 8))
    data[6268] = 63
    data[6269:6272] = bytes([12, 34, 56])
    return data

async def verify(path):
    async with websockets.connect(f'ws://127.0.0.1:{PORT}/ws') as ws:
        frame = json.loads(await asyncio.wait_for(ws.recv(), 5))
        assert frame['source'] == 'abi-fixture'
        assert frame['status'] == 'available'
        assert 'input' not in frame
        assert base64.b64decode(frame['led']['sliderBase64'])[:4] == bytes([63, 12, 34, 56])
        # Odd output is not accepted.
        with path.open('r+b') as f:
            f.seek(6248); f.write(struct.pack('<q', 3)); f.flush()
        while True:
            odd = json.loads(await asyncio.wait_for(ws.recv(), 5))
            if odd['led'] is None: break
        assert odd['status'] == 'waiting-for-snapshot'
        # Stable new commit clears buttons and updates LED independently.
        data = fixture()
        struct.pack_into('<q', data, 16, 2)
        struct.pack_into('<q', data, 72, 2)
        struct.pack_into('<IIQ', data, 104, 0, 1, 2345)
        struct.pack_into('<q', data, 6248, 4)
        path.write_bytes(data)
        while True:
            changed = json.loads(await asyncio.wait_for(ws.recv(), 5))
            if changed['led'] and changed['led']['sequence'] == '4': break
        assert changed['led']['sequence'] == '4'
        # Reject wrong ABI, then recover with the same WS connection.
        with path.open('r+b') as f:
            f.seek(4); f.write(struct.pack('<H', 2)); f.flush()
        while True:
            bad = json.loads(await asyncio.wait_for(ws.recv(), 5))
            if bad['status'] == 'unavailable': break
        assert 'input' not in bad and bad['led'] is None
        path.write_bytes(fixture())
        while True:
            recovered = json.loads(await asyncio.wait_for(ws.recv(), 5))
            if recovered['status'] == 'available': break
    async with websockets.connect(f'ws://127.0.0.1:{PORT}/ws') as ws:
        assert json.loads(await asyncio.wait_for(ws.recv(), 5))['status'] == 'available'

if __name__ == '__main__':
    scratch = os.environ.get('TMPDIR', str(Path.home() / '.hermes/cache/scratch'))
    with tempfile.TemporaryDirectory(prefix='anyslider-viewer-', dir=scratch) as folder:
        path = Path(folder) / 'mmio.bin'
        path.write_bytes(fixture())
        process = subprocess.Popen(['dotnet', 'run', '--no-build', '--project', str(ROOT / 'AnySlider-Viewer.csproj'), '--', '--port', str(PORT), '--snapshot-file', str(path)], stdout=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{PORT}/api/status') as response:
                        assert json.load(response)['status'] == 'available'
                    break
                except OSError: time.sleep(.1)
            else: raise RuntimeError('Server not ready')
            for name in ['index.html', 'viewer.js', 'viewer.css']:
                with urllib.request.urlopen(f'http://127.0.0.1:{PORT}/{name}') as response:
                    assert response.status == 200
            asyncio.run(verify(path))
            print('PASS: static assets, ABI decode, LED-only contract, odd LED rejection, changed commit, ABI rejection/recovery, WS reconnect')
        finally:
            process.terminate()
            process.wait(timeout=10)
