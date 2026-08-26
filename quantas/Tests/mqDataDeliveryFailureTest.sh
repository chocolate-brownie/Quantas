#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
launcher="$repo_root/quantas/Common/Concrete/Backends/BoostMq/Entrypoints/runBoostMq.sh"
leader="$repo_root/build/debug/quantas_mq_leader.exe"
peer="$repo_root/build/tests/mq_data_delivery_failure_peer.exe"
input="$repo_root/quantas/Tests/BoostMqDataDeliveryFailureInput.json"
result_dir="$repo_root/results/mqDataDeliveryFailure_EXP1"
report="$result_dir/leader_report.json"
run_log=$(mktemp)

cleanup() {
	make -C "$repo_root" --no-print-directory mq_cleanup >/dev/null 2>&1 || true
	rm -f -- "$run_log"
	rm -rf -- "$result_dir"
}
trap cleanup EXIT

set +e
(cd "$repo_root" && bash "$launcher" "$input" "$leader" "$peer" /dev/shm) >"$run_log" 2>&1
run_status=$?
set -e

if [[ $run_status -eq 0 ]]; then
	echo "FAIL: launcher succeeded after a data-send failure" >&2
	exit 1
fi

python3 - "$report" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as report_stream:
    report = json.load(report_stream)

test = report["tests"][0]
assert report["success"] is False
assert test["success"] is False
assert test["timedOut"] is False
assert test["transportReliability"]["dropped_backpressure_total"] == 1
assert test["transportReliability"]["reliable"] is False
PY

grep -q "leader recorded failure from peer 0" "$run_log"
grep -q "after 25 ms" "$run_log"

if pgrep -f 'quantas_mq_(leader|peer)\.exe|mq_data_delivery_failure_peer\.exe' >/dev/null; then
	echo "FAIL: BoostMQ processes remain after the data-send failure" >&2
	exit 1
fi

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queues remain after the data-send failure" >&2
	exit 1
fi

echo "PASS: data-send failure reaches the leader report and cleanup completes."
