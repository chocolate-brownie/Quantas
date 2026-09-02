#!/usr/bin/env python3
"""Run MTRC topology experiments through BoostMQ and record completed inputs."""

import argparse
import json
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("--n", type=int, default=100,
                    help="Run only topologies whose name contains n<N> (default: 100).")
args = parser.parse_args()

script_dir = Path(__file__).resolve().parent
repo_root = script_dir.parents[1]
topologies_dir = script_dir / "topologies"
completed_path = script_dir / "boostmq_experiments_ran.json"

completed = set(json.loads(completed_path.read_text(encoding="utf-8")))
topology_files = sorted(topologies_dir.glob("*.json"))

for topology_file in topology_files:
    if f"n{args.n}" not in topology_file.stem:
        continue
    if topology_file.name in completed:
        print(f"Skipping completed topology {topology_file.name}")
        continue

    print(f"Running BoostMQ topology {topology_file.name}...")
    subprocess.run(
        ["make", "mq", f"INPUTFILE=quantas/MTRCPeer/topologies/{topology_file.name}"],
        cwd=repo_root,
        check=True,
    )
    completed.add(topology_file.name)
    completed_path.write_text(json.dumps(sorted(completed), indent=2) + "\n", encoding="utf-8")
