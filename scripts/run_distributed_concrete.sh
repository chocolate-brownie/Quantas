#!/usr/bin/env bash
set -euo pipefail

make_usage() {
    cat <<'EOF'
This script is managed by the root makefile and is not meant to be run directly.

Use:
  make run_distributed_concrete INPUTFILE=<file> HOSTS_FILE=available_hosts.txt HOST_COUNT=5

Optional make variables:
  LEADER=<host> FOLLOWERS=<h1,h2,...> LEADER_INDEX=<n> PORT=<p> WORKDIR=<dir> ROOT_DIR=<dir>
EOF
}

require_make_wrapper() {
    if [[ "${QUANTAS_RUN_VIA_MAKE:-0}" != "1" ]]; then
        make_usage >&2
        exit 1
    fi
}

usage() {
    cat <<'EOF'
Usage:
  Internal helper called by make. Use `make run_distributed_concrete ...`

Required:
  -i, --input FILE            Input JSON path relative to the repo

Host Selection:
  -l, --leader HOST           SSH host for the leader process
  -f, --followers HOSTS       Comma-separated follower hosts
      --hosts-file FILE       File containing one host per line
      --count N               Use only the first N hosts from the hosts file, counting the leader
      --leader-index INDEX    Pick the leader from the hosts file by 1-based index (default: 1)

Optional:
  -p, --port PORT             Leader port (default: 5000)
  -w, --workdir DIR           Repo path on remote machines (default: current repo path)
      --root-dir DIR          Run root under the repo (default: experiments)
  -h, --help                  Show this help
EOF
}

require_make_wrapper

INPUTFILE=""
LEADER_HOST=""
FOLLOWERS_CSV=""
HOSTS_FILE=""
HOST_COUNT=""
LEADER_PORT="5000"
WORKDIR="$(pwd)"
RUN_ROOT_NAME="experiments"
LEADER_INDEX="1"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i|--input) INPUTFILE="$2"; shift 2 ;;
        -l|--leader) LEADER_HOST="$2"; shift 2 ;;
        -f|--followers) FOLLOWERS_CSV="$2"; shift 2 ;;
        --hosts-file) HOSTS_FILE="$2"; shift 2 ;;
        --count) HOST_COUNT="$2"; shift 2 ;;
        --leader-index) LEADER_INDEX="$2"; shift 2 ;;
        -p|--port) LEADER_PORT="$2"; shift 2 ;;
        -w|--workdir) WORKDIR="$2"; shift 2 ;;
        --root-dir) RUN_ROOT_NAME="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z "$INPUTFILE" ]]; then
    usage >&2
    exit 1
fi

FOLLOWER_HOSTS=()

if [[ -n "$FOLLOWERS_CSV" ]]; then
    IFS=',' read -r -a FOLLOWER_HOSTS <<< "$FOLLOWERS_CSV"
fi

if [[ -n "$HOSTS_FILE" ]]; then
    if [[ ! -f "$HOSTS_FILE" ]]; then
        echo "Hosts file not found: $HOSTS_FILE" >&2
        exit 1
    fi
    if [[ ! "$LEADER_INDEX" =~ ^[0-9]+$ ]] || [[ "$LEADER_INDEX" -lt 1 ]]; then
        echo "leader-index must be a positive integer." >&2
        exit 1
    fi

    mapfile -t ALL_HOSTS < <(
        while IFS= read -r host || [[ -n "$host" ]]; do
            host="${host%%#*}"
            host="$(printf '%s' "$host" | xargs)"
            if [[ -n "$host" ]]; then
                printf '%s\n' "$host"
            fi
        done < "$HOSTS_FILE"
    )

    if [[ ${#ALL_HOSTS[@]} -eq 0 ]]; then
        echo "Hosts file is empty: $HOSTS_FILE" >&2
        exit 1
    fi

    if [[ -n "$HOST_COUNT" ]]; then
        if [[ ! "$HOST_COUNT" =~ ^[0-9]+$ ]] || [[ "$HOST_COUNT" -lt 1 ]]; then
            echo "count must be a positive integer." >&2
            exit 1
        fi
        if [[ "$HOST_COUNT" -gt "${#ALL_HOSTS[@]}" ]]; then
            echo "Requested $HOST_COUNT hosts but only ${#ALL_HOSTS[@]} are available." >&2
            exit 1
        fi
        ALL_HOSTS=("${ALL_HOSTS[@]:0:$HOST_COUNT}")
    fi

    if [[ "$LEADER_INDEX" -gt "${#ALL_HOSTS[@]}" ]]; then
        echo "leader-index exceeds selected host count." >&2
        exit 1
    fi

    if [[ -z "$LEADER_HOST" ]]; then
        LEADER_HOST="${ALL_HOSTS[$((LEADER_INDEX - 1))]}"
    fi

    for host in "${ALL_HOSTS[@]}"; do
        if [[ "$host" != "$LEADER_HOST" ]]; then
            FOLLOWER_HOSTS+=("$host")
        fi
    done
fi

if [[ -z "$LEADER_HOST" ]]; then
    echo "Leader host is required unless it is selected from --hosts-file." >&2
    usage >&2
    exit 1
fi

if [[ ${#FOLLOWER_HOSTS[@]} -gt 0 ]]; then
    mapfile -t FOLLOWER_HOSTS < <(printf '%s\n' "${FOLLOWER_HOSTS[@]}" | awk '!seen[$0]++')
fi

if [[ ${#FOLLOWER_HOSTS[@]} -eq 0 || -z "${FOLLOWER_HOSTS[0]:-}" ]]; then
    echo "At least one follower host is required." >&2
    exit 1
fi

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
REMOTE_RUN_DIR="${WORKDIR}/${RUN_ROOT_NAME}/${TIMESTAMP}"
REMOTE_QUANTAS_DIR="${REMOTE_RUN_DIR}/quantas"
REMOTE_LAUNCHER_DIR="${REMOTE_RUN_DIR}/launcher"

runtime_env() {
    local role="$1"
    local host="$2"
    printf 'QUANTAS_RUN_DIR=%q QUANTAS_HOSTNAME=%q QUANTAS_PROCESS_ROLE=%q' \
        "$REMOTE_RUN_DIR" "$host" "$role"
}

REMOTE_LEADER_CMD="$(runtime_env leader "$LEADER_HOST") make MODE=concrete run INPUTFILE=${INPUTFILE} PORT=${LEADER_PORT}"
REMOTE_FOLLOWER_CMD_BASE="make MODE=concrete run INPUTFILE=${INPUTFILE}"

prepare_remote_dirs() {
    local host="$1"
    ssh -n "$host" "mkdir -p $(printf '%q' "$REMOTE_QUANTAS_DIR") $(printf '%q' "$REMOTE_LAUNCHER_DIR")"
}

run_remote() {
    local host="$1"
    local command="$2"
    prepare_remote_dirs "$host"
    ssh -n "$host" "cd $(printf '%q' "$WORKDIR") && ${command}"
}

write_remote_script() {
    local host="$1"
    local role="$2"
    local command="$3"
    local safe_host="${host//\//_}"
    local launcher_log="${REMOTE_LAUNCHER_DIR}/${role}__${safe_host}.launcher.log"
    local remote_script="/tmp/quantas_${TIMESTAMP}_${role}_${safe_host}.sh"

    prepare_remote_dirs "$host"
    ssh -n "$host" "cat > $(printf '%q' "$remote_script") <<EOF
#!/usr/bin/env bash
set -euo pipefail
trap 'rm -f $(printf '%q' "$remote_script")' EXIT
cd $(printf '%q' "$WORKDIR")
export QUANTAS_RUN_DIR=$(printf '%q' "$REMOTE_RUN_DIR")
export QUANTAS_HOSTNAME=$(printf '%q' "$host")
export QUANTAS_PROCESS_ROLE=$(printf '%q' "$role")
exec >> $(printf '%q' "$launcher_log") 2>&1
echo \"[\$(date +%Y-%m-%dT%H:%M:%S)] starting ${role} on \$(hostname -f 2>/dev/null || hostname)\"
${command}
status=\$?
echo \"[\$(date +%Y-%m-%dT%H:%M:%S)] finished ${role} with status \$status\"
exit \$status
EOF
chmod +x $(printf '%q' "$remote_script")"
}

start_remote_background() {
    local host="$1"
    local role="$2"
    local command="$3"
    local safe_host="${host//\//_}"
    local remote_script="/tmp/quantas_${TIMESTAMP}_${role}_${safe_host}.sh"

    write_remote_script "$host" "$role" "$command"
    ssh -f -n "$host" "setsid $(printf '%q' "$remote_script") >/dev/null 2>&1 </dev/null & exit 0"
    echo "started ${role} on ${host}"
}

stop_remote() {
    local host="$1"
    ssh -n "$host" "cd $(printf '%q' "$WORKDIR") && make kill"
}

echo "Input file:    $INPUTFILE"
echo "Leader host:   $LEADER_HOST:$LEADER_PORT"
echo "Followers:     ${FOLLOWER_HOSTS[*]}"
echo "Remote repo:   $WORKDIR"
echo "Run directory: $REMOTE_RUN_DIR"
echo "QUANTAS logs:  $REMOTE_QUANTAS_DIR"
echo "Launcher logs: $REMOTE_LAUNCHER_DIR"

echo "Building locally..."
(cd "$WORKDIR" && make MODE=concrete release "INPUTFILE=${INPUTFILE}")

echo "Cleaning stale runs on selected hosts..."
stop_remote "$LEADER_HOST"
for host in "${FOLLOWER_HOSTS[@]}"; do
    stop_remote "$host"
done

echo "Starting leader on ${LEADER_HOST}..."
start_remote_background "$LEADER_HOST" "leader" "$REMOTE_LEADER_CMD"
sleep 2

for host in "${FOLLOWER_HOSTS[@]}"; do
    remote_follower_cmd="$(runtime_env follower "$host") ${REMOTE_FOLLOWER_CMD_BASE}"
    echo "Starting follower on ${host}..."
    start_remote_background "$host" "follower" "$remote_follower_cmd"
    sleep 1
done

cat <<EOF
Launch complete.

Run root:
  ${REMOTE_RUN_DIR}

Monitor QUANTAS logs:
  See experiments folder on machine ${LEADER_HOST}:
  ${REMOTE_QUANTAS_DIR}

Launcher diagnostics, if needed:
  ${REMOTE_LAUNCHER_DIR}
EOF
