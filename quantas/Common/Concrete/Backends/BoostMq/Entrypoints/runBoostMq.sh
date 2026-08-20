#!/usr/bin/env bash

set -u

if [[ $# -ne 4 ]]; then
	echo "usage: $0 INPUTFILE LEADER_EXE PEER_EXE RESOURCE_DIR" >&2
	exit 1
fi

input_file=$1
leader_exe=$2
peer_exe=$3
resource_dir=$4
resource_regex='.*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))'

leader_pid=""
peer_pids=()

remove_resources() {
	local resource
	while IFS= read -r resource; do
		rm -f -- "$resource"
	done < <(
		find "$resource_dir" -maxdepth 1 -type f \
			-regextype posix-extended -regex "$resource_regex" -print
	)
}

terminate_children() {
	local entry

	if [[ -n $leader_pid ]]; then
		kill "$leader_pid" 2>/dev/null || true
	fi
	for entry in "${peer_pids[@]}"; do
		kill "${entry##*:}" 2>/dev/null || true
	done

	if [[ -n $leader_pid ]]; then
		wait "$leader_pid" 2>/dev/null || true
	fi
	for entry in "${peer_pids[@]}"; do
		wait "${entry##*:}" 2>/dev/null || true
	done

	remove_resources
}

interrupt_run() {
	terminate_children
	exit 130
}

"$leader_exe" --preflight "$input_file" || exit $?

plan=$(python3 -c '
import json
import sys

config = json.load(open(sys.argv[1]))
for index, experiment in enumerate(config["experiments"]):
    print(index, experiment["topology"]["initialPeers"])
' "$input_file") || exit $?

if [[ -z $plan ]]; then
	echo "error: no experiments found in $input_file" >&2
	exit 1
fi

trap interrupt_run INT TERM HUP

while read -r experiment_index total_peers; do
	echo "[mq] experiment $experiment_index: peers=$total_peers input=$input_file"
	leader_pid=""
	peer_pids=()

	"$leader_exe" --experiment "$experiment_index" "$input_file" &
	leader_pid=$!
	echo "[mq] started leader pid=$leader_pid"
	sleep 0.2

	for ((peer_id = 0; peer_id < total_peers; ++peer_id)); do
		"$peer_exe" --experiment "$experiment_index" "$input_file" "$peer_id" &
		peer_pid=$!
		echo "[mq] started peer $peer_id pid=$peer_pid"
		peer_pids+=("$peer_id:$peer_pid")
	done

	wait "$leader_pid"
	leader_status=$?
	echo "[mq] leader (pid $leader_pid) exit code=$leader_status"
	overall_status=$leader_status

	if [[ $leader_status -ne 0 ]]; then
		terminate_children
		trap - INT TERM HUP
		exit "$overall_status"
	fi

	for entry in "${peer_pids[@]}"; do
		peer_id=${entry%%:*}
		peer_pid=${entry##*:}
		wait "$peer_pid"
		peer_status=$?
		echo "[mq] peer $peer_id (pid $peer_pid) exit code=$peer_status"
		if [[ $peer_status -ne 0 && $overall_status -eq 0 ]]; then
			overall_status=$peer_status
		fi
	done

	leader_pid=""
	peer_pids=()
	if [[ $overall_status -ne 0 ]]; then
		remove_resources
		exit "$overall_status"
	fi
done <<< "$plan"

trap - INT TERM HUP
