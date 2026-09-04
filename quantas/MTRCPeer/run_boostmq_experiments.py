#!/usr/bin/env python3
# Run Lorenzo's MTRC topology inputs through the BoostMQ backend.  # noqa: EXE001
#
# The script selects topology JSON files for the requested peer count (for
# example, --n 50), optionally limits the number of new files, then launches
# each one with `make mq`.  A successful input is recorded in
# boostmq_experiments_ran.json, so a later invocation skips it instead of
# running it again.  If an experiment fails, subprocess.run stops the batch
# and leaves that input out of the completed list for investigation.

import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument(
    "--n",
    type=int,
    default=100,
    help="Run only topologies whose name contains n<N> (default: 100).",
)
parser.add_argument(
    "--limit", type=int, help="Run at most this many new matching topology files."
)
args = parser.parse_args()

if args.limit is not None and args.limit <= 0:
    parser.error("--limit must be a positive integer")

script_dir = Path(__file__).resolve().parent
repo_root = script_dir.parents[1]
topologies_dir = script_dir / "topologies"
completed_path = script_dir / "boostmq_experiments_ran.json"

completed = set(json.loads(completed_path.read_text(encoding="utf-8")))
topology_files = sorted(topologies_dir.glob("*.json"))
run_count = 0

for topology_file in topology_files:
    if f"n{args.n}" not in topology_file.stem:
        continue
    if topology_file.name in completed:
        print(f"Skipping completed topology {topology_file.name}")
        continue
    if args.limit is not None and run_count >= args.limit:
        break

    print(f"Running BoostMQ topology {topology_file.name}...")
    subprocess.run(
        ["make", "mq", f"INPUTFILE=quantas/MTRCPeer/topologies/{topology_file.name}"],
        cwd=repo_root,
        check=True,
    )
    completed.add(topology_file.name)
    completed_path.write_text(
        json.dumps(sorted(completed), indent=2) + "\n", encoding="utf-8"
    )
    run_count += 1
