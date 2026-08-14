#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
test_dir=$(mktemp -d /dev/shm/quantas-mq-cleanup-test.XXXXXX)
fake_process_pid=""
process_pattern='(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)'

cleanup_test_state() {
	if [[ -n "$fake_process_pid" ]] && kill -0 "$fake_process_pid" 2>/dev/null; then
		kill "$fake_process_pid" 2>/dev/null || true
		wait "$fake_process_pid" 2>/dev/null || true
	fi
	rm -rf -- "$test_dir"
}
trap cleanup_test_state EXIT

run_cleanup() {
	make --no-print-directory -s -C "$repo_root" \
		MQ_RESOURCE_DIR="$test_dir" mq_cleanup
}

assert_contains() {
	local output=$1
	local expected=$2
	if [[ "$output" != *"$expected"* ]]; then
		printf 'FAIL: expected output to contain: %s\n' "$expected" >&2
		printf '%s\n' "$output" >&2
		exit 1
	fi
}

if pgrep -af "$process_pattern" >/dev/null; then
	printf 'FAIL: stop active QUANTAS BoostMQ processes before running this test.\n' >&2
	exit 1
fi

empty_output=$(run_cleanup)
assert_contains "$empty_output" "No abandoned QUANTAS BoostMQ resources found."
printf 'PASS: cleanup is safe when no resources exist.\n'

touch "$test_dir/mq_barrier" "$test_dir/mq_done" \
	"$test_dir/peer_7_control" "$test_dir/peer_7_data" \
	"$test_dir/peer_bad_data"

stale_output=$(run_cleanup)
for resource in mq_barrier mq_done peer_7_control peer_7_data; do
	[[ ! -e "$test_dir/$resource" ]] || {
		printf 'FAIL: stale resource was not removed: %s\n' "$resource" >&2
		exit 1
	}
	assert_contains "$stale_output" "Removed $test_dir/$resource"
done
[[ -e "$test_dir/peer_bad_data" ]] || {
	printf 'FAIL: cleanup removed a lookalike resource.\n' >&2
	exit 1
}
printf 'PASS: stale resources are reported and removed; lookalikes are preserved.\n'

second_output=$(run_cleanup)
assert_contains "$second_output" "No abandoned QUANTAS BoostMQ resources found."
printf 'PASS: running cleanup twice is safe.\n'

touch "$test_dir/mq_done" "$test_dir/peer_8_control"
bash -c 'exec -a ./quantas_mq_leader.exe sleep 60' &
fake_process_pid=$!

for _ in {1..50}; do
	if ps -p "$fake_process_pid" -o args= | grep -Eq "$process_pattern"; then
		break
	fi
	sleep 0.02
done
if ! ps -p "$fake_process_pid" -o args= | grep -Eq "$process_pattern"; then
	printf 'FAIL: could not start the simulated MQ process.\n' >&2
	exit 1
fi

set +e
active_output=$(run_cleanup 2>&1)
active_status=$?
set -e

[[ $active_status -ne 0 ]] || {
	printf 'FAIL: cleanup succeeded while an MQ process was active.\n' >&2
	exit 1
}
for resource in mq_done peer_8_control; do
	[[ -e "$test_dir/$resource" ]] || {
		printf 'FAIL: cleanup removed a resource while an MQ process was active: %s\n' "$resource" >&2
		exit 1
	}
	assert_contains "$active_output" "Skipped $test_dir/$resource: active process detected"
done
printf 'PASS: cleanup refuses and reports resources while an MQ process is active.\n'
