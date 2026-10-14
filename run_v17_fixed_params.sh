#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${NS3_ROOT}"

RUNS="${RUNS:-1}"
VERSION_PREFIX="${VERSION_PREFIX:-V1.7}"
HI="${HI:-1.0}"
ALPHA="${ALPHA:-0.1}"
BETA="${BETA:-0.05}"
GAMMA="${GAMMA:-0.04}"
HELLO_DEADLINE_FACTOR="${HELLO_DEADLINE_FACTOR:-1.4}"
SCENARIO="${SCENARIO:-large}"
WIRELESS_PROFILE="${WIRELESS_PROFILE:-paper-dlr}"
PAPER_DLR_MAX_EXTRA_LOSS_DB="${PAPER_DLR_MAX_EXTRA_LOSS_DB:-12.0}"
OUTAGE_WINDOW="${OUTAGE_WINDOW:-1.0}"
USE_PREDICTION_FOR_MPR="${USE_PREDICTION_FOR_MPR:-false}"

POSITIONAL_OUTPUT_DIR=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --alpha)
            ALPHA="$2"
            shift 2
            ;;
        --beta)
            BETA="$2"
            shift 2
            ;;
        --gamma|--gama)
            GAMMA="$2"
            shift 2
            ;;
        --hi|--hello-interval)
            HI="$2"
            shift 2
            ;;
        --hello-deadline-factor)
            HELLO_DEADLINE_FACTOR="$2"
            shift 2
            ;;
        --runs)
            RUNS="$2"
            shift 2
            ;;
        --scenario)
            SCENARIO="$2"
            shift 2
            ;;
        --wireless-profile)
            WIRELESS_PROFILE="$2"
            shift 2
            ;;
        --paper-dlr-max-extra-loss-db)
            PAPER_DLR_MAX_EXTRA_LOSS_DB="$2"
            shift 2
            ;;
        --outage-window)
            OUTAGE_WINDOW="$2"
            shift 2
            ;;
        --use-prediction-for-mpr)
            USE_PREDICTION_FOR_MPR="$2"
            shift 2
            ;;
        --output-dir)
            POSITIONAL_OUTPUT_DIR="$2"
            shift 2
            ;;
        --version-prefix)
            VERSION_PREFIX="$2"
            shift 2
            ;;
        -*)
            echo "ERROR: Unknown option: $1"
            exit 1
            ;;
        *)
            POSITIONAL_OUTPUT_DIR="$1"
            shift
            ;;
    esac
done

OUTPUT_DIR="${POSITIONAL_OUTPUT_DIR:-scratch/P-OLSR/protocol-results/${VERSION_PREFIX}_fixed_alpha_${ALPHA}_beta_${BETA}_gamma_${GAMMA}_HI_${HI}_deadline_${HELLO_DEADLINE_FACTOR}HI_${RUNS}runs}"
FIGURE_DIR="scratch/P-OLSR/figures/$(basename "${OUTPUT_DIR}")"
SUMMARY_CSV="${OUTPUT_DIR}/polsr_protocol_summary.csv"
TIMESERIES_CSV="${OUTPUT_DIR}/polsr_goodput_timeseries.csv"
AVERAGE_CSV="${OUTPUT_DIR}/polsr_protocol_average.csv"
TIMESERIES_AVERAGE_CSV="${OUTPUT_DIR}/polsr_goodput_timeseries_average.csv"

if [[ -e "${SUMMARY_CSV}" || -e "${TIMESERIES_CSV}" || -e "${AVERAGE_CSV}" || -e "${TIMESERIES_AVERAGE_CSV}" ]]; then
    echo "ERROR: Output files already exist under:"
    echo "  ${OUTPUT_DIR}"
    echo "Choose a new output directory to avoid mixing old and new results."
    exit 1
fi

if ! ./ns3 show targets 2>/dev/null | grep "polsr_protocol_experiment" >/dev/null; then
    echo "Refreshing ns-3 CMake target cache for scratch/P-OLSR..."
    ./ns3 configure
fi

mkdir -p "${OUTPUT_DIR}" "${FIGURE_DIR}"

COMMON_ARGS="--scenario=${SCENARIO} \
--helloInterval=${HI} \
--predictionAlpha=${ALPHA} \
--predictionBeta=${BETA} \
--predictionGamma=${GAMMA} \
--helloDeadlineFactor=${HELLO_DEADLINE_FACTOR} \
--usePredictionForMpr=${USE_PREDICTION_FOR_MPR} \
--outageWindow=${OUTAGE_WINDOW} \
--wirelessProfile=${WIRELESS_PROFILE} \
--paperDlrMaxExtraLossDb=${PAPER_DLR_MAX_EXTRA_LOSS_DB} \
--outputDir=${OUTPUT_DIR}"

for run in $(seq 1 "${RUNS}"); do
    echo "[fixed alpha=${ALPHA}, beta=${BETA}, gamma=${GAMMA}] run ${run}/${RUNS}: OLSR-ETX"
    ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment \
--protocol=OLSR-ETX \
--runIndex=$((1000 + run)) \
--rngRun=${run} \
${COMMON_ARGS}"

    echo "[fixed alpha=${ALPHA}, beta=${BETA}, gamma=${GAMMA}] run ${run}/${RUNS}: P-OLSR"
    ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment \
--protocol=P-OLSR \
--runIndex=$((2000 + run)) \
--rngRun=${run} \
${COMMON_ARGS}"
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
echo "Output directory: ${OUTPUT_DIR}"
echo "Figure directory: ${FIGURE_DIR}"
echo "Average summary:"
echo "  ${AVERAGE_CSV}"
