# AnySlider-Viewer

.NET 10 local CLI/server + static `web/` frontend adapted from `divapro_keytest.html`.
First version displays **32 slider LED cells and four IO3 button lamps only** (Triangle bit 3, Cross bit 4, Square bit 6, Circle bit 7). It does not read input snapshots or display physical button presses. Input visualization is deferred; the input ring is an external producer contract, not the merged native game input stream.

## Run on the Windows game host

```powershell
dotnet run --project tools/AnySlider-Viewer/AnySlider-Viewer.csproj
```

- Viewer: `http://127.0.0.1:8765/`
- OBS browser source: `http://127.0.0.1:8765/?overlay=1` (transparent controller only).
- WebSocket: `ws://127.0.0.1:8765/ws`
- Diagnostic JSON: `/api/status`
- CLI: `--port 8765`, `--help`.

The server starts even if the game mapping is missing and retries read-only access. Expected mapping: `Local\MMIO_SHARED_BUFFER`, ABI 3.0 / 6368 bytes. It never writes the mapping. Listener is loopback-only with Host/Origin checks; this is not an authenticated remote service. Web assets are copied to build/publish output, not embedded into the DLL.

## Wire contract

Each message contains `source`, `status`, `error`, and nullable `led`:

```json
{"source":"windows-mapping","status":"available","error":null,"led":{"sequence":"2","timestampMs":"1234","io3Bitmap":8,"sliderBase64":"..."}}
```

`sequence` and `timestampMs` use decimal strings to preserve 64-bit precision. `sliderBase64` is the original 97-byte LED payload: 0x3F + 32 BRG triplets. Only the browser converts BRG to CSS RGB. Bitmap is the cabinet logical bitmap, not JVS GPIO wire bytes. Unknown bits remain in diagnostics but do not drive extra UI lamps.

Aligned sequence reads and memory barriers guard snapshot reads; odd/changing output is rejected. WS sends latest state, not history, and repeats status once per second. Unavailable data/disconnection clears rendered lights. An unchanged sequence for two seconds is marked stale; this is not proof of a device disconnect. Captured LED output does not prove physical output or ACK.

## Development/test

```sh
dotnet build tools/AnySlider-Viewer/AnySlider-Viewer.csproj
python tools/AnySlider-Viewer/test_bridge.py
```

The Python integration test requires `websockets` and generates explicitly synthetic ABI fixtures. `--snapshot-file <file>` reads a file-backed ABI fixture on Linux/Windows for bridge/UI testing; it is never automatically used as a replacement for real game data. The test uses port 18766, checks HTTP assets, LED-only JSON, decoding, odd-sequence rejection, new commits, ABI mismatch/recovery and reconnect.

Verified locally: .NET build, file-backed integration transport and live browser DOM rendering including BRG conversion and overlay transparency. Windows live named mapping, MegaMix runtime hooks and physical hardware acceptance remain unverified.
