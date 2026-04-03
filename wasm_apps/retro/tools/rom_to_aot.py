#!/usr/bin/env python3
"""
rom_to_aot.py — Convert retro game ROM images to AkiraOS AOT binaries.

Two-stage pipeline:
  1. ROM → WASM   (via rom_to_wasm.py / WASI SDK clang)
  2. WASM → AOT   (via wamrc, Ahead-Of-Time compiler)

The resulting .aot file runs directly on the ESP32-S3 without the
WASM interpreter, giving 10-50× performance improvement.

Usage:
  python3 rom_to_aot.py <rom_file> [options]

Options:
  -o, --output <file>      Output AOT file (default: <rom_name>.aot)
  -p, --platform <name>    Platform: auto|nes (default: auto)
  -n, --name <name>        App name in manifest (default: rom filename)
  --wasi-sdk <path>        Path to WASI SDK (default: /opt/wasi-sdk)
  --wamrc <path>           Path to wamrc binary (default: auto-detect)
  --sdk-root <path>        Path to AkiraSDK root (default: auto-detect)
  --target <arch>          AOT target architecture (default: xtensa)
  --cpu <cpu>              AOT target CPU (default: esp32s3)
  --opt-level <0-3>        AOT optimization level (default: 3)
  --size-level <0-3>       AOT code model (0=large,1=medium,2=kernel,3=small; default: 0)
  --keep-wasm              Keep intermediate .wasm file
  -v, --verbose            Verbose output

Examples:
  python3 rom_to_aot.py tetris.nes
  python3 rom_to_aot.py tetris.nes -o tetris.aot --keep-wasm
  python3 rom_to_aot.py galaga.nes --wamrc /path/to/wamrc -v
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ── Helpers ───────────────────────────────────────────────────────────

def log(msg, verbose=True):
    if verbose:
        print(msg)


def err(msg):
    print(f'ERROR: {msg}', file=sys.stderr)
    sys.exit(1)


def find_wamrc():
    """Auto-detect the wamrc AOT compiler binary.

    Search order:
      1. $WAMRC environment variable (explicit path)
      2. System PATH
      3. Look for a wasm-micro-runtime checkout near the SDK and check
         known build output paths inside it.
    """
    # 1. Explicit env var
    env_wamrc = os.environ.get('WAMRC')
    if env_wamrc and os.path.isfile(env_wamrc) and os.access(env_wamrc, os.X_OK):
        return env_wamrc

    # 2. System PATH
    path_wamrc = shutil.which('wamrc')
    if path_wamrc:
        return path_wamrc

    # 3. Search for a wasm-micro-runtime directory near the SDK root,
    #    then check known wamrc locations inside it.
    sdk_root = find_sdk_root()
    if sdk_root:
        search_root = os.path.dirname(sdk_root)
        wamrc_subpaths = (
            os.path.join('wamr-compiler', 'build', 'wamrc'),
            os.path.join('wamr-compiler', 'wamrc'),
        )
        for dirpath, dirnames, _ in os.walk(search_root):
            depth = dirpath[len(search_root):].count(os.sep)
            if depth >= 3:
                dirnames.clear()
                continue
            # Skip hidden directories
            dirnames[:] = [d for d in dirnames if not d.startswith('.')]
            if 'wasm-micro-runtime' in os.path.basename(dirpath):
                for sub in wamrc_subpaths:
                    c = os.path.join(dirpath, sub)
                    if os.path.isfile(c) and os.access(c, os.X_OK):
                        return c
                dirnames.clear()
    return None


def find_sdk_root():
    """Walk upward from script dir to find AkiraSDK root."""
    path = SCRIPT_DIR
    for _ in range(8):
        if os.path.isfile(os.path.join(path, 'include', 'akira_api.h')):
            return path
        path = os.path.dirname(path)
    return None


def build_wasm(rom_path, wasm_output, platform, name, wasi_sdk, sdk_root,
               verbose=False):
    """Stage 1: ROM → WASM via rom_to_wasm.py."""
    rom_to_wasm = os.path.join(SCRIPT_DIR, 'rom_to_wasm.py')
    if not os.path.isfile(rom_to_wasm):
        err(f'rom_to_wasm.py not found at {rom_to_wasm}')

    cmd = [
        sys.executable, rom_to_wasm,
        rom_path,
        '-o', wasm_output,
        '--wasi-sdk', wasi_sdk,
        '--sdk-root', sdk_root,
    ]
    if platform != 'auto':
        cmd += ['-p', platform]
    if name:
        cmd += ['-n', name]
    if verbose:
        cmd += ['-v']

    log(f'  Stage 1: ROM → WASM', verbose)
    log(f'  {" ".join(cmd)}', verbose)

    result = subprocess.run(cmd, capture_output=not verbose)
    if result.returncode != 0:
        if not verbose and result.stderr:
            print(result.stderr.decode() if isinstance(result.stderr, bytes)
                  else result.stderr, file=sys.stderr)
        err('WASM compilation failed')


def compile_aot(wasm_path, aot_output, wamrc, target, cpu, opt_level,
                size_level, verbose=False):
    """Stage 2: WASM → AOT via wamrc."""
    cmd = [
        wamrc,
        f'--target={target}',
        f'--cpu={cpu}',
        f'--opt-level={opt_level}',
        f'--size-level={size_level}',
        '--emit-custom-sections=.akira.manifest',
        '-o', aot_output,
        wasm_path,
    ]

    log(f'  Stage 2: WASM → AOT', verbose)
    log(f'  {" ".join(cmd)}', verbose)

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'AOT compilation FAILED:')
        print(result.stderr)
        err('wamrc failed')
    if verbose and result.stdout:
        print(result.stdout, end='')


def verify_aot(aot_path, verbose=False):
    """Quick sanity check on the AOT binary."""
    with open(aot_path, 'rb') as f:
        magic = f.read(4)
    if magic != b'\x00aot':
        err(f'Output file has wrong magic: {magic!r} (expected \\x00aot)')

    size = os.path.getsize(aot_path)

    # Check for embedded manifest
    with open(aot_path, 'rb') as f:
        data = f.read()
    has_manifest = b'.akira.manifest' in data

    log(f'  AOT binary verified: {size:,} bytes, '
        f'manifest={"yes" if has_manifest else "MISSING"}', verbose)

    if not has_manifest:
        print('WARNING: .akira.manifest section not found in AOT binary.')
        print('  The binary will load with default capabilities (none).')

    return size, has_manifest


# ── Main ──────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Convert retro game ROMs to AkiraOS AOT binaries.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument('rom', help='Input ROM file')
    parser.add_argument('-o', '--output', help='Output AOT file')
    parser.add_argument('-p', '--platform', default='auto',
                        help='Target platform (default: auto-detect)')
    parser.add_argument('-n', '--name', help='App name in manifest')
    parser.add_argument('--wasi-sdk', default='/opt/wasi-sdk',
                        help='WASI SDK path (default: /opt/wasi-sdk)')
    parser.add_argument('--wamrc', help='Path to wamrc binary')
    parser.add_argument('--sdk-root', help='Path to AkiraSDK root')
    parser.add_argument('--target', default='xtensa',
                        help='AOT target architecture (default: xtensa)')
    parser.add_argument('--cpu', default='esp32s3',
                        help='AOT target CPU (default: esp32s3)')
    parser.add_argument('--opt-level', type=int, default=3, choices=[0,1,2,3],
                        help='Optimization level (default: 3)')
    parser.add_argument('--size-level', type=int, default=0, choices=[0,1,2,3],
                        help='Code model: 0=large,1=medium,2=kernel,3=small (default: 0)')
    parser.add_argument('--keep-wasm', action='store_true',
                        help='Keep intermediate .wasm file')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')
    args = parser.parse_args()

    verbose = args.verbose

    # ── Validate ROM ──────────────────────────────────────────────────
    rom_path = os.path.abspath(args.rom)
    if not os.path.isfile(rom_path):
        err(f'ROM file not found: {rom_path}')

    rom_name = os.path.splitext(os.path.basename(rom_path))[0]
    rom_size = os.path.getsize(rom_path)
    log(f'ROM: {rom_path} ({rom_size:,} bytes)', verbose)

    # ── Find tools ────────────────────────────────────────────────────
    sdk_root = args.sdk_root or find_sdk_root()
    if not sdk_root:
        err('Cannot locate AkiraSDK root. Use --sdk-root.')

    wamrc = args.wamrc or find_wamrc()
    if not wamrc:
        err('wamrc not found. Install the WAMR AOT compiler or use --wamrc.\n'
            'You can also set the WAMRC environment variable.\n'
            'Build wamrc from: https://github.com/bytecodealliance/wasm-micro-runtime')
    log(f'wamrc: {wamrc}', verbose)

    # ── Output paths ──────────────────────────────────────────────────
    output_aot = args.output or f'{rom_name}.aot'
    output_aot = os.path.abspath(output_aot)

    # ── Build ─────────────────────────────────────────────────────────
    build_dir = tempfile.mkdtemp(prefix='akira_aot_')
    wasm_path = os.path.join(build_dir, f'{rom_name}.wasm')

    try:
        # Stage 1: ROM → WASM
        build_wasm(rom_path, wasm_path, args.platform, args.name,
                   args.wasi_sdk, sdk_root, verbose)

        if not os.path.isfile(wasm_path):
            err('WASM file was not produced')

        wasm_size = os.path.getsize(wasm_path)
        log(f'  WASM: {wasm_size:,} bytes', verbose)

        # Stage 2: WASM → AOT
        aot_tmp = os.path.join(build_dir, f'{rom_name}.aot')
        compile_aot(wasm_path, aot_tmp, wamrc, args.target, args.cpu,
                    args.opt_level, args.size_level, verbose)

        shutil.copy(aot_tmp, output_aot)
        aot_size, has_manifest = verify_aot(output_aot, verbose)

        # Optionally keep .wasm
        if args.keep_wasm:
            keep_path = os.path.splitext(output_aot)[0] + '.wasm'
            shutil.copy(wasm_path, keep_path)
            log(f'  Kept: {keep_path}', verbose)

        # ── Summary ──────────────────────────────────────────────────
        print()
        print(f'  {output_aot}')
        print(f'  ROM size : {rom_size:,} bytes')
        print(f'  WASM size: {wasm_size:,} bytes')
        print(f'  AOT size : {aot_size:,} bytes')
        print(f'  Target   : {args.target} / {args.cpu}')
        print(f'  Manifest : {"embedded" if has_manifest else "MISSING"}')
        print()
        print(f'Deploy .aot file to AkiraOS via USB, BLE, or SD card.')

    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == '__main__':
    main()
