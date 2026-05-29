#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${NS3_ROOT}"

# Usage:
#   bash scratch/P-OLSR/run_olsr_polsr_three_scenarios.sh
#   bash scratch/P-OLSR/run_olsr_polsr_three_scenarios.sh scratch/P-OLSR/protocol-results/my_compare
#
# Override scenarios if needed:
#   SCENARIOS="field large link" bash scratch/P-OLSR/run_olsr_polsr_three_scenarios.sh
#
# Important:
#   polsr_protocol_experiment.cc must explicitly support every scenario listed
#   here. Otherwise the script exits before running, to avoid mislabeled CSV rows.

OUTPUT_DIR="${1:-scratch/P-OLSR/protocol-results/compare_olsr_polsr_$(date +%Y%m%d_%H%M%S)}"
SCENARIOS_TEXT="${SCENARIOS:-link field large}"
PROTOCOLS=("OLSR" "P-OLSR")
EXPERIMENT_SRC="scratch/P-OLSR/polsr_protocol_experiment.cc"
SUMMARY_CSV="${OUTPUT_DIR}/polsr_protocol_summary.csv"
FIGURE_DIR="scratch/P-OLSR/figures/$(basename "${OUTPUT_DIR}")"

read -r -a SCENARIOS_ARRAY <<< "${SCENARIOS_TEXT}"

if [[ ${#SCENARIOS_ARRAY[@]} -ne 3 ]]; then
  echo "ERROR: This script is intended for exactly three scenarios."
  echo "Current SCENARIOS='${SCENARIOS_TEXT}'"
  exit 1
fi

for scenario in "${SCENARIOS_ARRAY[@]}"; do
  if [[ "${scenario}" == "link" ]]; then
    if grep -q "LinkPosition" "${EXPERIMENT_SRC}" && grep -q "link, field, or large" "${EXPERIMENT_SRC}"; then
      continue
    fi
  fi

  if [[ "${scenario}" == "field" ]]; then
    if grep -q "FieldPosition" "${EXPERIMENT_SRC}"; then
      continue
    fi
  fi

  if [[ "${scenario}" == "large" ]]; then
    if grep -q "LargePosition" "${EXPERIMENT_SRC}" && grep -q "link, field, or large" "${EXPERIMENT_SRC}"; then
      continue
    fi
  fi

  if ! grep -q "scenario == \"${scenario}\"" "${EXPERIMENT_SRC}"; then
    echo "ERROR: Scenario '${scenario}' is not explicitly supported by ${EXPERIMENT_SRC}."
    echo "Current protocol experiment code would not create a real '${scenario}' model."
    echo "Fix polsr_protocol_experiment.cc first, or run with supported scenarios only, for example:"
    echo "  SCENARIOS=\"field large <third_supported_scenario>\" bash $0"
    exit 1
  fi
done

if [[ -e "${SUMMARY_CSV}" ]]; then
  echo "ERROR: Output CSV already exists:"
  echo "  ${SUMMARY_CSV}"
  echo "Choose a new output directory to avoid mixing old and new results."
  exit 1
fi

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

echo "Output directory:"
echo "  ${OUTPUT_DIR}"
echo "Scenarios:"
printf "  %s\n" "${SCENARIOS_ARRAY[@]}"
echo "Protocols:"
printf "  %s\n" "${PROTOCOLS[@]}"

./ns3 run "scratch/P-OLSR/polsr_header_smoke_test"

for scenario in "${SCENARIOS_ARRAY[@]}"; do
  for protocol in "${PROTOCOLS[@]}"; do
    echo "Running scenario=${scenario}, protocol=${protocol}"
    ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment --scenario=${scenario} --protocol=${protocol} --outputDir=${OUTPUT_DIR}"
  done
done

python3 scratch/P-OLSR/plot_results.py \
  --base-dir scratch/P-OLSR \
  --protocol-csv "${SUMMARY_CSV}" \
  --figure-dir "${FIGURE_DIR}"

echo "Done."
echo "CSV:"
echo "  ${SUMMARY_CSV}"
echo "Figures:"
echo "  ${FIGURE_DIR}"
