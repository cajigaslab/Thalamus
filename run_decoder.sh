#!/usr/bin/env bash
# Launch the closed-loop neural decoder for a BMI session.
#
# The decoder is opt-in: it is NOT in eevee.json Orchestration.Processes, so normal
# behavior runs never start it. Run this only when you want closed-loop control, with
# the Thalamus stack up. It reconnects on core restart and waits if the core isn't up
# yet, so timing is not critical. Ctrl-C to stop.
#
# Any extra args are forwarded to neural_decoder.py, e.g.:
#   ./run_decoder.sh                                  # position mode (task 'direct')
#   ./run_decoder.sh --emit-mode velocity --gain 2.0  # velocity mode (task 'cumulative')
#   ./run_decoder.sh --band 1,300 --window-ms 150     # bandpass + shorter window
#
# Then point a joystick task's joystick_node at "Decoder" in the operator UI.
set -euo pipefail
cd "$(dirname "$0")"
exec ./.venv/bin/python -u neural_decoder.py "$@"
