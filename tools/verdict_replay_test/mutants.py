#!/usr/bin/env python3
"""Runner and validation script for tools\\verdict_replay_test.

Validates draw claim precedence, slot matching, and receipt filtering.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RIG_SRC = HERE / "verdict_replay_test.cpp"


def run_self_test():
    if not RIG_SRC.exists():
        print(f"ERROR: missing rig source: {RIG_SRC}", file=sys.stderr)
        return 1
    # Check that required headers are present
    required_headers = [
        ROOT / "src" / "d3d11" / "draw_verdict.h",
        ROOT / "src" / "d3d11" / "draw_dispatch.h",
        ROOT / "src" / "common" / "frame_context.h",
        ROOT / "src" / "common" / "plugin_registry.h",
        ROOT / "src" / "common" / "install_receipt.h",
    ]
    for h in required_headers:
        if not h.exists():
            print(f"ERROR: missing header {h}", file=sys.stderr)
            return 1
    print("PASS: verdict_replay_test self-test ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Verdict replay test runner")
    parser.add_argument("--self-test", action="store_true", help="Verify rig structure and dependencies")
    parser.add_argument("--dry-run", action="store_true", help="Print plan and exit without running")
    parser.add_argument("--bin", type=Path, default=None, help="Path to compiled verdict_replay_test.exe")

    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    if args.dry_run:
        print(f"verdict_replay_test: dry-run plan for {RIG_SRC}")
        return 0

    exe = args.bin
    if not exe:
        exe = ROOT / "build" / "verdict_replay_test.exe"

    if not exe.exists():
        print(f"ERROR: test executable not found: {exe}", file=sys.stderr)
        return 1

    proc = subprocess.run([str(exe), "--self-test"])
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
