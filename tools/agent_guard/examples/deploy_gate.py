#!/usr/bin/env python3
# Copyright (c) 2026 PenEngineering S.R.L
# SPDX-License-Identifier: Apache-2.0
"""Refuse to run an action unless a human approved exactly it on the console.

Run this where the action executes (CI deploy job, release host, payment
worker), not next to the agent.  It needs only the receipt and the console's
public key, pinned at pairing time; the agent's machine can't forge either.

    deploy_gate.py receipt.json --expect-action Deploy \\
        --expect-summary "Deploy web v2.3 to production" \\
        --pub "$AKIRA_GUARD_PUB" --state /var/lib/deploy/akira-counter

Checks: the signature is by the pinned key, the decision is "approved", the
approved request is the expected action, it is fresh (request time within
--max-age seconds), and its counter is newer than any receipt used before
(so a receipt can't be replayed).  Exit 0 to go ahead, 1 to refuse.
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from akira_guard import verify_receipt_file  # noqa: E402


def refuse(why):
    print(f"deploy gate: REFUSED - {why}", file=sys.stderr)
    return 1


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("receipt")
    p.add_argument("--pub", default=os.environ.get("AKIRA_GUARD_PUB"), help="pinned device public key (hex)")
    p.add_argument("--expect-action", help="the approved request's action must equal this")
    p.add_argument("--expect-summary", help="the approved request's summary must equal this")
    p.add_argument("--max-age", type=int, default=900, help="seconds (default 15 min)")
    p.add_argument("--state", help="file remembering the last receipt counter used")
    a = p.parse_args()
    if not a.pub:
        return refuse("no pinned public key (--pub or AKIRA_GUARD_PUB)")

    approved, ok, reason, r = verify_receipt_file(a.receipt, a.pub)
    if not ok:
        return refuse(reason)
    if not approved:
        return refuse(f"the owner answered {r['response'].get('decision')!r}")
    req, resp = r["request"], r["response"]
    if a.expect_action is not None and req.get("action") != a.expect_action:
        return refuse(f"approval was for action {req.get('action')!r}")
    if a.expect_summary is not None and req.get("summary") != a.expect_summary:
        return refuse(f"approval was for {req.get('summary')!r}")
    age = time.time() - float(req.get("time", 0))
    if not -60 <= age <= a.max_age:
        return refuse(f"approval is {int(age)} s old (max {a.max_age})")
    counter = int(resp.get("counter", 0))
    if a.state:
        last = 0
        if os.path.exists(a.state):
            with open(a.state) as f:
                last = int(f.read().strip() or 0)
        if counter <= last:
            return refuse(f"receipt counter {counter} already used (last {last})")
        with open(a.state, "w") as f:
            f.write(str(counter))
    print(f"deploy gate: OK - {req.get('action')}: {req.get('summary')} ({reason}, counter {counter})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
