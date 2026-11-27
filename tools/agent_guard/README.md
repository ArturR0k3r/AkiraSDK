# Agent Guard: a hardware approve button for your AI agent

Agent Guard turns an AkiraConsole into a desk device that stands between your
AI agent and anything it can't undo. When the agent wants to force-push,
deploy, delete data or send money, the request appears on the console screen.
You hold **A+B** to approve or press **X** to deny. The answer goes back to the
agent signed by a key that never leaves the console's **SE050 secure element**,
so the system that runs the action can check that a human really said yes.

This guide covers installing the app, pairing your computer, connecting it to
Claude Code, any MCP client, or your own agent code, and making approvals
binding on the server that executes the action.

## How it works

```
  your AI agent                     your computer                    AkiraConsole (Agent Guard)
 ┌────────────────┐  hook / MCP /  ┌──────────────┐  HTTP over Wi-Fi ┌──────────────────────────┐
 │ Claude Code,   │ ─────────────► │ akira-guard  │ ───────────────► │ shows the request        │
 │ MCP client,    │    CLI, lib    │ (Python,     │                  │ you: hold A+B  or  X     │
 │ your code      │ ◄───────────── │  stdlib only)│ ◄─────────────── │ SE050 signs the answer   │
 └────────────────┘   allow/deny   └──────────────┘  signed receipt  └──────────────────────────┘
                                          │
                                          ▼  receipt (optional, recommended for real enforcement)
                                   deploy server / CI / payment worker verifies the signature
```

- **Pairing**: the console shows an 8-character code and its key fingerprint.
  Your computer stores the code (used to authenticate requests) and pins the
  console's public key.
- **Requests** carry what will happen in plain words. The console shows them
  as printable ASCII only. Any other character is shown as `?`, so a
  lookalike-character trick is visible.
- **Answers** are ECDSA P-256 signatures over the decision, a counter, the
  time and the SHA-256 of the exact request bytes. A signed "approved" for
  one request can't be reused for another.
- **Fail closed**: if the console is off, unreachable or nobody answers, the
  answer is "not approved".

## What you need

- An **AkiraConsole Prod** running firmware built with the SE050 enabled for apps
  (`CONFIG_AKIRA_SE050_U2F=y`, set for `akiraconsole_prod` in
  `snippets/akira-reference`, branch `C0ns0l3-Firmware`). On other firmware or
  hardware Agent Guard still works, but signs with a **software key** and warns
  you on screen. That's fine for trying it out, but don't rely on it to protect
  real money or production systems.
- The console on **Wi-Fi** (Settings → Wi-Fi), on a network your computer can reach.
- **Python 3.8+** on the computer that runs your agent. No packages are needed.

## 1. Install the app on the console

```bash
cd AkiraSDK/wasm_apps
./build.sh agent_guard            # -> bin/agent_guard.wasm
```

Install `bin/agent_guard.wasm` the way you install other apps: copy it to the
SD card's `apps/` folder, or install it from the console shell. Then:

1. Connect the console to Wi-Fi in **Settings → Wi-Fi**.
2. Open **Agent Guard**. It shows `Waiting for agents` and its address, e.g. `192.168.1.42:8787`.
3. The top-right badge should read **SE050 key**. If it reads **SOFTWARE KEY**,
   see "What you need" above.

The first time a request is approved after the app starts, AkiraOS itself asks
you to let Agent Guard use the secure element (a **Seal**). Hold A+B for one
lap to allow it until the app closes, or for two laps to allow it always.

> Tip: give the console a fixed address (a DHCP reservation in your router),
> so the pairing keeps working after a reboot.

## 2. Pair your computer

```bash
ln -s "$PWD/tools/agent_guard/akira_guard.py" ~/.local/bin/akira-guard   # optional: shorter name
akira-guard pair 192.168.1.42
```

On the console, press **X** to open the Pair screen. Check that the fingerprint
printed on your computer matches the one on the console, then type the pairing
code. The settings go to `~/.config/akira-guard/device.json` (mode 0600, because
it holds the code).

Try it:

```bash
akira-guard ask "Hello from my agent" --action Test
```

The console shows the request and a short code (`#3FA2-9C1E`) that your terminal
also prints, so you can tell two requests apart. Hold A+B: the command prints
`APPROVED` and exits 0.

Pressing **Y** on the Pair screen makes a new code. That unpairs every computer
until you run `pair` again.

## 3. Connect your agent

### Claude Code: hook (recommended, enforced)

A `PreToolUse` hook runs before every tool call, so Claude can't skip it.
Ordinary calls pass straight through. Risky ones wait for the console. Add this
to `.claude/settings.json` in your project, or to `~/.claude/settings.json` for
every project:

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "python3 /absolute/path/to/AkiraSDK/tools/agent_guard/claude_code_hook.py",
            "timeout": 300
          }
        ]
      }
    ]
  }
}
```

- **What counts as risky**: shell commands such as `rm -rf`, `git push --force`
  or a push to `main`, `git reset --hard`, `terraform apply/destroy`,
  `kubectl delete/apply`, `npm publish`, `gh pr merge`, production deploys
  (Vercel, Fly, Firebase, Wrangler), `DROP TABLE`, `curl … | sh`, `sudo`, and
  flashing firmware. It also covers MCP tools whose names mention deploy,
  delete, payment, transfer, publish or merge. The full list is at the top of
  `claude_code_hook.py`.
- **Approved**: the hook answers `allow`, and Claude runs the command without
  asking again.
- **Denied** or **timed out**: the hook answers `deny`, and Claude is told you
  refused, so it won't retry.
- **Console unreachable**: the hook denies by default. Set
  `AKIRA_GUARD_ON_ERROR=ask` to fall back to Claude Code's own prompt.

Tuning, via environment variables:

| Variable | Meaning |
|---|---|
| `AKIRA_GUARD_HOOK_MODE=all` | every tool call needs approval (default `risky`) |
| `AKIRA_GUARD_POLICY=/path/policy.json` | extra patterns, see below |
| `AKIRA_GUARD_TIMEOUT=120` | seconds to wait on the console (keep below the hook `timeout`) |
| `AKIRA_GUARD_RECEIPTS=~/receipts` | keep every signed answer as a JSON file |

```json
{
  "bash": ["\\bmake\\s+release\\b", "\\bpsql\\b.*prod"],
  "tools": ["^mcp__stripe__.*$", "^Write$"]
}
```

### Any MCP client: Claude Code, Claude Desktop, Cursor, …

`mcp_server.py` adds one tool, `request_human_approval`, that blocks until you
answer on the console.

```bash
claude mcp add --transport stdio akira-guard -- python3 /absolute/path/to/AkiraSDK/tools/agent_guard/mcp_server.py
```

Claude Desktop (`claude_desktop_config.json`, then restart the app):

```json
{
  "mcpServers": {
    "akira-guard": {
      "command": "python3",
      "args": ["/absolute/path/to/AkiraSDK/tools/agent_guard/mcp_server.py"]
    }
  }
}
```

Then tell the agent when to use it, for example in `CLAUDE.md` or the system
prompt: *"Before anything irreversible (deleting, force-pushing, deploying,
paying), call `request_human_approval` and proceed only if it says APPROVED."*

- MCP depends on the model choosing to call the tool. Use the hook above, or
  server-side verification (section 4), when you need it enforced.
- In Claude Code, an MCP call that runs longer than 2 minutes moves to the
  background. Set `CLAUDE_CODE_MCP_AUTO_BACKGROUND_MS=0` if you want Claude to
  wait in the foreground while you decide.

### Your own agent code (Python)

```python
import sys; sys.path.insert(0, "/absolute/path/to/AkiraSDK/tools/agent_guard")
from akira_guard import Guard

guard = Guard.from_config()          # uses the pairing from `akira-guard pair`

def delete_bucket(name: str) -> str:
    """A tool your agent can call."""
    r = guard.approve(f"Delete S3 bucket {name} and every object in it",
                      action="aws s3 rb", risk="high", agent="ops-agent")
    if not r.approved:               # True only for a signed, verified "approved"
        return f"Not done: the owner did not approve ({r.decision})."
    ...                              # really delete
    return "Deleted."
```

Wrap the dangerous tools of any framework this way (LangChain, LlamaIndex,
OpenAI Agents SDK, your own loop). `approve()` blocks until you answer, then
returns a `Receipt`. `r.save("receipt.json")` keeps a copy as proof.

### Shell scripts and everything else

```bash
akira-guard run --summary "Deploy web v2.3 to production" --risk high -- ./deploy.sh
akira-guard ask "Rotate the database password" --action Ops --receipt approval.json && ./rotate.sh
```

| Exit code | Meaning |
|---|---|
| 0 | approved, and the signature was verified |
| 1 | denied, timed out, or the agent gave up |
| 2 | error: no console, not paired, or bad signature |

Other languages can talk to the console directly. See the protocol reference below.

## 4. Make approvals binding: verify where the action runs

The hook and the MCP tool run on the same computer as the agent. They stop an
agent's **mistakes**, but an agent or attacker that controls that computer
could skip them. To make an approval truly required, have the **system that
executes the action** check the signed receipt. It needs only the receipt and
the console's public key, and the agent's computer can forge neither.

[`examples/deploy_gate.py`](examples/deploy_gate.py) does this, for a CI job or
deploy host:

```bash
# on the agent's machine
akira-guard ask "Deploy web v2.3 to production" --action Deploy --receipt receipt.json
# ...send receipt.json along with the deploy request...

# on the deploy server (AKIRA_GUARD_PUB = "pub" from ~/.config/akira-guard/device.json)
python3 deploy_gate.py receipt.json --expect-action Deploy \
    --expect-summary "Deploy web v2.3 to production" \
    --state /var/lib/deploy/akira-counter && ./really_deploy.sh
```

It refuses unless:
- the signature is by the pinned key;
- the decision is "approved";
- the request is exactly the expected action;
- the approval is fresh (15 minutes by default);
- its counter hasn't been used before.

Use the same check in a payment worker, a database-migration runner, or a
GitHub Actions step.

## Security notes and limits

- **On the LAN**, requests travel as plain HTTP:
  - Others on your network can read them.
  - Requests are authenticated with the pairing code (HMAC-SHA256). Anyone who
    knows the code can put a request on screen, but approving it still takes
    your thumbs.
  - Answers are signed, so a tampered answer won't verify.
  - Exact replays of a request are refused.
- **Software key**: without an SE050 the private key sits in the console's
  settings storage. It's a demo mode only.
- **SE050 key slots** are shared between apps that hold the `crypto`
  capability. Another app could replace Agent Guard's key, though it can't sign
  with it without its own Seal. A replaced key changes the fingerprint, and
  every verifier that pinned the old one rejects the new signatures.
- **New key**: wiping the app's settings creates a new key. Pair again and
  update `AKIRA_GUARD_PUB` wherever you verify.
- **One request at a time**: a second request gets HTTP 409 (busy) while one is
  on screen.
- **Foreground only**: Agent Guard must be open on the console. While you're in
  another app, requests fail (closed).
- **Device time**: the `time` in answers is the console's clock (`0` if it was
  never set). The freshness check uses the request's own `time` field, which
  your computer sets and the signature covers.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Waiting for Wi-Fi` on the console | connect in Settings → Wi-Fi, then reopen Agent Guard |
| `cannot reach Agent Guard at …` | check that the app is open, the IP matches the screen, port 8787 isn't blocked, and you're on the same network |
| `the console refused the pairing code` | the code changed (Y on the Pair screen): run `akira-guard pair` again |
| `device key changed since pairing` (`status`) | the app's settings were wiped or it's another console: pair again, update verifiers |
| approved on screen but `signing failed … Seal refused` | AkiraOS asked for a Seal and it was refused: approve again and allow the Seal |
| `busy: another request is on screen` | answer or let the first request time out |
| Claude Code waits but nothing shows | make sure the hook `timeout` (seconds) is larger than `AKIRA_GUARD_TIMEOUT` |

## Protocol reference

HTTP/1.1 on TCP port **8787**, one request per connection.

| Endpoint | Auth | Reply |
|---|---|---|
| `GET /v1/info` | none | `{"app":"agent_guard","version","protocol":1,"key":"se050"\|"software","pub":"04…","fp":"AB12 CD34 EF56 7890","busy":false}` |
| `POST /v1/ping` | yes | `{"ok":true}`, which confirms the pairing code |
| `POST /v1/approve` | yes | waits for the owner, then returns the answer |

**Auth header**: `X-Akira-Auth: hex(HMAC-SHA256(K, METHOD + " " + PATH + "\n" + body))`,
with `K = SHA-256("akira-agent-guard/pair\n" + CODE)`. `CODE` is the 8 pairing
characters in upper case, without the dash.

**Approve body** (JSON, at most 2048 bytes; unknown fields are ignored but still covered by the signature):

| Field | Required | Notes |
|---|---|---|
| `summary` | yes | what will happen; up to 255 characters are shown |
| `nonce` | yes | 8–64 random characters, so every request is unique |
| `action` | no | short title, e.g. `Bash`, `Deploy`, `Payment` |
| `details` | no | longer text; up to 1023 characters, scrollable on the console |
| `agent` | no | who is asking |
| `risk` | no | `low`, `medium` (default) or `high`, which sets the header colour |
| `timeout` | no | seconds to wait, 10–600 (default 120) |
| `time` | no | your Unix time; verifiers use it for freshness |

**Answer**:

```json
{"decision":"approved","counter":17,"time":1795000000,
 "request_sha256":"<hex SHA-256 of the exact request body>","key":"se050","sig":"<hex r||s>"}
```

- `decision` is one of `approved`, `denied`, `timeout` or `cancelled`.
- Only `approved` and `denied` are signed. For the others, `sig` is empty.
- If signing fails after you approve, the answer says `denied` and includes an `error` field.

**Signature**: ECDSA P-256 over SHA-256 of these exact bytes, with each line ending in `\n`:

```
AKIRA-AGENT-GUARD/1
decision:<decision>
counter:<counter>
time:<time>
request:<request_sha256>
```

Verify it against the 65-byte public key (`04 || X || Y`) you pinned at
pairing. `akira_guard.verify_receipt()` does this in pure Python.

**Errors**:
- `400`: bad JSON, no summary, or a bad nonce.
- `401`: bad auth.
- `404`: unknown endpoint.
- `409`: busy, or an exact replay.
- `413`: the request is too large.
