#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
input_file=quantas/Tests/BoostMqRepeatedTestsInput.json
result_dir=results/mqRepeatedTests_EXP1
report=$result_dir/leader_report.json

cleanup() {
	make --no-print-directory -s -C "$repo_root" mq_cleanup >/dev/null 2>&1 || true
	rm -rf -- "${repo_root:?}/${result_dir:?}"
}
trap cleanup EXIT

cleanup
make --no-print-directory -s -C "$repo_root" mq_debug INPUTFILE="$input_file"

python3 - "$repo_root/$report" "$repo_root" <<'PY'
import json
import pathlib
import sys

report = json.loads(pathlib.Path(sys.argv[1]).read_text())
root = pathlib.Path(sys.argv[2])
tests = report["tests"]

assert report["success"] is True
assert report["testCount"] == 3
assert len(tests) == 3

output_files = []
for index, test in enumerate(tests, start=1):
    assert test["success"] is True
    assert test["completedPeerCount"] == 2
    assert sorted(test["completedPeers"]) == [0, 1]
    assert sorted(test["readyPeers"]) == [0, 1]
    assert test["missingPeers"] == []
    assert test["timedOut"] is False

    peer_files = test["peerOutputFiles"]
    assert sorted(peer_files) == ["0", "1"]
    for path in peer_files.values():
        output_files.append(path)
        peer_report = json.loads((root / path).read_text())
        assert len(peer_report["tests"]) == 1
        assert "transportMetrics" in peer_report

assert len(output_files) == 6
assert len(set(output_files)) == 6
for test_number in range(1, 4):
    assert sum(f"_TEST{test_number}" in path for path in output_files) == 2
PY

if pgrep -af '(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)' >/dev/null; then
	echo "FAIL: BoostMQ processes remain after repeated-test run" >&2
	exit 1
fi

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queues remain after repeated-test run" >&2
	exit 1
fi

echo "PASS: three repeated BoostMQ tests are isolated."
