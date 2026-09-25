#!/usr/bin/env python3
"""
tools/check_pr_feedback.py -- Polls GitHub for PR reviews, review comments, and issue comments.

Usage:
  python tools/check_pr_feedback.py [--prs 40,41] [--state-file PATH]
  python tools/check_pr_feedback.py --self-test
"""

import argparse
import json
import os
import sys
import urllib.request
import urllib.error
import subprocess

REPO = "characterecho-sean/edvr-unofficial-patch"

def get_github_token():
    # 1. Environment variable
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        return token
    # 2. Git Credential Manager
    try:
        p = subprocess.Popen(['git', 'credential', 'fill'],
                             stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,
                             text=True)
        stdout, _ = p.communicate(input="protocol=https\nhost=github.com\n\n", timeout=5)
        for line in stdout.splitlines():
            if line.startswith("password="):
                return line.split("=", 1)[1].strip()
    except Exception:
        pass
    return None

def fetch_json(url, token=None):
    headers = {
        "User-Agent": "EDVR-PR-Monitor/1.0",
        "Accept": "application/vnd.github.v3+json"
    }
    if token:
        headers["Authorization"] = f"token {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.loads(resp.read().decode('utf-8'))

def parse_pr_events(reviews, comments, issue_comments):
    events = []
    for r in reviews:
        events.append({
            "type": "review",
            "id": f"rev_{r.get('id')}",
            "author": r.get("user", {}).get("login"),
            "state": r.get("state"),
            "body": r.get("body") or "",
            "submitted_at": r.get("submitted_at")
        })
    for c in comments:
        events.append({
            "type": "inline_comment",
            "id": f"comm_{c.get('id')}",
            "author": c.get("user", {}).get("login"),
            "path": c.get("path"),
            "line": c.get("line"),
            "body": c.get("body") or "",
            "updated_at": c.get("updated_at")
        })
    for ic in issue_comments:
        events.append({
            "type": "issue_comment",
            "id": f"issue_{ic.get('id')}",
            "author": ic.get("user", {}).get("login"),
            "body": ic.get("body") or "",
            "updated_at": ic.get("updated_at")
        })
    return events

def check_feedback(prs, state_file, token=None):
    seen = {}
    if os.path.exists(state_file):
        try:
            with open(state_file, "r", encoding="utf-8") as f:
                seen = json.load(f)
        except Exception:
            seen = {}

    new_events = []

    for pr_num in prs:
        try:
            reviews = fetch_json(f"https://api.github.com/repos/{REPO}/pulls/{pr_num}/reviews", token)
            comments = fetch_json(f"https://api.github.com/repos/{REPO}/pulls/{pr_num}/comments", token)
            issue_comments = fetch_json(f"https://api.github.com/repos/{REPO}/issues/{pr_num}/comments", token)
        except Exception as e:
            print(f"[pr_monitor] Warning: could not fetch PR #{pr_num}: {e}", file=sys.stderr)
            continue

        events = parse_pr_events(reviews, comments, issue_comments)
        for ev in events:
            ev_id = ev["id"]
            if ev_id not in seen:
                seen[ev_id] = ev
                ev["pr"] = pr_num
                new_events.append(ev)

    # Persist updated state
    os.makedirs(os.path.dirname(os.path.abspath(state_file)), exist_ok=True)
    with open(state_file, "w", encoding="utf-8") as f:
        json.dump(seen, f, indent=2)

    return new_events

def self_test():
    mock_reviews = [{
        "id": 101,
        "user": {"login": "characterecho-sean"},
        "state": "CHANGES_REQUESTED",
        "body": "Mock test change request",
        "submitted_at": "2026-09-25T10:00:00Z"
    }]
    mock_comments = [{
        "id": 202,
        "user": {"login": "characterecho-sean"},
        "path": "src/openxr/native_device.h",
        "line": 97,
        "body": "Mock inline comment",
        "updated_at": "2026-09-25T10:01:00Z"
    }]
    mock_issue_comments = [{
        "id": 303,
        "user": {"login": "dmnemec"},
        "body": "Mock author response",
        "updated_at": "2026-09-25T10:02:00Z"
    }]

    parsed = parse_pr_events(mock_reviews, mock_comments, mock_issue_comments)
    assert len(parsed) == 3, f"Expected 3 events, got {len(parsed)}"
    assert parsed[0]["id"] == "rev_101"
    assert parsed[0]["state"] == "CHANGES_REQUESTED"
    assert parsed[1]["id"] == "comm_202"
    assert parsed[2]["id"] == "issue_303"
    print("tools/check_pr_feedback.py: self-test ok")
    return 0

def main():
    parser = argparse.ArgumentParser(description="Check PR feedback from maintainers.")
    parser.add_argument("--self-test", action="store_true", help="Run internal self-tests.")
    parser.add_argument("--prs", default="40,41", help="Comma-separated list of PR numbers.")
    parser.add_argument("--state-file", default=None, help="Path to state file tracking seen events.")
    parser.add_argument("--token", default=None, help="GitHub personal access token.")

    args = parser.parse_args()

    if args.self_test:
        return self_test()

    token = args.token or get_github_token()
    prs = [int(p.strip()) for p in args.prs.split(",") if p.strip()]

    state_file = args.state_file
    if not state_file:
        localapp = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        state_file = os.path.join(localapp, "EDVR", "pr_feedback_seen.json")

    new_events = check_feedback(prs, state_file, token)
    if new_events:
        print(f"[pr_monitor] Found {len(new_events)} new event(s):")
        for ev in new_events:
            print(f"--- PR #{ev['pr']} | {ev['type']} from @{ev['author']} ---")
            if ev.get("state"):
                print(f"State: {ev['state']}")
            if ev.get("path"):
                print(f"File: {ev['path']} L{ev['line']}")
            print(f"Body: {ev.get('body')}\n")
        return 10  # Exit code 10 signals new feedback found
    else:
        print("[pr_monitor] No new feedback.")
        return 0

if __name__ == "__main__":
    sys.exit(main())
