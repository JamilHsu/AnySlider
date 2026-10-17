#!/usr/bin/env bash
# Linux -> Windows x64 Release build using LLVM and an xwin SDK/CRT tree.
set -euo pipefail

if [[ "${1:-}" == "--help" ]]; then
    printf '%s\n' \
        'Usage: XWIN_DIR=/path/to/xwin ./cross_build.sh' \
        'Requires: bash, python3, clang-cl, lld-link, llvm-ml and the checked-in Detours library.' \
        'Overrides: CLANG_CL (or CC_x86_64_pc_windows_msvc), LLD_LINK, LLVM_ML.' \
        'Output: bin/x64/AnySlider.dll and a fresh copy of config.toml.' \
        'Always rebuilds all sources; does not download dependencies or modify the SDK.'
    exit 0
fi
if [[ $# -ne 0 ]]; then
    printf 'Unknown argument. Use --help.\n' >&2
    exit 2
fi

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export ANYSLIDER_BUILD_ROOT="$ROOT"
python3 - <<'PY'
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def fail(message):
    raise SystemExit('cross_build: ' + message)


def find_tool(variable, default, sibling=None):
    configured = os.environ.get(variable)
    candidate = configured or (str(sibling) if sibling and sibling.is_file() else default)
    resolved = shutil.which(candidate)
    if not resolved:
        fail(f'{variable}: tool not found: {candidate}')
    return str(Path(resolved).absolute())


root = Path(os.environ['ANYSLIDER_BUILD_ROOT'])
# Preserve clang-cl's executable name: resolving its symlink to clang changes driver mode.
compiler_default = os.environ.get('CC_x86_64_pc_windows_msvc', 'clang-cl')
compiler = find_tool('CLANG_CL', compiler_default)
llvm_bin = Path(compiler).parent
linker = find_tool('LLD_LINK', 'lld-link', llvm_bin / 'lld-link')
assembler = find_tool('LLVM_ML', 'llvm-ml', llvm_bin / 'llvm-ml')
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
detours = root / 'Dependencies/Detours/lib/x64/detours.lib'
if not detours.is_file():
    fail('missing Dependencies/Detours/lib/x64/detours.lib')

project = ET.parse(root / 'AnySlider.vcxproj')
namespace = {'ms': 'http://schemas.microsoft.com/developer/msbuild/2003'}
# Read the project source lists, including MASM: no second manually maintained list.
sources = [node.attrib['Include'].replace('\\', '/')
           for node in project.findall('.//ms:ClCompile', namespace) if 'Include' in node.attrib]
assembly_sources = [node.attrib['Include'].replace('\\', '/')
                    for node in project.findall('.//ms:MASM', namespace) if 'Include' in node.attrib]
if not sources:
    fail('no compile sources in AnySlider.vcxproj')
for source in sources + assembly_sources:
    if not (root / source).is_file():
        fail(f'missing source: {source}; initialize repository dependencies first')

resource_dir = Path(subprocess.check_output(
    [compiler, '/clang:-print-resource-dir'], text=True).strip())
flags = ['--target=x86_64-pc-windows-msvc', '/nologo', '/c', '/O2', '/MT', '/EHsc',
         '/utf-8', '/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR', '/DNDEBUG',
         '/DANYSLIDER_EXPORTS', '/D_WINDOWS', '/D_USRDLL', '/DUNICODE', '/D_UNICODE',
         '/clang:-Wno-nonportable-include-path']
for include in [root, root / 'include', root / 'Dependencies/JoyShockLibrary/JoyShockLibrary',
                root / 'Dependencies/hidapi/hidapi', root / 'Dependencies/Detours/include']:
    flags.append('/I' + str(include))
# Clang builtin headers must precede the Windows CRT headers (notably stddef.h).
for include in [resource_dir / 'include'] + includes:
    flags.append('/external:I' + str(include))

out = root / 'bin/x64'
intermediate = root / 'obj/x64'
intermediate.mkdir(parents=True, exist_ok=True)
# Fresh objects avoid stale-header and changed-compiler cache issues. Clean on failure too.
with tempfile.TemporaryDirectory(prefix='cross-build-', dir=intermediate) as directory:
    work = Path(directory)
    objects = []
    for index, source in enumerate(sources):
        obj = work / f'cpp-{index}.obj'
        language = ['/TC', '/std:c17'] if source.endswith('.c') else ['/TP', '/std:c++20']
        print(f'Compile {source}', flush=True)
        # Sources already include their own headers; this build does not force a PCH.
        dependency_flags = []
        if source.startswith('Dependencies/JoyShockLibrary/'):
            # Upstream's exported C++ structs and nested comment examples are intentional.
            # Restrict suppressions to this dependency, never to our project sources.
            dependency_flags = ['/clang:-Wno-return-type-c-linkage', '/clang:-Wno-comment']
        subprocess.run([compiler, *flags, *language, *dependency_flags, source,
                        '/Fo' + str(obj)], cwd=root, check=True)
        objects.append(str(obj))
    for index, source in enumerate(assembly_sources):
        obj = work / f'asm-{index}.obj'
        asm = work / f'asm-{index}.asm'
        # llvm-ml uses case-sensitive symbols but does not implement MASM CASEMAP.
        # Remove only this directive from a temporary copy; leave tracked sources untouched.
        lines = (root / source).read_text(encoding='utf-8').splitlines()
        asm.write_text('\n'.join(line for line in lines
                                 if line.strip().upper() != 'OPTION CASEMAP:NONE') + '\n', encoding='utf-8')
        print(f'Assemble {source}', flush=True)
        subprocess.run([assembler, '-m64', '/c', '/Fo' + str(obj), str(asm)], cwd=root, check=True)
        objects.append(str(obj))

    dll = work / 'AnySlider.dll'
    command = [linker, '/nologo', '/dll', '/machine:x64', '/out:' + str(dll),
               '/implib:' + str(work / 'AnySlider.lib'), *objects, str(detours)]
    command += ['/libpath:' + str(directory) for directory in libraries]
    command += ['kernel32.lib', 'user32.lib', 'advapi32.lib', 'setupapi.lib', 'hid.lib',
                'winmm.lib', 'ole32.lib', 'uuid.lib', 'dinput8.lib', 'dxguid.lib']
    subprocess.run(command, cwd=root, check=True)
    # Publish only after compile/link succeeds; always refresh the packaged configuration.
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dll, out / 'AnySlider.dll')
    shutil.copy2(root / 'config.toml', out / 'config.toml')
print('Built bin/x64/AnySlider.dll and bin/x64/config.toml', flush=True)
PY
