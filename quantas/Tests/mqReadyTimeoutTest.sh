#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
input_file="$repo_root/quantas/Tests/BoostMqReadyTimeoutInput.json"
leader_exe="$repo_root/build/debug/quantas_mq_leader.exe"
peer_exe="$repo_root/build/debug/quantas_mq_peer.exe"
launcher="$repo_root/quantas/Common/Concrete/Backends/BoostMq/Entrypoints/runBoostMq.sh"
result_dir="$repo_root/results/mqReadyTimeout_EXP1"
report_file="$result_dir/leader_report.json"
temp_dir=$(mktemp -d)
peer_wrapper="$temp_dir/peer-wrapper.sh"
process_pattern='(^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$)'
resource_regex='.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))'

cleanup() {
	make -C "$repo_root" --no-print-directory mq_cleanup >/dev/null 2>&1 || true
	rm -rf -- "$result_dir"
	rm -rf -- "$temp_dir"
}
trap cleanup EXIT

if pgrep -f "$process_pattern" >/dev/null; then
	echo "FAIL: another BoostMQ run is active" >&2
	exit 1
fi

cat >"$peer_wrapper" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ ${4:-} == 2 ]]; then
	exit 0
fi
exec "$QUANTAS_REAL_PEER_EXE" "$@"
EOF
chmod +x "$peer_wrapper"

set +e
QUANTAS_REAL_PEER_EXE="$peer_exe" \
	bash "$launcher" "$input_file" "$leader_exe" "$peer_wrapper" /dev/shm
launcher_status=$?
set -e

if [[ $launcher_status -eq 0 ]]; then
	echo "FAIL: launcher succeeded although peer 2 never became ready" >&2
	exit 1
fi

python3 - "$report_file" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as report_stream:
    report = json.load(report_stream)

test = report["tests"][0]
assert report["success"] is False
assert report["boostMq"]["readyTimeoutMs"] == 500
assert test["success"] is False
assert test["timedOut"] is True
assert test["readyPeers"] == [0, 1]
assert test["missingPeers"] == [2]
PY

if pgrep -f "$process_pattern" >/dev/null; then
	echo "FAIL: BoostMQ processes remain after the timeout" >&2
	exit 1
fi

if find /dev/shm -maxdepth 1 -type f -regextype posix-extended \
	-regex "$resource_regex" -print -quit | grep -q .; then
	echo "FAIL: BoostMQ queue resources remain after the timeout" >&2
	exit 1
fi

echo "PASS: readiness timeout is reported and all BoostMQ state is cleaned."
