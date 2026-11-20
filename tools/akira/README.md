# `akira` — AkiraOS developer CLI

Scaffold, build, deploy and manage sandboxed WebAssembly apps on AkiraOS devices, from
idea to running app in a couple of commands.

> There are two CLIs, by design:
> - **`akira`** (this tool, Python / pipx) — the developer workflow: `new`, `build`,
>   `deploy`, `devices`, `logs`, `apps`, `publish`.
> - **`akira-cli`** (Go) — packaging and signing primitives: `pack`, `sign`, `verify`,
>   `install`, `update`. `akira` shells out to it for `.akpkg` packaging.

## Install

```bash
pipx install akira-dev                 # once published to PyPI
# or from a checkout:
pipx install ./AkiraSDK/tools/akira
# device discovery (optional):
pipx inject akira-dev zeroconf
```

## 60-second workflow

```bash
akira new myapp --lang c        # scaffold (also: --lang rust | python)
cd myapp
akira build                     # -> myapp.wasm  (needs WASI SDK / cargo)
akira deploy --sim              # open the browser simulator
akira devices                   # find a device on the LAN (mDNS)
akira deploy --device 192.168.1.42
```

## Commands

| Command | What it does |
|---------|--------------|
| `akira new <name> --lang c\|rust\|python` | Scaffold a new app from a template |
| `akira build [--dir .] [-o out.wasm]` | Build C (WASI SDK), Rust (`cargo`) or Python (MicroPython) to `.wasm`, embedding the manifest |
| `akira deploy [--device IP] [--sim\|--web] [--url URL]` | Install on a device (`POST /api/apps/install`), or open the [simulator](https://console.app.akiraos.dev/simulator) (with `--url` as a deep link) |
| `akira devices [--timeout 4]` | Discover `_akiraos._tcp` devices over mDNS (needs the `mdns` extra) |
| `akira info --device IP` | `GET /api/v1/info` |
| `akira logs --device IP` | `GET /api/v1/logs` |
| `akira apps list\|start\|stop\|remove [app] --device IP` | Manage installed apps (`/api/v1/apps*`) |
| `akira publish <app.akpkg>` | Submit to the [AkiraConsole catalogue](https://console.app.akiraos.dev) |

## Environment

| Variable | Used by | Default |
|----------|---------|---------|
| `WASI_SDK` | C builds | `/opt/wasi-sdk` |
| `CARGO` | Rust builds | `cargo` |
| `MICROPYTHON_WASM` | Python builds | — (required) |
| `AKIRA_SDK` | manifest embedding / Python packaging | auto-detected inside the SDK checkout |

Device calls use the REST API on port 8080; the firmware advertises itself on the LAN as
`_akiraos._tcp` (`CONFIG_AKIRA_MDNS`).
