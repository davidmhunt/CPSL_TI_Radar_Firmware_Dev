#!/usr/bin/env python3
"""Fake `docker compose` (selected with FW_COMPOSE). Logs the call, then runs the project script on
the host with /build_context mapped to FAKE_ROOT. FAKE_COMPOSE_MODE=hang sleeps instead (cancel tests)."""
import json
import os
import subprocess
import sys
import time

argv = sys.argv[1:]
root = os.environ["FAKE_ROOT"]
env = dict(os.environ)
i = argv.index("run")
j = i + 1
service = None
while j < len(argv):
    a = argv[j]
    if a in ("--rm", "-T"):
        j += 1
    elif a in ("--user", "-e"):
        if a == "-e":
            k, _, v = argv[j + 1].partition("=")
            env[k] = v
        j += 2
    else:
        service = a
        break
rest = argv[j + 1:]            # bash -c <snippet> <script> args...
script, args = rest[3], rest[4:]
fix = lambda s: root + s[len("/build_context"):] if s.startswith("/build_context") else s
script, args = fix(script), [fix(a) for a in args]
with open(os.environ["FAKE_COMPOSE_LOG"], "a") as f:
    f.write(json.dumps({"service": service, "script": script, "args": args,
                        "env": {k: v for k, v in env.items() if k.startswith("FW_")}}) + "\n")
if os.environ.get("FAKE_COMPOSE_MODE") == "hang":
    open(os.environ["FAKE_COMPOSE_PIDFILE"], "w").write(str(os.getpid()))
    print("[ 10%] compiling", flush=True)
    time.sleep(120)
    sys.exit(0)
sys.exit(subprocess.call(["bash", script, *args], env=env))
