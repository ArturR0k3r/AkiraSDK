#!/usr/bin/env python3
"""
rom_to_wasm.py — Convert retro game ROM images to AkiraOS WASM binaries.

Supported platforms:
  .nes  — Nintendo Entertainment System
  .sms  — Sega Master System
  .gg   — Game Gear
  .gb   — Game Boy
  .gbc  — Game Boy Color
  .a26  — Atari 2600         (future)
  .sfc  — Super Nintendo     (future)
  .smc  — Super Nintendo     (future)

Usage:
  python3 rom_to_wasm.py <rom_file> [options]

Options:
  -o, --output <file>      Output WASM file (default: <rom_name>.wasm)
  -p, --platform <name>    Platform: auto|nes|gb|sms|atari|snes (default: auto)
  -n, --name <name>        App name embedded in manifest (default: rom filename)
  --wasi-sdk <path>        Path to WASI SDK (default: /opt/wasi-sdk)
  --sdk-root <path>        Path to AkiraSDK root (default: auto-detect)
  --gen-rom-only           Only generate rom_data.c, do not compile
  --dry-run                Print compile command without executing
  -v, --verbose            Verbose output

Examples:
  python3 rom_to_wasm.py tetris.nes
  python3 rom_to_wasm.py tetris.nes -o tetris.wasm -n "NES Tetris"
  python3 rom_to_wasm.py donkey_kong.nes --wasi-sdk /opt/wasi-sdk-20
"""
import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

# ── Platform definitions ──────────────────────────────────────────────

PLATFORMS = {
    'nes': {
        'extensions': ['.nes'],
        'magic': b'NES\x1a',
        'magic_offset': 0,
        'template_dir': 'nes',
        'sources': ['main.c', 'nes.c', 'cpu6502.c', 'ppu.c', 'mapper.c'],
        'stack_size': 8192,
        'extra_memory': 200 * 1024,  # framebuffer + emulator state overhead
        'capabilities': ['display.write', 'gpio.read', 'input.read', 'app.switch'],
        'description': 'Nintendo Entertainment System',
    },
    'gb': {
        'extensions': ['.gb', '.gbc'],
        'magic': b'Nintendo',
        'magic_offset': 0x134,
        'template_dir': 'gb',
        'sources': ['main.c', 'gb.c', 'cpu.c', 'ppu.c'],
        'stack_size': 8192,
        # Overhead: GB struct ~460KB (VRAM 16KB + WRAM 32KB + cart RAM 128KB +
        #           OAM/HRAM + FB 46KB + I/O + code) + headroom
        'extra_memory': 512 * 1024,
        'capabilities': ['display.write', 'gpio.read', 'input.read', 'app.switch'],
        'description': 'Game Boy / Game Boy Color',
    },
    'sms': {
        'extensions': ['.sms', '.gg'],
        # SMS ROMs ≥32KB have "TMR SEGA" header at 0x7FF0.  Smaller homebrew
        # ROMs may omit it, so we accept any .sms/.gg by extension alone but
        # warn when the header is absent.
        'magic': b'TMR SEGA',
        'magic_offset': 0x7FF0,
        'template_dir': 'sms',
        'sources': ['main.c', 'sms.c', 'z80.c', 'vdp.c', 'mapper.c'],
        'stack_size': 8192,
        # Overhead breakdown:
        #   SMS machine struct: ~123 KB  (16 KB VRAM + 8 KB WRAM + 96 KB FB + state)
        #   Code + globals:     ~40 KB
        #   Headroom:           ~37 KB
        'extra_memory': 200 * 1024,
        'capabilities': ['display.write', 'gpio.read', 'input.read', 'app.switch'],
        'description': 'Sega Master System / Game Gear',
    },
    'atari': {
        'extensions': ['.a26', '.bin'],
        'magic': None,
        'magic_offset': 0,
        'template_dir': 'atari2600',
        'sources': ['main.c', 'atari.c'],
        'stack_size': 4096,
        'extra_memory': 50 * 1024,
        'capabilities': ['display.write', 'gpio.read', 'input.read', 'app.switch'],
        'description': 'Atari 2600',
    },
    'snes': {
        'extensions': ['.sfc', '.smc'],
        'magic': None,
        'magic_offset': 0,
        'template_dir': 'snes',
        'sources': ['main.c', 'snes.c'],
        'stack_size': 16384,
        'extra_memory': 300 * 1024,
        'capabilities': ['display.write', 'gpio.read', 'input.read', 'app.switch'],
        'description': 'Super Nintendo Entertainment System',
    },
}


# ── Helpers ───────────────────────────────────────────────────────────

def log(msg, verbose=True):
    if verbose:
        print(msg)


def err(msg):
    print(f'ERROR: {msg}', file=sys.stderr)
    sys.exit(1)


def detect_platform(rom_path):
    """Detect platform from file extension and magic bytes."""
    ext = os.path.splitext(rom_path)[1].lower()
    for name, info in PLATFORMS.items():
        if ext in info['extensions']:
            # Verify magic if defined
            if info['magic'] is not None:
                try:
                    with open(rom_path, 'rb') as f:
                        f.seek(info['magic_offset'])
                        data = f.read(len(info['magic']))
                    if data != info['magic']:
                        print(f'Warning: {rom_path} has extension {ext} but '
                              f'magic bytes do not match {name!r}.')
                except OSError:
                    pass
            return name
    return None


def next_wasm_page(n_bytes):
    """Round up to the next WebAssembly memory page boundary (64KB = 65536 bytes)."""
    page = 65536
    pages = math.ceil(n_bytes / page)
    return pages * page


def calc_memory(rom_size, platform_info):
    """Calculate the required WASM --initial-memory for this ROM."""
    total = rom_size + platform_info['extra_memory'] + platform_info['stack_size']
    return next_wasm_page(total)


def generate_rom_data_c(rom_bytes, output_path, verbose=False):
    """Write rom_data.c containing the ROM as a C byte array."""
    log(f'  Generating {output_path} ({len(rom_bytes):,} bytes)...', verbose)

    with open(output_path, 'w') as f:
        f.write('/* Auto-generated by rom_to_wasm.py — DO NOT EDIT */\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'const uint32_t rom_size = {len(rom_bytes)}u;\n\n')
        f.write('const uint8_t rom_data[] = {\n')

        # Emit 16 bytes per line
        for i in range(0, len(rom_bytes), 16):
            chunk = rom_bytes[i:i + 16]
            hex_vals = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f'    {hex_vals},\n')

        f.write('};\n')


def find_sdk_root(script_dir):
    """Walk upward from the script to find AkiraSDK root (contains include/akira_api.h)."""
    path = script_dir
    for _ in range(8):
        candidate = os.path.join(path, 'include', 'akira_api.h')
        if os.path.isfile(candidate):
            return path
        path = os.path.dirname(path)
    return None


def generate_manifest(name, capabilities, memory_quota):
    """Return manifest JSON dict."""
    return {
        'name': name,
        'version': '1.0.0',
        'capabilities': capabilities,
        'memory_quota': memory_quota,
    }


def compile_wasm(sources, rom_data_c, output, wasi_sdk, include_dirs,
                 memory_bytes, stack_size, verbose=False, dry_run=False):
    """Invoke WASI SDK clang to compile the WASM binary."""
    cc = os.path.join(wasi_sdk, 'bin', 'clang')
    if not os.path.isfile(cc):
        err(f'WASI SDK clang not found at {cc}\n'
            f'Install WASI SDK or specify --wasi-sdk <path>')

    includes = [f'-I{d}' for d in include_dirs]

    cmd = [
        cc, '-O3', '-nostdlib',
        '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unknown-attributes',
    ] + includes + [
        '-Wl,--no-entry',
        '-Wl,--export=main',
        '-Wl,--allow-undefined',
        '-Wl,--strip-all',
        f'-z', f'stack-size={stack_size}',
        f'-Wl,--initial-memory={memory_bytes}',
        f'-Wl,--max-memory={memory_bytes}',
        '-o', output,
    ] + sources + [rom_data_c]

    log(f'  Compile command:', verbose)
    log('  ' + ' '.join(cmd), verbose)

    if dry_run:
        print('DRY RUN — command above would be executed.')
        return True

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print('Compilation FAILED:')
        print(result.stderr)
        return False
    if result.stderr:
        print(result.stderr, end='')
    return True


def embed_manifest(wasm_path, manifest_dict, embed_script, verbose=False):
    """Embed manifest JSON into the WASM custom section."""
    if not os.path.isfile(embed_script):
        log(f'  Warning: embed_manifest.py not found at {embed_script}; '
            'manifest NOT embedded.', verbose)
        return

    with tempfile.NamedTemporaryFile(mode='w', suffix='.json',
                                     delete=False) as f:
        json.dump(manifest_dict, f, indent=2)
        tmp_manifest = f.name

    try:
        result = subprocess.run(
            [sys.executable, embed_script, wasm_path, tmp_manifest, wasm_path],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print(f'Warning: manifest embedding failed: {result.stderr}')
        else:
            log('  Manifest embedded.', verbose)
    finally:
        os.unlink(tmp_manifest)


# ── Main ──────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Convert retro game ROM images to AkiraOS WASM binaries.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('rom', help='Input ROM file')
    parser.add_argument('-o', '--output', help='Output WASM file')
    parser.add_argument('-p', '--platform', default='auto',
                        choices=['auto'] + list(PLATFORMS.keys()),
                        help='Target platform (default: auto-detect)')
    parser.add_argument('-n', '--name', help='App name in manifest')
    parser.add_argument('--wasi-sdk', default='/opt/wasi-sdk',
                        help='Path to WASI SDK (default: /opt/wasi-sdk)')
    parser.add_argument('--sdk-root', help='Path to AkiraSDK root')
    parser.add_argument('--gen-rom-only', action='store_true',
                        help='Only generate rom_data.c; skip compilation')
    parser.add_argument('--dry-run', action='store_true',
                        help='Print compile command but do not execute')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')
    args = parser.parse_args()

    verbose = args.verbose or args.dry_run

    # ── Locate ROM ────────────────────────────────────────────────────
    rom_path = os.path.abspath(args.rom)
    if not os.path.isfile(rom_path):
        err(f'ROM file not found: {rom_path}')

    with open(rom_path, 'rb') as f:
        rom_bytes = f.read()

    rom_name = os.path.splitext(os.path.basename(rom_path))[0]
    log(f'ROM: {rom_path} ({len(rom_bytes):,} bytes)', verbose)

    # ── Detect platform ───────────────────────────────────────────────
    if args.platform == 'auto':
        platform_name = detect_platform(rom_path)
        if platform_name is None:
            err(f'Cannot auto-detect platform for {rom_path}.\n'
                f'Use --platform to specify: {", ".join(PLATFORMS.keys())}')
        log(f'Platform: {platform_name} (auto-detected)', verbose)
    else:
        platform_name = args.platform

    platform = PLATFORMS[platform_name]

    # ── Check template sources exist ──────────────────────────────────
    script_dir  = os.path.dirname(os.path.abspath(__file__))
    sdk_root    = args.sdk_root or find_sdk_root(script_dir)
    if sdk_root is None:
        err('Cannot locate AkiraSDK root (expected to contain include/akira_api.h).\n'
            'Use --sdk-root <path> to specify it manually.')

    templates_dir = os.path.join(sdk_root, 'wasm_apps', 'retro', 'cores',
                                  platform['template_dir'])
    if not os.path.isdir(templates_dir):
        err(f'Core directory not found: {templates_dir}\n'
            f'Platform {platform_name!r} emulator core has not been implemented yet.')

    # Verify all template sources exist
    for src in platform['sources']:
        src_path = os.path.join(templates_dir, src)
        if not os.path.isfile(src_path):
            err(f'Template source not found: {src_path}')

    # ── Output paths ──────────────────────────────────────────────────
    output_wasm  = args.output or f'{rom_name}.wasm'
    output_wasm  = os.path.abspath(output_wasm)
    app_name     = args.name or rom_name.replace(' ', '_').lower()

    # rom_data.c goes in a temp dir (cleaned up after build)
    build_dir    = tempfile.mkdtemp(prefix='akira_nes_')
    rom_data_c   = os.path.join(build_dir, 'rom_data.c')

    log(f'Output:   {output_wasm}', verbose)
    log(f'App name: {app_name}', verbose)

    try:
        # ── Generate rom_data.c ───────────────────────────────────────
        generate_rom_data_c(rom_bytes, rom_data_c, verbose)

        if args.gen_rom_only:
            import shutil
            dest = os.path.join(os.path.dirname(output_wasm), 'rom_data.c')
            shutil.copy(rom_data_c, dest)
            print(f'Generated: {dest}')
            return

        # ── Calculate WASM memory ─────────────────────────────────────
        memory_bytes = calc_memory(len(rom_bytes), platform)
        log(f'WASM memory: {memory_bytes // 1024} KB '
            f'(ROM {len(rom_bytes)//1024}KB + '
            f'overhead {platform["extra_memory"]//1024}KB + '
            f'stack {platform["stack_size"]//1024}KB, '
            f'rounded to {memory_bytes//65536} WASM pages)', verbose)

        # ── Build include paths ───────────────────────────────────────
        include_dirs = [
            templates_dir,
            os.path.join(sdk_root, 'include'),
        ]

        # ── Compile ───────────────────────────────────────────────────
        sources = [os.path.join(templates_dir, s) for s in platform['sources']]
        tmp_wasm = os.path.join(build_dir, 'out.wasm')

        ok = compile_wasm(
            sources=sources,
            rom_data_c=rom_data_c,
            output=tmp_wasm,
            wasi_sdk=args.wasi_sdk,
            include_dirs=include_dirs,
            memory_bytes=memory_bytes,
            stack_size=platform['stack_size'],
            verbose=verbose,
            dry_run=args.dry_run,
        )
        if not ok or args.dry_run:
            return

        # ── Generate and embed manifest ────────────────────────────────
        manifest = generate_manifest(
            name=app_name,
            capabilities=platform['capabilities'],
            memory_quota=memory_bytes,
        )

        embed_script = os.path.join(sdk_root, 'scripts', 'embed_manifest.py')

        import shutil
        shutil.copy(tmp_wasm, output_wasm)
        embed_manifest(output_wasm, manifest, embed_script, verbose)

        # ── Done ───────────────────────────────────────────────────────
        wasm_size = os.path.getsize(output_wasm)
        print(f'')
        print(f'  {output_wasm}')
        print(f'  Platform : {platform_name} ({platform["description"]})')
        print(f'  ROM size : {len(rom_bytes):,} bytes')
        print(f'  WASM size: {wasm_size:,} bytes')
        print(f'  Memory   : {memory_bytes // 1024} KB')
        print(f'')
        print(f'Deploy to AkiraOS via USB, BLE, or SD card.')

    finally:
        # Clean up temp build directory
        import shutil
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == '__main__':
    main()
