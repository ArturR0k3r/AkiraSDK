#!/usr/bin/env python3
# Copyright (c) 2026 PenEngineering S.R.L
# SPDX-License-Identifier: Apache-2.0
"""MCP server (stdio) exposing one tool: request_human_approval.

Any MCP client (Claude Code, Claude Desktop, Cursor, agent frameworks with
MCP support) can ask the owner to approve an action on the AkiraConsole:

    claude mcp add --transport stdio akira-guard -- python3 /path/to/mcp_server.py

The tool blocks until the owner answers on the console (or the timeout).
Hand-written JSON-RPC over newline-delimited stdio, standard library only.
Logs go to stderr; stdout carries protocol messages only.
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from akira_guard import Guard, GuardError  # noqa: E402

SUPPORTED = ("2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05")
SERVER_INFO = {"name": "akira-guard", "version": "1.0.0"}

TOOL = {
    "name": "request_human_approval",
    "title": "Ask the owner on the AkiraConsole",
    "description": (
        "Ask the human owner to approve an action on their AkiraConsole hardware button "
        "before doing anything irreversible or high-impact: deleting data, force-pushing, "
        "deploying, publishing, sending money or messages, changing production systems. "
        "Describe exactly what will happen; the owner reads it on the device screen. "
        "Only proceed if the result says APPROVED. If it says DENIED or TIMEOUT, do not "
        "perform the action and do not retry the same request; tell the user instead."),
    "inputSchema": {
        "type": "object",
        "properties": {
            "action": {"type": "string", "description": "Short title, e.g. 'Deploy', 'Delete files', 'Payment'."},
            "summary": {"type": "string", "description": "Exactly what will happen, in one or two plain sentences."},
            "details": {"type": "string", "description": "Optional longer context: commands, amounts, targets."},
            "risk": {"type": "string", "enum": ["low", "medium", "high"], "description": "How bad a mistake would be."},
        },
        "required": ["summary"],
        "additionalProperties": False,
    },
}


def log(*a):
    print("[akira-guard-mcp]", *a, file=sys.stderr, flush=True)


def send(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def call_tool(args):
    summary = str(args.get("summary", "")).strip()
    if not summary:
        return {"content": [{"type": "text", "text": "summary is required"}], "isError": True}
    try:
        g = Guard.from_config()
        r = g.approve(summary, action=str(args.get("action") or "Action"),
                      details=str(args.get("details") or ""), risk=str(args.get("risk") or "medium"),
                      agent=os.environ.get("AKIRA_GUARD_AGENT", "MCP agent"),
                      timeout=int(os.environ.get("AKIRA_GUARD_TIMEOUT", "120")),
                      on_waiting=lambda code: log(f"waiting for the owner (request #{code})"))
    except GuardError as e:
        return {"content": [{"type": "text", "text":
                f"NOT APPROVED: the AkiraConsole could not be reached ({e}). Do not perform the action."}],
                "isError": True}
    rdir = os.environ.get("AKIRA_GUARD_RECEIPTS")
    path = None
    if rdir:
        os.makedirs(rdir, exist_ok=True)
        path = os.path.join(rdir, f"{int(time.time())}-{r.response.get('counter', 0)}.json")
        r.save(path)
    if r.approved:
        text = f"APPROVED by the owner on the AkiraConsole ({r.reason}). You may proceed with exactly this action."
    elif r.decision == "denied" and r.verified:
        text = "DENIED by the owner on the AkiraConsole. Do not perform this action or retry it."
    elif r.decision == "timeout":
        text = "TIMEOUT: the owner did not answer. Do not perform this action."
    else:
        text = f"NOT APPROVED ({r.decision}: {r.reason}). Do not perform this action."
    if path:
        text += f" Receipt: {path}"
    return {"content": [{"type": "text", "text": text}],
            "structuredContent": {"approved": r.approved, "decision": r.decision, "verified": r.verified,
                                  "counter": r.response.get("counter"), "receipt": path},
            "isError": False}


def handle(msg):
    method, mid = msg.get("method"), msg.get("id")
    if mid is None:
        return  # notification (e.g. notifications/initialized): no reply
    if method == "initialize":
        want = (msg.get("params") or {}).get("protocolVersion")
        send({"jsonrpc": "2.0", "id": mid, "result": {
            "protocolVersion": want if want in SUPPORTED else SUPPORTED[0],
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
            "instructions": "Call request_human_approval before any irreversible or high-impact action."}})
    elif method == "ping":
        send({"jsonrpc": "2.0", "id": mid, "result": {}})
    elif method == "tools/list":
        send({"jsonrpc": "2.0", "id": mid, "result": {"tools": [TOOL]}})
    elif method == "tools/call":
        p = msg.get("params") or {}
        if p.get("name") != TOOL["name"]:
            send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": "unknown tool"}})
            return
        send({"jsonrpc": "2.0", "id": mid, "result": call_tool(p.get("arguments") or {})})
    else:
        send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"method not found: {method}"}})


def main():
    log("ready")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}})
            continue
        try:
            handle(msg)
        except Exception as e:  # keep serving
            log("error:", e)
            if msg.get("id") is not None:
                send({"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32603, "message": str(e)}})


if __name__ == "__main__":
    main()
