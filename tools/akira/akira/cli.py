"""
akira — AkiraOS developer CLI.

Scaffold, build, deploy and manage sandboxed WebAssembly apps on AkiraOS devices.
Device calls use the on-device REST API (port 8080); discovery uses the `_akiraos._tcp`
mDNS service the firmware advertises.

Commands:
  new      scaffold a new app (C / Rust / Python)
  build    build the app in the current directory to a .wasm
  deploy   install the built app on a device, or open it in the simulator (--sim/--web)
  devices  discover AkiraOS devices on the LAN over mDNS
  info     print a device's /api/v1/info
  logs     fetch a device's recent logs
  apps     list / start / stop / remove apps on a device
  publish  submit a packaged .akpkg to the AkiraConsole catalogue
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from pathlib import Path

__version__ = "1.6.7"

DEVICE_PORT = 8080
SIM_URL = "https://console.app.akiraos.dev/simulator"
CATALOGUE_SUBMIT_URL = "https://console.app.akiraos.dev/api/submit"
TEMPLATES = Path(__file__).parent / "templates"


# ── small utilities ──────────────────────────────────────────────────────────
def die(msg: str, code: int = 1) -> "NoReturn":  # type: ignore[name-defined]
    print(f"akira: error: {msg}", file=sys.stderr)
    raise SystemExit(code)


def info(msg: str) -> None:
    print(msg, file=sys.stderr)


def base_url(device: str) -> str:
    """Normalise a --device value into http://host:8080 ."""
    if device.startswith("http://") or device.startswith("https://"):
        return device.rstrip("/")
    host = device
    if ":" not in host:
        host = f"{host}:{DEVICE_PORT}"
    return f"http://{host}"


def http(method: str, url: str, data: bytes | None = None,
         ctype: str | None = None, timeout: int = 15):
    req = urllib.request.Request(url, data=data, method=method)
    if ctype:
        req.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except urllib.error.URLError as e:
        die(f"cannot reach {url}: {e.reason}")


def print_json(body: bytes) -> None:
    try:
        print(json.dumps(json.loads(body.decode("utf-8")), indent=2))
    except Exception:
        sys.stdout.write(body.decode("utf-8", "replace"))
        if not body.endswith(b"\n"):
            print()


def read_manifest_name(app_dir: Path) -> str:
    m = app_dir / "manifest.json"
    if m.is_file():
        try:
            return json.loads(m.read_text()).get("name") or app_dir.name
        except Exception:
            pass
    return app_dir.name


# ── new ──────────────────────────────────────────────────────────────────────
def cmd_new(args) -> int:
    dest = Path(args.name)
    if dest.exists():
        die(f"{dest} already exists")
    src = TEMPLATES / args.lang
    if not src.is_dir():
        die(f"no template for --lang {args.lang}")
    shutil.copytree(src, dest)
    # Fill the app name into manifest.json.
    man = dest / "manifest.json"
    if man.is_file():
        txt = man.read_text().replace("__APP_NAME__", dest.name)
        man.write_text(txt)
    for f in dest.rglob("*"):
        if f.is_file():
            t = f.read_text(errors="ignore")
            if "__APP_NAME__" in t:
                f.write_text(t.replace("__APP_NAME__", dest.name))
    info(f"Created {args.lang} app in ./{dest}/")
    info(f"Next: cd {dest} && akira build && akira deploy --sim")
    return 0


# ── build ────────────────────────────────────────────────────────────────────
def _sdk_scripts_dir() -> Path | None:
    # This package ships inside AkiraSDK at tools/akira/akira/; scripts/ is at the SDK root.
    cand = Path(__file__).resolve().parents[3] / "scripts"
    if (cand / "embed_manifest.py").is_file():
        return cand
    env = os.environ.get("AKIRA_SDK")
    if env and (Path(env) / "scripts" / "embed_manifest.py").is_file():
        return Path(env) / "scripts"
    return None


def _embed_manifest(wasm: Path, manifest: Path) -> None:
    scripts = _sdk_scripts_dir()
    if not scripts or not manifest.is_file():
        info("note: manifest not embedded (set AKIRA_SDK to the SDK checkout to embed it)")
        return
    subprocess.run([sys.executable, str(scripts / "embed_manifest.py"),
                    str(wasm), str(manifest), str(wasm)], check=True)
    info(f"Embedded manifest into {wasm.name}")


def cmd_build(args) -> int:
    d = Path(args.dir).resolve()
    out = Path(args.out) if args.out else None
    name = read_manifest_name(d)
    manifest = d / "manifest.json"

    if (d / "Cargo.toml").is_file():
        cargo = os.environ.get("CARGO", "cargo")
        subprocess.run([cargo, "build", "--release",
                        "--target", "wasm32-unknown-unknown"], cwd=d, check=True)
        built = next((d / "target/wasm32-unknown-unknown/release").glob("*.wasm"), None)
        if not built:
            die("cargo build produced no .wasm")
    elif list(d.glob("*.c")):
        wasi = os.environ.get("WASI_SDK", "/opt/wasi-sdk")
        clang = Path(wasi) / "bin" / "clang"
        if not clang.is_file():
            die(f"WASI SDK clang not found at {clang} (set WASI_SDK)")
        built = out or (d / f"{name}.wasm")
        srcs = [str(p) for p in d.glob("*.c")]
        cmd = [str(clang), "-target", "wasm32-unknown-unknown", "-nostdlib",
               "-fvisibility=hidden", "-Wl,--no-entry", "-Wl,--export=main",
               "-Wl,--allow-undefined", "-Wl,--strip-all",
               "-z", "stack-size=4096", "-Wl,--initial-memory=131072",
               "-O2", "-Wno-incompatible-library-redeclaration", "-o", str(built), *srcs]
        inc = _sdk_scripts_dir()
        if inc:
            cmd[-len(srcs)-2:-len(srcs)-2] = ["-I", str(inc.parent / "include")]
        subprocess.run(cmd, check=True)
    elif (d / "main.py").is_file():
        scripts = _sdk_scripts_dir()
        mp = os.environ.get("MICROPYTHON_WASM")
        if not scripts or not mp:
            die("Python build needs AKIRA_SDK and MICROPYTHON_WASM set")
        built = out or (d / f"{name}.wasm")
        subprocess.run([sys.executable, str(scripts / "py_to_wasm.py"),
                        str(d / "main.py"), str(built), "--runtime", mp], check=True)
    else:
        die(f"no buildable sources in {d} (expected *.c, Cargo.toml or main.py)")

    built = Path(built)
    if out and built != out:
        shutil.copy2(built, out)
        built = out
    _embed_manifest(built, manifest)
    info(f"Built {built}")
    print(built)
    return 0


# ── deploy ───────────────────────────────────────────────────────────────────
def _find_wasm(args) -> Path:
    if args.app:
        p = Path(args.app)
        if not p.is_file():
            die(f"{p} not found")
        return p
    cwd = Path.cwd()
    name = read_manifest_name(cwd)
    for c in (cwd / f"{name}.wasm", *cwd.glob("*.wasm"),
              *(cwd / "target/wasm32-unknown-unknown/release").glob("*.wasm")):
        if c.is_file():
            return c
    die("no .wasm found — run `akira build` first or pass a path")


def cmd_deploy(args) -> int:
    wasm = _find_wasm(args)
    name = args.name or read_manifest_name(Path(wasm).parent) or wasm.stem

    if args.sim or args.web:
        url = f"{SIM_URL}?url={urllib.parse.quote(args.url, safe='')}" if args.url else SIM_URL
        if args.url:
            info(f"Open in the browser simulator:\n  {url}")
        else:
            info("The simulator cannot read a local file over a URL. Open it and drag the "
                 f".wasm in:\n  {SIM_URL}\n  file: {wasm}")
        try:
            webbrowser.open(url)
        except Exception:
            pass
        return 0

    if not args.device:
        die("deploy to a device needs --device <ip> (or use --sim)")
    url = f"{base_url(args.device)}/api/apps/install?name={urllib.parse.quote(name)}"
    info(f"Installing {wasm.name} as '{name}' → {args.device}")
    status, body = http("POST", url, data=wasm.read_bytes(),
                        ctype="application/octet-stream", timeout=60)
    print_json(body)
    return 0 if 200 <= status < 300 else 1


# ── devices (mDNS) ───────────────────────────────────────────────────────────
def cmd_devices(args) -> int:
    try:
        from zeroconf import Zeroconf, ServiceBrowser  # type: ignore
    except ImportError:
        die("mDNS discovery needs the 'mdns' extra: pipx inject akira-dev zeroconf "
            "(or `pip install zeroconf`). Otherwise use --device.")
    import time
    found: dict[str, str] = {}

    class L:
        def add_service(self, zc, type_, nm):
            inf = zc.get_service_info(type_, nm, timeout=3000)
            if inf and inf.addresses:
                ip = ".".join(str(b) for b in inf.addresses[0])
                found[nm] = f"{ip}:{inf.port}"
        def update_service(self, *a):  # noqa: D401
            pass
        def remove_service(self, *a):
            pass

    zc = Zeroconf()
    info("Browsing _akiraos._tcp for %ds …" % args.timeout)
    ServiceBrowser(zc, "_akiraos._tcp.local.", L())
    try:
        time.sleep(args.timeout)
    finally:
        zc.close()
    if not found:
        info("No AkiraOS devices found. Is the device on the same network with WiFi up?")
        return 1
    for nm, addr in sorted(found.items()):
        print(f"{addr}\t{nm.split('.')[0]}")
    return 0


# ── info / logs ──────────────────────────────────────────────────────────────
def cmd_info(args) -> int:
    status, body = http("GET", f"{base_url(args.device)}/api/v1/info")
    print_json(body)
    return 0 if 200 <= status < 300 else 1


def cmd_logs(args) -> int:
    status, body = http("GET", f"{base_url(args.device)}/api/v1/logs", timeout=30)
    sys.stdout.write(body.decode("utf-8", "replace"))
    return 0 if 200 <= status < 300 else 1


# ── apps ─────────────────────────────────────────────────────────────────────
def cmd_apps(args) -> int:
    b = base_url(args.device)
    if args.action == "list":
        status, body = http("GET", f"{b}/api/v1/apps")
        print_json(body)
    elif args.action in ("start", "stop"):
        if not args.app:
            die(f"apps {args.action} needs an app name")
        q = urllib.parse.quote(args.app)
        status, body = http("POST", f"{b}/api/v1/apps/{args.action}?name={q}")
        print_json(body)
    elif args.action == "remove":
        if not args.app:
            die("apps remove needs an app name")
        q = urllib.parse.quote(args.app)
        status, body = http("DELETE", f"{b}/api/v1/apps?name={q}")
        print_json(body)
    else:
        die(f"unknown apps action: {args.action}")
    return 0 if 200 <= status < 300 else 1


# ── publish ──────────────────────────────────────────────────────────────────
def cmd_publish(args) -> int:
    pkg = Path(args.akpkg)
    if not pkg.is_file() or pkg.suffix != ".akpkg":
        die("publish expects a .akpkg file (build one with `akira-cli pack`)")
    boundary = "----akira" + os.urandom(8).hex()
    parts = [b"--" + boundary.encode(),
             b'Content-Disposition: form-data; name="package"; filename="' + pkg.name.encode() + b'"',
             b"Content-Type: application/octet-stream", b"", pkg.read_bytes()]
    if args.author:
        parts += [b"--" + boundary.encode(),
                  b'Content-Disposition: form-data; name="author"', b"", args.author.encode()]
    if args.description:
        parts += [b"--" + boundary.encode(),
                  b'Content-Disposition: form-data; name="description"', b"", args.description.encode()]
    parts += [b"--" + boundary.encode() + b"--", b""]
    data = b"\r\n".join(parts)
    info(f"Submitting {pkg.name} → {args.url}")
    status, body = http("POST", args.url, data=data,
                        ctype=f"multipart/form-data; boundary={boundary}", timeout=60)
    print_json(body)
    return 0 if 200 <= status < 300 else 1


# ── argument parser ──────────────────────────────────────────────────────────
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="akira", description=__doc__.strip().splitlines()[1])
    p.add_argument("--version", action="version", version=f"akira {__version__}")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("new", help="scaffold a new app")
    sp.add_argument("name")
    sp.add_argument("--lang", choices=["c", "rust", "python"], default="c")
    sp.set_defaults(func=cmd_new)

    sp = sub.add_parser("build", help="build the app in the current directory")
    sp.add_argument("--dir", default=".")
    sp.add_argument("-o", "--out")
    sp.set_defaults(func=cmd_build)

    sp = sub.add_parser("deploy", help="install on a device, or open in the simulator")
    sp.add_argument("app", nargs="?", help="path to a .wasm (default: auto-detect)")
    sp.add_argument("--device", help="device IP or host[:port]")
    sp.add_argument("--sim", action="store_true", help="open the browser simulator")
    sp.add_argument("--web", action="store_true", help="alias for --sim")
    sp.add_argument("--url", help="public URL of the .wasm for a simulator deep link")
    sp.add_argument("--name", help="install name (default: from manifest)")
    sp.set_defaults(func=cmd_deploy)

    sp = sub.add_parser("devices", help="discover AkiraOS devices over mDNS")
    sp.add_argument("--timeout", type=int, default=4)
    sp.set_defaults(func=cmd_devices)

    sp = sub.add_parser("info", help="print a device's info")
    sp.add_argument("--device", required=True)
    sp.set_defaults(func=cmd_info)

    sp = sub.add_parser("logs", help="fetch a device's recent logs")
    sp.add_argument("--device", required=True)
    sp.set_defaults(func=cmd_logs)

    sp = sub.add_parser("apps", help="manage apps on a device")
    sp.add_argument("action", choices=["list", "start", "stop", "remove"])
    sp.add_argument("app", nargs="?")
    sp.add_argument("--device", required=True)
    sp.set_defaults(func=cmd_apps)

    sp = sub.add_parser("publish", help="submit a .akpkg to the catalogue")
    sp.add_argument("akpkg")
    sp.add_argument("--url", default=CATALOGUE_SUBMIT_URL)
    sp.add_argument("--author")
    sp.add_argument("--description")
    sp.set_defaults(func=cmd_publish)

    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except KeyboardInterrupt:
        return 130
    except subprocess.CalledProcessError as e:
        die(f"command failed: {' '.join(str(x) for x in e.cmd)} (exit {e.returncode})")


if __name__ == "__main__":
    raise SystemExit(main())
