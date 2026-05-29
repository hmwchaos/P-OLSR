#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${NS3_ROOT}"
OUTPUT_DIR="${1:-scratch/P-OLSR/protocol-results}"

if ! ./ns3 show targets 2>/dev/null | grep "polsr_protocol_experiment" >/dev/null || \
   ! ./ns3 show targets 2>/dev/null | grep "polsr_header_smoke_test" >/dev/null; then
  echo "Refreshing ns-3 CMake target cache for scratch/P-OLSR..."
  ./ns3 configure
fi

if ! ./ns3 show targets | grep "polsr_protocol_experiment" >/dev/null || \
   ! ./ns3 show targets | grep "polsr_header_smoke_test" >/dev/null; then
  echo "ERROR: P-OLSR protocol targets are still not registered."
  echo "Run './ns3 show targets | grep polsr' and check scratch/P-OLSR/CMakeLists.txt."
  exit 1
fi

./ns3 run "scratch/P-OLSR/polsr_header_smoke_test"

for scenario in link field large; do
  for protocol in OLSR P-OLSR; do
    ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment --scenario=${scenario} --protocol=${protocol} --outputDir=${OUTPUT_DIR}"
  done
done

python3 scratch/P-OLSR/plot_results.py --protocol-csv "${OUTPUT_DIR}/polsr_protocol_summary.csv"

echo "Protocol-level results:"
echo "  ${OUTPUT_DIR}/polsr_protocol_summary.csv"
echo "Figures:"
echo "  scratch/P-OLSR/figures"
