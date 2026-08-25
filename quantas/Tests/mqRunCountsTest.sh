#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
input_file=quantas/Tests/BoostMqMixedCountsInput.json
result_dirs=(results/mqCountsFirst_EXP1 results/mqCountsSecond_EXP2)

cleanup_test_state() {
	make --no-print-directory -s -C "$repo_root" mq_cleanup >/dev/null 2>&1 || true
	rm -rf -- "${result_dirs[@]/#/$repo_root/}"
}
trap cleanup_test_state EXIT

assert_removed_override() {
	local variable=$1
	local expected=$2
	local output
	if output=$(make --no-print-directory -s -C "$repo_root" mq \
		INPUTFILE="$input_file" "$variable=99" 2>&1); then
		printf 'FAIL: %s was accepted.\n' "$variable" >&2
		exit 1
	fi
	[[ "$output" == *"$expected"* ]] || {
		printf 'FAIL: wrong %s error:\n%s\n' "$variable" "$output" >&2
		exit 1
	}
}

cleanup_test_state
assert_removed_override MQ_TOTAL_PEERS "MQ_TOTAL_PEERS was removed; edit the JSON input file"
assert_removed_override MQ_ROUNDS "MQ_ROUNDS was removed; edit the JSON input file"
assert_removed_override MQ_PEER_ID "MQ_PEER_ID was removed; peer IDs come from the JSON experiment"
assert_removed_override MQ_DEBUG_PEER_ID "MQ_DEBUG_PEER_ID was removed; mq_debug runs the complete JSON experiment"

make --no-print-directory -s -C "$repo_root" mq_debug INPUTFILE="$input_file"

python3 - "$repo_root" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
expected = [
    ("results/mqCountsFirst_EXP1/leader_report.json", 2, 1, 1),
    ("results/mqCountsSecond_EXP2/leader_report.json", 3, 2, 2),
]

for relative_path, peers, tests, rounds in expected:
    report = json.loads((root / relative_path).read_text())
    assert report["peerCount"] == peers
    assert report["testCount"] == tests
    assert report["rounds"] == rounds
    assert len(report["tests"]) == tests
    assert all(test["completedPeerCount"] == peers for test in report["tests"])
    assert all(len(test["completedPeers"]) == peers for test in report["tests"])
    for test_number, test in enumerate(report["tests"], start=1):
        expected_directory = str((root / relative_path).parent)
        assert len(test["peerOutputFiles"]) == peers
        for peer_id, output_path in test["peerOutputFiles"].items():
            path = root / output_path
            assert str(path.parent) == expected_directory
            assert path.name == f"peer_{peer_id}_TEST{test_number}.txt"
            assert path.is_file()
PY

if pgrep -af '(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)' >/dev/null; then
	printf 'FAIL: BoostMQ processes remain after the count test.\n' >&2
	exit 1
fi

resources=$(find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print)
[[ -z "$resources" ]] || {
	printf 'FAIL: BoostMQ resources remain after the count test:\n%s\n' "$resources" >&2
	exit 1
}

printf 'PASS: JSON controls mixed BoostMQ experiment counts.\n'
