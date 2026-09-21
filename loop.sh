#!/bin/bash
# Loop script for the GLT CPU ray tracer agent
set -uo pipefail

WORK="/root/glt-cpu"
BIN="/root/.opencode/bin/opencode"
MODEL="opencode/muse-spark-1.3-contributor-free"

mkdir -p "$WORK/logs"

cd "$WORK"
while true; do
    echo "=== run $(date) ===" >> "$WORK/logs/glt.log"
    PROMPT="You are working on /root/glt-cpu. Read AGENT.md for full instructions. Work on the next unfinished task. Do not ask for permission. After each change: compile with make, test by running ./glt, fix any errors. Commit and push significant progress. Keep going until the project is complete with screenshots."
    timeout 3600 "$BIN" run --auto --dir "$WORK" --model "$MODEL" "$PROMPT" 2>&1 >> "$WORK/logs/glt.log" || true
    sleep 5
done
