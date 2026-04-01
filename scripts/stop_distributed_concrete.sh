#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  stop_distributed_concrete.sh [host options] [options]

Host Selection:
  -H, --hosts HOSTS           Comma-separated ssh hosts to stop
      --hosts-file FILE       File containing one host per line
      --count N               Use only the first N hosts from the hosts file

Optional:
  -w, --workdir DIR           Repo path on remote machines (default: current repo path)
  -h, --help                  Show this help

Example:
  ./scripts/stop_distributed_concrete.sh -H eon1,eon2,eon3,eon4,eon5
  ./scripts/stop_distributed_concrete.sh --hosts-file scripts/available_hosts.txt
EOF
}

HOSTS_CSV=""
HOSTS_FILE=""
HOST_COUNT=""
WORKDIR="$(pwd)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -H|--hosts)
            HOSTS_CSV="$2"
            shift 2
            ;;
        --hosts-file)
            HOSTS_FILE="$2"
            shift 2
            ;;
        --count)
            HOST_COUNT="$2"
            shift 2
            ;;
        -w|--workdir)
            WORKDIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

HOSTS=()

if [[ -n "$HOSTS_CSV" ]]; then
    IFS=',' read -r -a HOSTS <<< "$HOSTS_CSV"
fi

if [[ -n "$HOSTS_FILE" ]]; then
    if [[ ! -f "$HOSTS_FILE" ]]; then
        echo "Hosts file not found: $HOSTS_FILE" >&2
        exit 1
    fi
    while IFS= read -r host || [[ -n "$host" ]]; do
        host="${host%%#*}"
        host="$(printf '%s' "$host" | xargs)"
        if [[ -z "$host" ]]; then
            continue
        fi
        HOSTS+=("$host")
    done < "$HOSTS_FILE"
fi

if [[ ${#HOSTS[@]} -gt 0 ]]; then
    mapfile -t HOSTS < <(printf '%s\n' "${HOSTS[@]}" | awk '!seen[$0]++')
fi

if [[ -n "$HOST_COUNT" ]]; then
    if [[ ! "$HOST_COUNT" =~ ^[0-9]+$ ]] || [[ "$HOST_COUNT" -lt 1 ]]; then
        echo "count must be a positive integer." >&2
        exit 1
    fi
    if [[ "$HOST_COUNT" -gt "${#HOSTS[@]}" ]]; then
        echo "Requested $HOST_COUNT hosts but only ${#HOSTS[@]} are available." >&2
        exit 1
    fi
    HOSTS=("${HOSTS[@]:0:$HOST_COUNT}")
fi

if [[ ${#HOSTS[@]} -eq 0 ]]; then
    usage >&2
    exit 1
fi

for host in "${HOSTS[@]}"; do
    echo "Stopping concrete run on ${host}..."
    ssh -n "$host" "cd $(printf '%q' "$WORKDIR") && make kill"
done

echo "Stop command sent to: ${HOSTS[*]}"
