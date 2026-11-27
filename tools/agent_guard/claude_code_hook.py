#!/usr/bin/env python3
# Copyright (c) 2026 PenEngineering S.R.L
# SPDX-License-Identifier: Apache-2.0
"""Claude Code PreToolUse hook: risky tool calls need a yes on the AkiraConsole.

Register it in .claude/settings.json (see README.md):

    "hooks": {"PreToolUse": [{"matcher": "*", "hooks": [{"type": "command",
        "command": "python3 /path/to/claude_code_hook.py", "timeout": 300}]}]}

Ordinary calls pass straight through (no output: Claude Code's own
permission rules apply).  A risky call is shown on the console; the hook then
answers "allow" only for a signed, verified approval of that exact call, and
"deny" otherwise, with the reason shown to Claude.

Environment:
  AKIRA_GUARD_HOOK_MODE   risky (default) | all  — which calls need approval
  AKIRA_GUARD_ON_ERROR    deny (default) | ask   — console off or unreachable
  AKIRA_GUARD_POLICY      JSON file with extra "bash" regexes / "tools" regexes
  AKIRA_GUARD_RECEIPTS    directory to keep signed receipts in (optional)
"""

import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from akira_guard import Guard, GuardError  # noqa: E402

# Shell commands that are hard or impossible to undo.
RISKY_BASH = [
    r"\brm\s+(-[a-zA-Z]*[rf][a-zA-Z]*\s+)+",            # rm -rf / rm -r / rm -f
    r"\bgit\s+push\b.*(\s--force\b|\s-f\b|\s--force-with-lease\b|\s--delete\b|\s:\S)",
    r"\bgit\s+push\b.*\b(main|master|prod|production|release)\b",
    r"\bgit\s+(reset\s+--hard|clean\s+-[a-z]*f|branch\s+-D|filter-branch|filter-repo)\b",
    r"\bgit\s+(rebase|commit\s+--amend)\b.*\b(main|master)\b",
    r"\b(terraform|tofu)\s+(apply|destroy)\b",
    r"\bkubectl\s+(delete|apply|replace|drain|scale)\b",
    r"\bhelm\s+(install|upgrade|uninstall|delete|rollback)\b",
    r"\b(aws|gcloud|az|doctl)\b.*\b(delete|terminate|destroy|remove|rb|purge)\b",
    r"\b(npm|pnpm|yarn)\s+publish\b|\bcargo\s+publish\b|\btwine\s+upload\b|\bgem\s+push\b",
    r"\bgh\s+(release\s+(create|delete)|pr\s+merge|repo\s+delete)\b",
    r"\b(vercel\b.*--prod|netlify\s+deploy\b.*--prod|fly\s+deploy|firebase\s+deploy|wrangler\s+(deploy|publish))",
    r"\bdocker\s+(system\s+prune|volume\s+(rm|prune)|rm\s+-f)\b",
    r"\b(drop\s+(table|database|schema)|truncate\s+table)\b",
    r"\bdelete\s+from\s+\w+\s*(;|$)",                    # DELETE without WHERE
    r"(curl|wget)\b[^|]*\|\s*(sudo\s+)?(sh|bash|zsh|python3?)\b",
    r"\b(mkfs(\.\w+)?|dd\s+.*\bof=/dev/|shutdown|reboot|halt)\b",
    r"\bchmod\s+-R\s+777\b|\bchown\s+-R\b.*\s/(\s|$)",
    r"\b(west\s+flash|esptool(\.py)?\b.*\b(write_flash|erase_flash))\b",
    r"\./build\.sh\b.*\s-(r|e)\b",                        # AkiraOS: flash / erase hardware
    r"\bsudo\b",
]

# Non-shell tools that always need approval (regex on the tool name),
# e.g. MCP servers that move money or deploy.
RISKY_TOOLS = [
    r"^mcp__.*(deploy|delete|destroy|drop|payment|pay|transfer|send_money|refund|publish|merge).*$",
]


def load_policy():
    bash, tools = list(RISKY_BASH), list(RISKY_TOOLS)
    path = os.environ.get("AKIRA_GUARD_POLICY")
    if path and os.path.exists(path):
        with open(path) as f:
            pol = json.load(f)
        bash += pol.get("bash", [])
        tools += pol.get("tools", [])
    return [re.compile(p, re.I) for p in bash], [re.compile(p, re.I) for p in tools]


def needs_approval(tool, tinput):
    if os.environ.get("AKIRA_GUARD_HOOK_MODE", "risky") == "all":
        return True
    bash, tools = load_policy()
    if tool == "Bash":
        cmd = str(tinput.get("command", ""))
        return any(p.search(cmd) for p in bash)
    return any(p.search(tool) for p in tools)


def describe(tool, tinput, cwd):
    """(action, summary, details) for the console card."""
    if tool == "Bash":
        cmd = str(tinput.get("command", ""))
        details = f"in {cwd}"
        if tinput.get("description"):
            details = f"{tinput['description']}\n{details}"
        return "Bash", cmd[:250], details[:1000]
    summary = tinput.get("description") or tinput.get("file_path") or tinput.get("url") or ""
    details = json.dumps(tinput, indent=1, ensure_ascii=True)
    if not summary:
        summary, details = details, ""
    return tool[:38], str(summary)[:250], details[:1000]


def out(decision, reason):
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse",
                                             "permissionDecision": decision,
                                             "permissionDecisionReason": reason}}))
    return 0


def main():
    try:
        event = json.load(sys.stdin)
    except ValueError:
        return 0  # not for us
    tool = event.get("tool_name", "")
    tinput = event.get("tool_input") or {}
    if not needs_approval(tool, tinput):
        return 0
    action, summary, details = describe(tool, tinput, event.get("cwd", ""))
    on_error = os.environ.get("AKIRA_GUARD_ON_ERROR", "deny")
    try:
        g = Guard.from_config()
        r = g.approve(summary, action=action, details=details, risk="high", agent="Claude Code",
                      timeout=int(os.environ.get("AKIRA_GUARD_TIMEOUT", "120")),
                      on_waiting=lambda code: print(f"Agent Guard: approve #{code} on the console",
                                                    file=sys.stderr))
    except GuardError as e:
        if on_error == "ask":
            return out("ask", f"AkiraConsole unavailable ({e}); confirm here instead.")
        return out("deny", f"Blocked: this action needs approval on the AkiraConsole, "
                           f"which is unavailable ({e}). Ask the user to open Agent Guard.")
    rdir = os.environ.get("AKIRA_GUARD_RECEIPTS")
    if rdir:
        os.makedirs(rdir, exist_ok=True)
        r.save(os.path.join(rdir, f"{int(time.time())}-{r.response.get('counter', 0)}.json"))
    if r.approved:
        return out("allow", f"Approved by the user on AkiraConsole ({r.reason}).")
    if r.decision == "denied" and r.verified:
        return out("deny", "The user DENIED this on the AkiraConsole. Do not retry it; "
                           "ask the user what they want instead.")
    return out("deny", f"Not approved on the AkiraConsole ({r.decision}: {r.reason}).")


if __name__ == "__main__":
    sys.exit(main())
