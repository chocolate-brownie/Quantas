#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
leader="$repo_root/build/debug/quantas_mq_leader.exe"
input="$repo_root/quantas/Tests/BoostMqInvalidTopologyInput.json"
run_log=$(mktemp)

cleanup() {
	make --no-print-directory -s -C "$repo_root" mq_cleanup >/dev/null 2>&1 || true
	rm -f -- "$run_log"
}
trap cleanup EXIT

cleanup
set +e
"$leader" --preflight "$input" >"$run_log" 2>&1
run_status=$?
set -e

if [[ $run_status -eq 0 ]]; then
	echo "FAIL: invalid topology passed preflight" >&2
	exit 1
fi

grep -q "topology.height \* topology.width must equal topology.initialPeers" "$run_log"
if grep -q "temporary .* queue created successfully" "$run_log"; then
	echo "FAIL: queue preflight ran before topology validation" >&2
	exit 1
fi

if pgrep -af '(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)' >/dev/null; then
	echo "FAIL: BoostMQ processes remain after invalid topology preflight" >&2
	exit 1
fi

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queues remain after invalid topology preflight" >&2
	exit 1
fi

echo "PASS: invalid topology fails before a BoostMQ run starts."
