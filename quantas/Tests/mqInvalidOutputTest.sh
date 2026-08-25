#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
launcher="$repo_root/quantas/Common/Concrete/Backends/BoostMq/Entrypoints/runBoostMq.sh"
leader="$repo_root/build/debug/quantas_mq_leader.exe"
peer="$repo_root/build/tests/mq_invalid_output_peer.exe"
result_dir="$repo_root/results/mqInvalidOutput_EXP1"
report="$result_dir/leader_report.json"

cleanup() {
	make -C "$repo_root" --no-print-directory mq_cleanup >/dev/null 2>&1 || true
	rm -rf -- "$result_dir"
}
trap cleanup EXIT

for input in BoostMqMissingOutputInput.json BoostMqMalformedOutputInput.json; do
	set +e
	(cd "$repo_root" && bash "$launcher" \
		"$repo_root/quantas/Tests/$input" "$leader" "$peer" /dev/shm >/dev/null 2>&1)
	status=$?
	set -e
	if [[ $status -eq 0 ]]; then
		echo "FAIL: $input was reported successful" >&2
		exit 1
	fi

	python3 - "$report" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as report_stream:
    report = json.load(report_stream)
assert report["success"] is False
assert report["tests"][0]["success"] is False
PY
done

echo "PASS: missing and malformed peer output fail the leader report."
