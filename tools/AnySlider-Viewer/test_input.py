"""Synthetic fixture test for the actual WS -> MMIO writer, not game acceptance."""
import asyncio
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import urllib.request
import websockets
from test_bridge import fixture, ROOT
PORT=18767
async def status(ws, enabled=None, error=False):
    for _ in range(200):
        frame=json.loads(await asyncio.wait_for(ws.recv(),5))
        if (enabled is None or frame['inputEnabled']==enabled) and (not error or frame['inputError']): return frame
    raise AssertionError('status not received')
def snapshots(path):
    data=path.read_bytes(); seq=struct.unpack_from('<q',data,16)[0]
    result=[]
    for n in range(max(1,seq-127),seq+1):
        offset=24+(n-1)%128*48
        committed=struct.unpack_from('<q',data,offset)[0]
        if committed==n: result.append((n,*struct.unpack_from('<QQQIIQ',data,offset+8)))
    return result
async def verify(path):
    async with websockets.connect(f'ws://127.0.0.1:{PORT}/ws') as ws:
        await status(ws,False)
        original=path.read_bytes()
        await ws.send(json.dumps({'type':'state','buttons':256}))
        await status(ws,False,True)
        assert path.read_bytes()==original, 'disabled writes'
        await ws.send(json.dumps({'type':'enable'})); await status(ws,True)
        baseline=path.read_bytes()[6208:]
        for bit in [8,7,10,9]:
            await ws.send(json.dumps({'type':'state','buttons':1<<bit,'touchMask':0x80000001}))
            await asyncio.sleep(.05)
            assert snapshots(path)[-1][1:6]==(1<<bit,0,0,0x80000001,1)
        await ws.send(json.dumps({'type':'pulse','buttons':256,'touchMask':0x80000000}))
        await asyncio.sleep(.05)
        slots=snapshots(path)
        pairs=[(a,b) for a,b in zip(slots,slots[1:]) if a[1:6]==(256,0,0,0x80000000,1) and b[1:6]==(0,0,0,0,1)]
        assert pairs, 'missing consecutive pulse press/release'
        # Native consumer button-edge formula over this pair retains both edges.
        down=0; tapped=0; released=0
        for row in pairs[-1]:
            current=row[1]; tapped |= current & ~down; released |= down & ~current; down=current
        assert tapped==released==256 and down==0
        async with websockets.connect(f'ws://127.0.0.1:{PORT}/ws') as second:
            await second.send(json.dumps({'type':'enable'})); await status(second,False,True)
        await ws.send(json.dumps({'type':'state','buttons':128})); await asyncio.sleep(.04)
        await ws.send(json.dumps({'type':'disable'})); await status(ws,False)
        assert snapshots(path)[-1][1:6]==(0,0,0,0,0)
        assert path.read_bytes()[6208:]==baseline, 'hook/output modified'
        await ws.send(json.dumps({'type':'enable'})); await status(ws,True)
        await ws.send(json.dumps({'type':'state','buttons':512})); await asyncio.sleep(.04)
    await asyncio.sleep(.1)
    assert snapshots(path)[-1][1:6]==(0,0,0,0,0), 'disconnect release'
    async with websockets.connect(f'ws://127.0.0.1:{PORT}/ws') as ws:
        await ws.send(json.dumps({'type':'enable'})); await status(ws,True)
        await ws.send(json.dumps({'type':'state','buttons':1024})); await asyncio.sleep(1.8)
        await status(ws,False)
        assert snapshots(path)[-1][1:6]==(0,0,0,0,0), 'watchdog release'
    data=bytearray(path.read_bytes());struct.pack_into('<I',data,6168,123);struct.pack_into('<Q',data,6184,int(time.monotonic()*1000));path.write_bytes(data)
    async with websockets.connect(f'ws://127.0.0.1:{PORT}/ws') as ws:
        await ws.send(json.dumps({'type':'enable'}));frame=await status(ws,False,True)
        assert 'active MMIO producer' in frame['inputError']
        assert path.read_bytes()==data
if __name__=='__main__':
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR',str(Path.home()/'.hermes/cache/scratch'))) as folder:
        path=Path(folder)/'mmio.bin';path.write_bytes(fixture())
        process=subprocess.Popen(['dotnet','run','--no-build','--project',str(ROOT/'AnySlider-Viewer.csproj'),'--','--port',str(PORT),'--snapshot-file',str(path)],stdout=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:
                    urllib.request.urlopen(f'http://127.0.0.1:{PORT}/api/status').close();break
                except OSError:time.sleep(.1)
            else: raise RuntimeError('server not ready')
            asyncio.run(verify(path))
            print('PASS: opt-in writes, 4 button IDs, touch bit31, pulse pair/edges, exclusive client, disable/disconnect/watchdog release, active producer rejection, hook/LED unchanged')
        finally:process.terminate();process.wait(timeout=10)
