#!/bin/bash
# Compute per-channel RMS noise + FFT spectra for one or more Magnify ROOT files.
# Produces <file>.rms.root alongside each input (contains RMS TTrees and FFT TH2Fs).
#
# Usage:
#   ./scripts/run_rms_analysis.sh input_files/040475_1/magnify-run040475-evt1-anode0.root
#   ./scripts/run_rms_analysis.sh input_files/040475_1/magnify-run040475-evt1-anode*.root
#   ./scripts/run_rms_analysis.sh input_files/*/magnify-*.root

set -euo pipefail

magnify_source="$(dirname "$(readlink -f "$BASH_SOURCE")")"

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <magnify_file.root> [<magnify_file.root> ...]"
    exit 1
fi

cd "$magnify_source"   # scripts/ — required for loadClasses.C relative paths

for inFile in "$@"; do
    absFile="$(readlink -f "$inFile")"
    echo "Processing: $absFile"
    root -b -q "run_rms_analysis.C(\"$absFile\")"
done

echo "Done."
