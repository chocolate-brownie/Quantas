#!/usr/bin/env python3
"""Compare saved Abstract RunTime values with saved BoostMQ durations."""

import argparse
import json
from pathlib import Path


def read_number(path, key):
    with path.open() as stream:
        value = json.load(stream)[key]
    if not isinstance(value, (int, float)) or value < 0:
        raise ValueError(f"{path}: {key} must be a non-negative number")
    return float(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    with args.manifest.open() as stream:
        manifest = json.load(stream)

    rows = []
    for item in manifest:
        abstract_seconds = read_number(Path(item["abstract"]), "RunTime")
        boost_seconds = read_number(Path(item["boostmq"]), "durationSeconds")
        difference = abs(abstract_seconds - boost_seconds)
        percent = 0.0 if boost_seconds == 0 else difference / boost_seconds * 100.0
        rows.append({
            "delay": item["delay"],
            "abstractSeconds": abstract_seconds,
            "boostMqSeconds": boost_seconds,
            "differenceSeconds": difference,
            "differencePercent": percent,
            "withinFivePercent": percent <= 5.0,
        })

    closest = min(rows, key=lambda row: row["differencePercent"])
    report = {
        "metric": "total runtime in seconds",
        "formula": "abs(Abstract RunTime - BoostMQ durationSeconds) / BoostMQ durationSeconds * 100",
        "results": rows,
        "closestAbstractDelay": closest["delay"],
        "closestDifferencePercent": closest["differencePercent"],
        "closestWithinFivePercent": closest["withinFivePercent"],
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")

    print("delay | abstract seconds | BoostMQ seconds | difference")
    print("--- | ---: | ---: | ---:")
    for row in rows:
        print(f"{row['delay']} | {row['abstractSeconds']:.6f} | {row['boostMqSeconds']:.6f} | {row['differencePercent']:.2f}%")
    print(f"closest delay: {closest['delay']} ({closest['differencePercent']:.2f}%)")


if __name__ == "__main__":
    main()
