#!/bin/bash
# Separate EmulationStation / PortMaster entry for the automated GE benchmark.
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd -P)"
exec "$SCRIPT_DIR/GoldenEye 007.sh" --benchmark-suite
