#!/usr/bin/env python3
"""Explicitly authorized development launcher. Reads JSON requests from stdin."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from devtools.session import Session

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--allow", nargs="+", required=True, choices=("inspect", "click", "close"))
parser.add_argument("--allow-text", action="store_true", help="allow reading native label/title text")
args = parser.parse_args()
with Session(allow=args.allow, allow_text=args.allow_text) as session:
    print(json.dumps(session.ready), flush=True)
    for line in sys.stdin:
        # Complete protocol records are forwarded, without a shell or command method.
        session.sock.sendall(line.encode())
        reply = session.receive()
        print(json.dumps(reply, ensure_ascii=False), flush=True)
        if reply.get("result", {}).get("closed"):
            session.process.wait(timeout=3)
            break
if session.forced_cleanup:
    print("El lanzador canceló el proceso; no cuenta como cierre nativo limpio.", file=sys.stderr)
    raise SystemExit(2)
raise SystemExit(session.process.returncode)
