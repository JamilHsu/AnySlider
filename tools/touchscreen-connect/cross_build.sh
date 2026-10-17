#!/usr/bin/env bash
# Linux -> Windows x64 Release build for TouchscreenConnect using LLVM and xwin SDK/CRT tree.
set -euo pipefail

if [[ "${1:-}" == "--help" ]]; then
    printf '%s\n' \
        'Usage: XWIN_DIR=/path/to/xwin ./cross_build.sh' \
        'Requires: bash, python3, clang-cl, lld-link.' \
        'Overrides: CLANG_CL (or CC_x86_64_pc_windows_msvc), LLD_LINK.' \
        'Output: TouchscreenConnect.exe'
    exit 0
fi

DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$DIR/../.." && pwd)"

export TOUCHSCREEN_CONNECT_DIR="$DIR"
export ANYSLIDER_ROOT="$ROOT"

python3 - <<'PY'
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

def fail(message):
    raise SystemExit('cross_build: ' + message)

def find_tool(variable, default, sibling=None):
    configured = os.environ.get(variable)
    candidate = configured or (str(sibling) if sibling and sibling.is_file() else default)
    resolved = shutil.which(candidate)
    if not resolved:
        fail(f'{variable}: tool not found: {candidate}')
    return str(Path(resolved).absolute())

root = Path(os.environ['ANYSLIDER_ROOT'])
tool_dir = Path(os.environ['TOUCHSCREEN_CONNECT_DIR'])

compiler_default = os.environ.get('CC_x86_64_pc_windows_msvc', 'clang-cl')
compiler = find_tool('CLANG_CL', compiler_default)
llvm_bin = Path(compiler).parent
linker = find_tool('LLD_LINK', 'lld-link', llvm_bin / 'lld-link')

sdk_setting = os.environ.get('XWIN_DIR')
if not sdk_setting:
    fail('set XWIN_DIR to an xwin splat directory containing crt/ and sdk/. See --help.')
sdk = Path(sdk_setting).expanduser().resolve()
includes = [sdk / 'crt/include', sdk / 'sdk/include/ucrt', sdk / 'sdk/include/um',
            sdk / 'sdk/include/shared', sdk / 'sdk/include/winrt']
libraries = [sdk / 'crt/lib/x86_64', sdk / 'sdk/lib/ucrt/x86_64', sdk / 'sdk/lib/um/x86_64']
for directory in includes + libraries:
    if not directory.is_dir():
        fail(f'incomplete xwin SDK/CRT tree: missing {directory}')

resource_dir = Path(subprocess.check_output(
    [compiler, '/clang:-print-resource-dir'], text=True).strip())

flags = ['--target=x86_64-pc-windows-msvc', '/nologo', '/c', '/O2', '/MT', '/EHsc',
         '/utf-8', '/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR', '/DNDEBUG',
         '/D_CONSOLE', '/DUNICODE', '/D_UNICODE', '/std:c++20',
         '/clang:-Wno-nonportable-include-path']

for inc in [root, root / 'src', root / 'include', tool_dir]:
    flags.append('/I' + str(inc))

for inc in [resource_dir / 'include'] + includes:
    flags.append('/external:I' + str(inc))

sources = [
    tool_dir / 'main.cpp',
    tool_dir / 'windows_input.cpp',
    tool_dir / 'test_window.cpp',
    tool_dir / 'elo_emulator.cpp',
    tool_dir / 'calibration_session.cpp',
    root / 'src/mm_io_touchscreen.cpp',
    root / 'src/arcade_eeprom.cpp',
    root / 'src/arcade_eeprom_write.cpp'
]

for src in sources:
    if not src.is_file():
        fail(f'missing source: {src}')

intermediate = tool_dir / 'obj/x64'
intermediate.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(prefix='cross-build-', dir=intermediate) as directory:
    work = Path(directory)
    objects = []
    for index, src in enumerate(sources):
        obj = work / f'obj-{index}.obj'
        print(f'Compile {src.relative_to(root) if src.is_relative_to(root) else src.name}', flush=True)
        subprocess.run([compiler, *flags, str(src), '/Fo' + str(obj)], cwd=tool_dir, check=True)
        objects.append(str(obj))

    exe = work / 'TouchscreenConnect.exe'
    command = [linker, '/nologo', '/subsystem:console', '/machine:x64', '/out:' + str(exe), *objects]
    command += ['/libpath:' + str(d) for d in libraries]
    command += ['kernel32.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib', 'setupapi.lib']
    subprocess.run(command, cwd=tool_dir, check=True)

    out_bin = tool_dir / 'bin/x64'
    out_bin.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, out_bin / 'TouchscreenConnect.exe')
    shutil.copy2(exe, tool_dir / 'TouchscreenConnect.exe')

    # Also make available in repo bin/x64 for convenience
    repo_bin = root / 'bin/x64'
    repo_bin.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, repo_bin / 'TouchscreenConnect.exe')

print(f'Built {tool_dir / "TouchscreenConnect.exe"} and {repo_bin / "TouchscreenConnect.exe"}', flush=True)
PY
