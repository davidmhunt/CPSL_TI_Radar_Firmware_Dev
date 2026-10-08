"""Argument parsing and dispatch; owns the one-`result`-last guarantee (R10)."""
from __future__ import annotations

import argparse
import sys
import traceback

from . import procs
from .core import FAILED, USAGE, Cancelled, FwExit, Out
from .state import sweep_tokens
from .v_build import cmd_build, cmd_deps, cmd_publish
from .v_flash import cmd_flash, cmd_verify
from .v_info import cmd_help, cmd_list, cmd_new, cmd_ports, cmd_test


class Parser(argparse.ArgumentParser):
    def _print_message(self, message, file=None):
        if message:
            print(message, end="", file=sys.stderr if Out.json else (file or sys.stdout))

    def error(self, message):
        raise FwExit(USAGE, f"{self.prog}: {message}")


def build_parsers() -> dict[str, Parser]:
    sub: dict[str, Parser] = {}

    def verb(name, fn, help_):
        s = sub[name] = Parser(prog=f"fw {name}", description=help_)
        s.add_argument("--json", action="store_true", help="JSON Lines on stdout")
        s.set_defaults(fn=fn)
        return s

    verb("help", cmd_help, "summary")
    verb("list", cmd_list, "list projects")
    s = verb("ports", cmd_ports, "serial ports"); s.add_argument("project", nargs="?")
    s = verb("new", cmd_new, "new project"); s.add_argument("project")
    s = verb("deps", cmd_deps, "check downloads"); s.add_argument("project", nargs="?")
    s = verb("build", cmd_build, "build")
    s.add_argument("project"); s.add_argument("--variant"); s.add_argument("--dry-run", action="store_true")
    s.add_argument("rest", nargs="*")
    s = verb("test", cmd_test, "hardware-free checks")
    s.add_argument("project", nargs="?"); s.add_argument("--skip-commands", action="store_true")
    s = verb("flash", cmd_flash, "flash")
    s.add_argument("project"); s.add_argument("port"); s.add_argument("image", nargs="?")
    s.add_argument("--dry-run", action="store_true"); s.add_argument("--plan", action="store_true")
    s.add_argument("--confirm", metavar="TOKEN")
    s = verb("verify", cmd_verify, "identify probes")
    s.add_argument("project"); s.add_argument("--port"); s.add_argument("--dry-run", action="store_true")
    s = verb("publish", cmd_publish, "publish images")
    s.add_argument("project"); s.add_argument("--dest"); s.add_argument("--dry-run", action="store_true")
    return sub


def main(argv: list[str]) -> int:
    Out.json = "--json" in argv
    argv = [a for a in argv if a != "--json"]
    if not argv or argv[0] in ("-h", "--help"):
        argv = ["help"]
    Out.verb = argv[0]
    procs.install_signal_handlers()
    code, reason, data = 0, "", {}
    try:
        sweep_tokens()
        parsers = build_parsers()
        if argv[0] not in parsers:
            raise FwExit(USAGE, f"unknown command '{argv[0]}' (run ./fw help)")
        args = parsers[argv[0]].parse_intermixed_args(argv[1:])
        try:
            args.fn(args)
            raise FwExit(0)
        except Cancelled:
            procs.kill_children()
            for s in Out.cancel_note:
                Out.say("  - " + s)
            raise FwExit(FAILED, "cancelled")
    except FwExit as e:
        code, reason, data = e.code, e.reason, e.data
    except SystemExit as e:  # argparse --help
        code = int(e.code or 0)
    except Cancelled:
        code, reason = FAILED, "cancelled"
    except Exception as e:  # never leave a GUI without a result
        traceback.print_exc(file=sys.stderr)
        code, reason = FAILED, f"internal error: {e}"
    Out.result(code, reason, data)
    return code
