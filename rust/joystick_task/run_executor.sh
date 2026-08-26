#!/usr/bin/env bash
# Launches joystick_intro_executor with stdout/stderr captured to a
# timestamped log file (in addition to the terminal), so a wedge/crash can
# be diagnosed after the fact instead of only in the live terminal scrollback.
#
# Usage: same args as the binary itself, e.g.:
#   ./run_executor.sh --thalamus http://127.0.0.1:50050
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p logs
log_file="logs/executor_$(date +%Y%m%d_%H%M%S).log"
echo "Logging to $log_file"

exec ./target/release/joystick_intro_executor "$@" 2>&1 | tee "$log_file"
