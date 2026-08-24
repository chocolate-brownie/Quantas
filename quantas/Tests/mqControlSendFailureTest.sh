#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
leader="$repo_root/build/debug/quantas_mq_leader.exe"
ready_sender="$repo_root/build/tests/mq_send_ready_ids.exe"
input="$repo_root/quantas/Tests/BoostMqControlSendFailureInput.json"
report="$repo_root/build/tests/mqControlSendFailure_EXP1_leader_report.json"
leader_log=$(mktemp)
leader_pid=""

cleanup() {
	if [[ -n $leader_pid ]]; then
		kill "$leader_pid" 2>/dev/null || true
		wait "$leader_pid" 2>/dev/null || true
	fi
	make -C "$repo_root" --no-print-directory mq_cleanup >/dev/null 2>&1 || true
	rm -f -- "$leader_log" "$report"
}
trap cleanup EXIT

"$leader" --experiment 0 "$input" >"$leader_log" 2>&1 &
leader_pid=$!

for _ in {1..100}; do
	[[ -e /dev/shm/mq_barrier ]] && break
	sleep 0.01
done
if [[ ! -e /dev/shm/mq_barrier ]]; then
	echo "FAIL: leader did not create the ready barrier" >&2
	exit 1
fi

"$ready_sender" 2

set +e
wait "$leader_pid"
leader_status=$?
set -e
leader_pid=""

if [[ $leader_status -eq 0 ]]; then
	echo "FAIL: leader succeeded without peer control queues" >&2
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

grep -q "Failed sending assignment to peer 0 within 50 ms" "$leader_log"

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queues remain after the control-send failure" >&2
	exit 1
fi

echo "PASS: leader reports control-send failure and cleans BoostMQ queues."
