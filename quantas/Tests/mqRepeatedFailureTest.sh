#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
launcher="$repo_root/quantas/Common/Concrete/Backends/BoostMq/Entrypoints/runBoostMq.sh"
leader="$repo_root/build/debug/quantas_mq_leader.exe"
peer="$repo_root/build/tests/mq_data_delivery_failure_peer.exe"
input="$repo_root/quantas/Tests/BoostMqRepeatedFailureInput.json"
report="$repo_root/build/tests/mqDataDeliveryFailure_EXP1_leader_report.json"
run_log=$(mktemp)

cleanup() {
	make -C "$repo_root" --no-print-directory mq_cleanup >/dev/null 2>&1 || true
	rm -f -- "$run_log" "$report" \
		"$repo_root/build/tests/mqDataDeliveryFailure_EXP1_PEER0_TEST1.json" \
		"$repo_root/build/tests/mqDataDeliveryFailure_EXP1_PEER1_TEST1.json"
}
trap cleanup EXIT

cleanup
set +e
(cd "$repo_root" && bash "$launcher" "$input" "$leader" "$peer" /dev/shm) >"$run_log" 2>&1
run_status=$?
set -e

if [[ $run_status -eq 0 ]]; then
	echo "FAIL: repeated failure run succeeded" >&2
	exit 1
fi

python3 - "$report" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as report_stream:
    report = json.load(report_stream)

assert report["success"] is False
assert report["testCount"] == 2
assert len(report["tests"]) == 1
assert report["tests"][0]["success"] is False
assert report["tests"][0]["transportReliability"]["dropped_backpressure_total"] == 1
PY

if pgrep -af 'quantas_mq_(leader|peer)\.exe|mq_data_delivery_failure_peer\.exe' >/dev/null; then
	echo "FAIL: BoostMQ processes remain after repeated failure" >&2
	exit 1
fi

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queues remain after repeated failure" >&2
	exit 1
fi

echo "PASS: a failed test cannot make a later test appear successful."
