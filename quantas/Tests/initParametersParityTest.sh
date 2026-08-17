#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
input_file=quantas/Tests/InitParametersParityInput.json
output_files=(
	build/tests/initParametersExample.json
	build/tests/initParametersExample2.json
	build/tests/initParametersEmpty.json
	build/tests/initParametersExample_EXP1_p0_TEST1.json
	build/tests/initParametersExample2_EXP2_p0_TEST1.json
	build/tests/initParametersEmpty_EXP3_p0_TEST1.json
	build/tests/initParametersExample_EXP1_leader_report.json
	build/tests/initParametersExample2_EXP2_leader_report.json
	build/tests/initParametersEmpty_EXP3_leader_report.json
)

cleanup_test_state() {
	make --no-print-directory -s -C "$repo_root" mq_cleanup >/dev/null 2>&1 || true
	rm -f -- "${output_files[@]/#/$repo_root/}"
}
trap cleanup_test_state EXIT

cleanup_test_state
mkdir -p "$repo_root/build/tests"
make --no-print-directory -s -C "$repo_root" run INPUTFILE="$input_file"
make --no-print-directory -s -C "$repo_root" mq_debug INPUTFILE="$input_file"

python3 - "$repo_root" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
checks = [
    (
        "build/tests/initParametersExample.json",
        "build/tests/initParametersExample_EXP1_p0_TEST1.json",
        {"parameter1": 731, "parameter2": "example-peer", "changePeerType": False},
    ),
    (
        "build/tests/initParametersExample2.json",
        "build/tests/initParametersExample2_EXP2_p0_TEST1.json",
        {"parameter1": 947, "parameter2": "example-peer-2", "parameter3": True},
    ),
    (
        "build/tests/initParametersEmpty.json",
        "build/tests/initParametersEmpty_EXP3_p0_TEST1.json",
        {"changePeerType": False},
    ),
]

for abstract_path, mq_path, expected in checks:
    abstract = json.loads((root / abstract_path).read_text())["tests"][0]
    mq = json.loads((root / mq_path).read_text())["tests"][0]
    for key, value in expected.items():
        assert abstract[key] == [value], (abstract_path, key, abstract[key])
        assert mq[key] == [value], (mq_path, key, mq[key])
        assert abstract[key] == mq[key], key
PY

if pgrep -af '(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)' >/dev/null; then
	printf 'FAIL: BoostMQ processes remain after the parameter test.\n' >&2
	exit 1
fi

resources=$(find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print)
[[ -z "$resources" ]] || {
	printf 'FAIL: BoostMQ resources remain after the parameter test:\n%s\n' "$resources" >&2
	exit 1
}

printf 'PASS: Abstract and BoostMQ receive the same JSON parameters.\n'
