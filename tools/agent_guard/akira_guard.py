#!/usr/bin/env python3
# Copyright (c) 2026 PenEngineering S.R.L
# SPDX-License-Identifier: Apache-2.0
"""akira-guard: ask a human to approve an AI agent's action on an AkiraConsole.

Talks to the Agent Guard app over the LAN (HTTP, port 8787), shows the
request on the console, waits for the owner to hold A+B (approve) or press X
(deny), and checks the answer's ECDSA P-256 signature against the device key
pinned at pairing time.  Standard library only (Python 3.8+).

    akira-guard pair 192.168.1.42          # once: pin the key, store the code
    akira-guard ask "git push --force origin main" --action Bash --risk high
    akira-guard run --summary "deploy web to prod" -- ./deploy.sh
    akira-guard verify receipt.json        # offline, e.g. on the deploy server

Library:
    from akira_guard import Guard
    receipt = Guard.from_config().approve("Delete bucket logs-2024", action="aws s3 rb")
    if receipt.approved: ...

Exit codes (ask/run): 0 approved and verified, 1 denied / timed out /
withdrawn, 2 error (no device, bad signature, not paired).
"""

import argparse
import base64
import getpass
import hashlib
import hmac
import http.client
import json
import os
import secrets
import subprocess
import sys
import time

PROTOCOL = 1
DEFAULT_PORT = 8787
MAGIC = "AKIRA-AGENT-GUARD/1"


def config_path():
    env = os.environ.get("AKIRA_GUARD_CONFIG")
    if env:
        return env
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "akira-guard", "device.json")


# ── ECDSA P-256 verification (pure Python) ────────────────────────────────
_P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
_A = _P - 3
_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
_N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
_G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
      0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def _add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    (x1, y1), (x2, y2) = p1, p2
    if x1 == x2:
        if (y1 + y2) % _P == 0:
            return None
        lam = (3 * x1 * x1 + _A) * pow(2 * y1, -1, _P) % _P
    else:
        lam = (y2 - y1) * pow(x2 - x1, -1, _P) % _P
    x3 = (lam * lam - x1 - x2) % _P
    return x3, (lam * (x1 - x3) - y1) % _P


def _mul(k, pt):
    acc = None
    while k:
        if k & 1:
            acc = _add(acc, pt)
        pt = _add(pt, pt)
        k >>= 1
    return acc


def _on_curve(pt):
    x, y = pt
    return 0 <= x < _P and 0 <= y < _P and (y * y - (x * x * x + _A * x + _B)) % _P == 0


def ecdsa_p256_verify(pub65: bytes, message: bytes, sig64: bytes) -> bool:
    """Verify a raw r||s ECDSA P-256/SHA-256 signature over message."""
    if len(pub65) != 65 or pub65[0] != 4 or len(sig64) != 64:
        return False
    q = (int.from_bytes(pub65[1:33], "big"), int.from_bytes(pub65[33:], "big"))
    if not _on_curve(q):
        return False
    r, s = int.from_bytes(sig64[:32], "big"), int.from_bytes(sig64[32:], "big")
    if not (1 <= r < _N and 1 <= s < _N):
        return False
    e = int.from_bytes(hashlib.sha256(message).digest(), "big")
    w = pow(s, -1, _N)
    pt = _add(_mul(e * w % _N, _G), _mul(r * w % _N, q))
    return pt is not None and pt[0] % _N == r


# ── Protocol helpers ──────────────────────────────────────────────────────
def fingerprint(pub65: bytes) -> str:
    """Same fingerprint the console shows: first 8 bytes of SHA-256(pub)."""
    h = hashlib.sha256(pub65).hexdigest()[:16].upper()
    return " ".join(h[i:i + 4] for i in range(0, 16, 4))


def request_code(body: bytes) -> str:
    """Short code the console shows on the request card (#XXXX-XXXX)."""
    h = hashlib.sha256(body).hexdigest()[:8].upper()
    return h[:4] + "-" + h[4:]


def pair_key(code: str) -> bytes:
    code = code.strip().upper().replace("-", "").replace(" ", "")
    return hashlib.sha256(b"akira-agent-guard/pair\n" + code.encode()).digest()


def signed_message(decision, counter, t, request_sha256_hex) -> bytes:
    return (f"{MAGIC}\ndecision:{decision}\ncounter:{counter}\ntime:{t}\n"
            f"request:{request_sha256_hex}\n").encode()


class GuardError(Exception):
    pass


class Receipt:
    """A signed answer from the console, plus the request it answers."""

    def __init__(self, request_body: bytes, response: dict, device: dict):
        self.request_body = request_body
        self.response = response
        self.device = device
        self.verified, self.reason = verify_receipt(request_body, response, device.get("pub", ""))

    @property
    def decision(self):
        return self.response.get("decision", "error")

    @property
    def approved(self):
        """True only for a signed, verified approval of this exact request."""
        return self.verified and self.decision == "approved"

    def to_dict(self):
        return {
            "request": json.loads(self.request_body),
            "request_body_b64": base64.b64encode(self.request_body).decode(),
            "response": self.response,
            "device": {k: self.device.get(k) for k in ("pub", "fp", "key")},
            "verified": self.verified,
            "reason": self.reason,
        }

    def save(self, path):
        with open(path, "w") as f:
            json.dump(self.to_dict(), f, indent=2)


def verify_receipt(request_body: bytes, response: dict, pub_hex: str):
    """(ok, reason): the response is a valid signature by pub over this request."""
    try:
        pub = bytes.fromhex(pub_hex)
    except ValueError:
        return False, "no pinned public key"
    rh = hashlib.sha256(request_body).hexdigest()
    if response.get("request_sha256") != rh:
        return False, "response is for a different request"
    sig_hex = response.get("sig") or ""
    if not sig_hex:
        return False, response.get("error") or "unsigned answer (" + str(response.get("decision")) + ")"
    decision = response.get("decision")
    if decision not in ("approved", "denied"):
        return False, "unexpected decision " + repr(decision)
    msg = signed_message(decision, response.get("counter"), response.get("time"), rh)
    try:
        sig = bytes.fromhex(sig_hex)
    except ValueError:
        return False, "malformed signature"
    if not ecdsa_p256_verify(pub, msg, sig):
        return False, "BAD SIGNATURE (wrong device key or tampered answer)"
    return True, "signed by device " + fingerprint(pub)


def verify_receipt_file(path, pub_hex=None):
    with open(path) as f:
        r = json.load(f)
    body = base64.b64decode(r["request_body_b64"])
    pub_hex = pub_hex or r.get("device", {}).get("pub", "")
    ok, reason = verify_receipt(body, r["response"], pub_hex)
    return ok and r["response"].get("decision") == "approved", ok, reason, r


# ── Client ────────────────────────────────────────────────────────────────
class Guard:
    def __init__(self, host, port=DEFAULT_PORT, code=None, pub=None, fp=None, key=None):
        self.host, self.port = host, int(port)
        self.code, self.pub, self.fp, self.key = code, pub, fp, key

    @classmethod
    def from_config(cls, path=None):
        path = path or config_path()
        cfg = {}
        if os.path.exists(path):
            with open(path) as f:
                cfg = json.load(f)
        host = os.environ.get("AKIRA_GUARD_HOST", cfg.get("host"))
        if not host:
            raise GuardError("not paired: run `akira-guard pair <console-ip>` first")
        return cls(host, os.environ.get("AKIRA_GUARD_PORT", cfg.get("port", DEFAULT_PORT)),
                   os.environ.get("AKIRA_GUARD_CODE", cfg.get("code")),
                   os.environ.get("AKIRA_GUARD_PUB", cfg.get("pub")), cfg.get("fp"), cfg.get("key"))

    def save_config(self, path=None):
        path = path or config_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)  # holds the pairing code
        with os.fdopen(fd, "w") as f:
            json.dump({"host": self.host, "port": self.port, "code": self.code,
                       "pub": self.pub, "fp": self.fp, "key": self.key}, f, indent=2)

    def _http(self, method, path, body=b"", timeout=10):
        headers = {"Connection": "close"}
        if method == "POST":
            if not self.code:
                raise GuardError("no pairing code configured")
            mac = hmac.new(pair_key(self.code), f"{method} {path}\n".encode() + body, hashlib.sha256)
            headers["X-Akira-Auth"] = mac.hexdigest()
            headers["Content-Type"] = "application/json"
        try:
            conn = http.client.HTTPConnection(self.host, self.port, timeout=timeout)
            conn.request(method, path, body=body if method == "POST" else None, headers=headers)
            resp = conn.getresponse()
            data = resp.read()
            conn.close()
        except (OSError, http.client.HTTPException) as e:
            raise GuardError(f"cannot reach Agent Guard at {self.host}:{self.port}: {e}") from e
        try:
            obj = json.loads(data or b"{}")
        except ValueError:
            raise GuardError(f"bad reply from device (HTTP {resp.status})")
        if resp.status == 401:
            raise GuardError("the console refused the pairing code: run `akira-guard pair` again")
        if resp.status != 200:
            raise GuardError(f"device said HTTP {resp.status}: {obj.get('error', obj)}")
        return obj

    def info(self):
        return self._http("GET", "/v1/info")

    def ping(self):
        return self._http("POST", "/v1/ping", json.dumps({"nonce": secrets.token_hex(8)}).encode())

    def approve(self, summary, action="Action", details="", risk="medium", agent=None,
                timeout=120, on_waiting=None, **extra):
        """Show a request on the console and wait for the owner's answer.

        Returns a Receipt; check receipt.approved (signed + verified)."""
        if not self.pub:
            raise GuardError("no pinned device key: run `akira-guard pair` first")
        req = {"agent": agent or os.environ.get("AKIRA_GUARD_AGENT", "AI agent"), "action": action,
               "summary": summary, "details": details, "risk": risk,
               "nonce": secrets.token_hex(16), "time": int(time.time()),
               "timeout": int(timeout)}
        req.update(extra)
        body = json.dumps(req, separators=(",", ":"), ensure_ascii=True).encode()
        if on_waiting:
            on_waiting(request_code(body))
        resp = self._http("POST", "/v1/approve", body, timeout=int(timeout) + 30)
        return Receipt(body, resp, {"pub": self.pub, "fp": self.fp, "key": self.key})


# ── CLI ───────────────────────────────────────────────────────────────────
def _split_host(s):
    if ":" in s:
        h, p = s.rsplit(":", 1)
        return h, int(p)
    return s, DEFAULT_PORT


def cmd_pair(a):
    host, port = _split_host(a.host)
    g = Guard(host, port)
    info = g.info()
    if info.get("app") != "agent_guard":
        raise GuardError("that device is not running Agent Guard")
    pub = bytes.fromhex(info["pub"])
    fp = fingerprint(pub)
    if fp != info.get("fp"):
        raise GuardError("device fingerprint does not match its key")
    print(f"Agent Guard {info.get('version')} at {host}:{port}")
    print(f"  key:         {'SE050 secure element' if info.get('key') == 'se050' else 'SOFTWARE key (no secure element)'}")
    print(f"  fingerprint: {fp}")
    print("Check that the console's Pair screen (press X) shows the same fingerprint.")
    code = a.code or getpass.getpass("Pairing code shown on the console: ")
    g.code, g.pub, g.fp, g.key = code.strip().upper(), info["pub"], fp, info.get("key")
    g.ping()
    g.save_config(a.config)
    print(f"Paired. Settings saved to {a.config or config_path()}")
    return 0


def cmd_status(a):
    g = Guard.from_config(a.config)
    info = g.info()
    print(f"{g.host}:{g.port}  Agent Guard {info.get('version')}  key={info.get('key')}  fp={info.get('fp')}"
          f"  busy={info.get('busy')}")
    if info.get("pub") != g.pub:
        print("WARNING: the device key changed since pairing (re-flashed or another device). Pair again.")
        return 2
    g.ping()
    print("Pairing code accepted.")
    return 0


def _ask(a, summary):
    g = Guard.from_config(a.config)

    def waiting(code):
        if not a.quiet:
            print(f"Waiting for approval on the console (request #{code}, {a.timeout}s)...", file=sys.stderr)

    r = g.approve(summary, action=a.action, details=a.details or "", risk=a.risk, agent=a.agent,
                  timeout=a.timeout, on_waiting=waiting)
    if a.receipt:
        r.save(a.receipt)
    if a.json:
        print(json.dumps(r.to_dict(), indent=2))
    elif not a.quiet:
        if r.approved:
            print(f"APPROVED on the console - {r.reason}", file=sys.stderr)
        elif r.verified:
            print("DENIED on the console", file=sys.stderr)
        else:
            print(f"NOT APPROVED: {r.decision} ({r.reason})", file=sys.stderr)
    if r.approved:
        return 0
    return 1 if r.decision in ("denied", "timeout", "cancelled") and (r.verified or not r.response.get("sig")) else 2


def cmd_ask(a):
    return _ask(a, a.summary)


def cmd_run(a):
    if not a.command:
        raise GuardError("nothing to run: akira-guard run [--summary ...] -- COMMAND ...")
    summary = a.summary or " ".join(a.command)
    rc = _ask(a, summary)
    if rc != 0:
        return rc
    return subprocess.call(a.command)


def cmd_verify(a):
    # Trust the key pinned at pairing (or --pub), never the one inside the
    # receipt: a forged receipt could carry its own key.
    pub = a.pub or os.environ.get("AKIRA_GUARD_PUB")
    if not pub:
        try:
            pub = Guard.from_config(a.config).pub
        except GuardError:
            pub = None
    if not pub:
        print("akira-guard: no pinned key (pair first or pass --pub)", file=sys.stderr)
        return 2
    approved, ok, reason, r = verify_receipt_file(a.receipt, pub)
    req = r["request"]
    print(f"{'VALID' if ok else 'INVALID'}: {reason}")
    print(f"  decision: {r['response'].get('decision')}  counter: {r['response'].get('counter')}"
          f"  time: {r['response'].get('time')}")
    print(f"  {req.get('agent')} -> {req.get('action')}: {req.get('summary')}")
    return 0 if approved else 1


def main(argv=None):
    p = argparse.ArgumentParser(prog="akira-guard", description=__doc__.split("\n\n")[0])
    p.add_argument("--config", help=f"device settings file (default {config_path()})")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("pair", help="pair with a console running Agent Guard")
    s.add_argument("host", help="console IP (or IP:PORT), shown on the Agent Guard screen")
    s.add_argument("--code", help="pairing code (asked interactively if omitted)")
    s.set_defaults(fn=cmd_pair)

    s = sub.add_parser("status", help="check the paired console")
    s.set_defaults(fn=cmd_status)

    for name, fn, hlp in (("ask", cmd_ask, "ask for approval and wait"),
                          ("run", cmd_run, "ask, then run a command if approved")):
        s = sub.add_parser(name, help=hlp)
        if name == "ask":
            s.add_argument("summary", help="what will happen, in plain words (shown on the console)")
        else:
            s.add_argument("--summary", help="defaults to the command line")
            s.add_argument("command", nargs=argparse.REMAINDER)
        s.add_argument("--action", default="Run command" if name == "run" else "Action",
                       help="short title, e.g. Bash, Deploy, Payment")
        s.add_argument("--details", help="longer text the owner can scroll through")
        s.add_argument("--risk", choices=("low", "medium", "high"), default="medium")
        s.add_argument("--agent", help="who is asking (default $AKIRA_GUARD_AGENT or 'AI agent')")
        s.add_argument("--timeout", type=int, default=120, help="seconds to wait (10-600)")
        s.add_argument("--receipt", help="save the signed receipt to this file")
        s.add_argument("--json", action="store_true", help="print the receipt as JSON")
        s.add_argument("-q", "--quiet", action="store_true")
        s.set_defaults(fn=fn)

    s = sub.add_parser("verify", help="verify a saved receipt offline")
    s.add_argument("receipt")
    s.add_argument("--pub", help="device public key (hex); default: the key pinned by `pair`")
    s.set_defaults(fn=cmd_verify)

    a = p.parse_args(argv)
    if getattr(a, "command", None) and a.command[:1] == ["--"]:
        a.command = a.command[1:]
    try:
        return a.fn(a)
    except GuardError as e:
        print(f"akira-guard: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
