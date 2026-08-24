#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
input_file=quantas/Tests/BoostMqResearcherContractInput.json
report=build/tests/mqResearcherContract_EXP1_leader_report.json
peer_outputs=(
	build/tests/mqResearcherContract_EXP1_p0_TEST1.json
	build/tests/mqResearcherContract_EXP1_p1_TEST1.json
)

cleanup() {
	make --no-print-directory -s -C "$repo_root" mq_cleanup >/dev/null 2>&1 || true
	rm -f -- "$repo_root/$report" "${peer_outputs[@]/#/$repo_root/}"
}
trap cleanup EXIT

cleanup
make --no-print-directory -s -C "$repo_root" mq_debug INPUTFILE="$input_file"

python3 - "$repo_root" "$report" "${peer_outputs[@]}" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
leader_report = json.loads((root / sys.argv[2]).read_text())
peer_paths = [root / path for path in sys.argv[3:]]

assert leader_report["success"] is True
assert len(leader_report["tests"]) == 1
assert leader_report["tests"][0]["success"] is True
assert sorted(leader_report["tests"][0]["completedPeers"]) == [0, 1]

for peer_id, path in enumerate(peer_paths):
    output = json.loads(path.read_text())
    assert output["localPeerId"] == peer_id
    assert output["localPeerCount"] == 1
    assert output["localHookVectors"] is True
    assert output["assignedNeighbors"] == [1 - peer_id]
    assert output["parameterMarker"] == "contract-marker"
    assert output["initParametersCalls"] == 1
    assert output["performComputationCalls"] == 3
    assert output["endOfRoundCalls"] == 3
    assert output["endOfExperimentCalls"] == 1
PY

if pgrep -af '(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)' >/dev/null; then
	echo "FAIL: BoostMQ processes remain after researcher contract test" >&2
	exit 1
fi

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex '.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))' -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queues remain after researcher contract test" >&2
	exit 1
fi

echo "PASS: BoostMQ researcher contract behavior is verified."
