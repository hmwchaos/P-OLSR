#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${NS3_ROOT}"

OUTPUT_DIR="${1:-scratch/P-OLSR/protocol-results/fig15_goodput_$(date +%Y%m%d_%H%M%S)}"
RUNS="${RUNS:-10}"
SUMMARY_CSV="${OUTPUT_DIR}/polsr_protocol_summary.csv"
TIMESERIES_CSV="${OUTPUT_DIR}/polsr_goodput_timeseries.csv"
AVERAGE_CSV="${OUTPUT_DIR}/polsr_protocol_average.csv"
TIMESERIES_AVERAGE_CSV="${OUTPUT_DIR}/polsr_goodput_timeseries_average.csv"
FIGURE_DIR="scratch/P-OLSR/figures/$(basename "${OUTPUT_DIR}")"

if [[ -e "${SUMMARY_CSV}" || -e "${TIMESERIES_CSV}" || -e "${AVERAGE_CSV}" || -e "${TIMESERIES_AVERAGE_CSV}" ]]; then
  echo "ERROR: Output files already exist under:"
  echo "  ${OUTPUT_DIR}"
  echo "Choose a new output directory to avoid mixing old and new results."
  exit 1
fi

if ! ./ns3 show targets 2>/dev/null | grep "polsr_protocol_experiment" >/dev/null || \
   ! ./ns3 show targets 2>/dev/null | grep "polsr_header_smoke_test" >/dev/null; then
  echo "Refreshing ns-3 CMake target cache for scratch/P-OLSR..."
  ./ns3 configure
fi

./ns3 run "scratch/P-OLSR/polsr_header_smoke_test"

COMMON_ARGS="--scenario=large --helloInterval=1.0 --predictionAlpha=0.1 --predictionBeta=0.2 --predictionGamma=0.08 --outageWindow=1.0 --wirelessProfile=paper-dlr --paperDlrMaxExtraLossDb=12.0 --outputDir=${OUTPUT_DIR}"

for run in $(seq 1 "${RUNS}"); do
  echo "Running native OLSR run ${run}/${RUNS} with HI=1.0 reference condition"
  ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment --protocol=OLSR --runIndex=${run} --rngRun=${run} ${COMMON_ARGS}"

  echo "Running OLSR-ETX run ${run}/${RUNS} with HI=1.0, alpha=0.1 reference condition"
  ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment --protocol=OLSR-ETX --runIndex=${run} --rngRun=${run} ${COMMON_ARGS}"

  echo "Running P-OLSR run ${run}/${RUNS} with HI=1.0, alpha=0.1, beta=0.2, gamma=0.08, UsePredictionForMpr=false"
  ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment --protocol=P-OLSR --usePredictionForMpr=false --runIndex=${run} --rngRun=${run} ${COMMON_ARGS}"
done

python3 scratch/P-OLSR/summarize_protocol_results.py \
  --summary-csv "${SUMMARY_CSV}" \
  --timeseries-csv "${TIMESERIES_CSV}" \
  --output-dir "${OUTPUT_DIR}"

python3 scratch/P-OLSR/plot_results.py \
  --base-dir scratch/P-OLSR \
  --protocol-csv "${AVERAGE_CSV}" \
  --timeseries-csv "${TIMESERIES_AVERAGE_CSV}" \
  --figure-dir "${FIGURE_DIR}"

echo "Done."
echo "CSV summary:"
echo "  ${SUMMARY_CSV}"
echo "Goodput time series:"
echo "  ${TIMESERIES_CSV}"
echo "Average summary:"
echo "  ${AVERAGE_CSV}"
echo "Average goodput time series:"
echo "  ${TIMESERIES_AVERAGE_CSV}"
echo "Figures:"
echo "  ${FIGURE_DIR}"
