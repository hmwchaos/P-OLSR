#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${NS3_ROOT}"

RUNS="${RUNS:-10}"
VERSION_PREFIX="${VERSION_PREFIX:-V1.7}"
HI="${HI:-1.0}"
ALPHA="${ALPHA:-0.2}"
BETA="${BETA:-0.2}"
GAMMA="${GAMMA:-0.08}"
HELLO_DEADLINE_FACTOR="${HELLO_DEADLINE_FACTOR:-1.4}"
SCENARIO="${SCENARIO:-large}"
WIRELESS_PROFILE="${WIRELESS_PROFILE:-paper-dlr}"
PAPER_DLR_MAX_EXTRA_LOSS_DB="${PAPER_DLR_MAX_EXTRA_LOSS_DB:-12.0}"
OUTAGE_WINDOW="${OUTAGE_WINDOW:-1.0}"
USE_PREDICTION_FOR_MPR="${USE_PREDICTION_FOR_MPR:-false}"

ALPHA_VALUES="${ALPHA_VALUES:-0.05 0.1 0.2 0.3 0.4}"
BETA_VALUES="${BETA_VALUES:-0.05 0.1 0.2 0.3 0.4}"
GAMMA_VALUES="${GAMMA_VALUES:-0.02 0.04 0.08 0.12 0.16}"

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
        --alpha-values)
            ALPHA_VALUES="$2"
            shift 2
            ;;
        --beta-values)
            BETA_VALUES="$2"
            shift 2
            ;;
        --gamma-values|--gama-values)
            GAMMA_VALUES="$2"
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

OUTPUT_DIR="${POSITIONAL_OUTPUT_DIR:-scratch/P-OLSR/protocol-results/${VERSION_PREFIX}_outage_sweeps_HI_${HI}_deadline_${HELLO_DEADLINE_FACTOR}HI_${RUNS}runs}"
FIGURE_DIR="scratch/P-OLSR/figures/$(basename "${OUTPUT_DIR}")"
SUMMARY_CSV="${OUTPUT_DIR}/polsr_protocol_summary.csv"
TIMESERIES_CSV="${OUTPUT_DIR}/polsr_goodput_timeseries.csv"
AVERAGE_CSV="${OUTPUT_DIR}/polsr_protocol_average.csv"
SWEEP_CSV="${OUTPUT_DIR}/polsr_outage_sweep_average.csv"
SWEEP_CASES_CSV="${OUTPUT_DIR}/polsr_outage_sweep_cases.csv"

if [[ -e "${SUMMARY_CSV}" || -e "${TIMESERIES_CSV}" || -e "${AVERAGE_CSV}" || -e "${SWEEP_CSV}" ]]; then
    echo "ERROR: Output files already exist under:"
    echo "  ${OUTPUT_DIR}"
    echo "Choose a new output directory to avoid mixing old and new results."
    exit 1
fi

mkdir -p "${OUTPUT_DIR}" "${FIGURE_DIR}"
echo "run_index,rng_run,sweep,value,protocol,alpha,beta,gamma" > "${SWEEP_CASES_CSV}"

CASE_INDEX=0

run_case() {
    local sweep_name="$1"
    local sweep_value="$2"
    local protocol="$3"
    local alpha="$4"
    local beta="$5"
    local gamma="$6"
    local run_index="$7"
    local rng_run="$8"

    echo "${run_index},${rng_run},${sweep_name},${sweep_value},${protocol},${alpha},${beta},${gamma}" >> "${SWEEP_CASES_CSV}"

    ./ns3 run "scratch/P-OLSR/polsr_protocol_experiment \
--scenario=${SCENARIO} \
--protocol=${protocol} \
--helloInterval=${HI} \
--predictionAlpha=${alpha} \
--predictionBeta=${beta} \
--predictionGamma=${gamma} \
--helloDeadlineFactor=${HELLO_DEADLINE_FACTOR} \
--usePredictionForMpr=${USE_PREDICTION_FOR_MPR} \
--outageWindow=${OUTAGE_WINDOW} \
--wirelessProfile=${WIRELESS_PROFILE} \
--paperDlrMaxExtraLossDb=${PAPER_DLR_MAX_EXTRA_LOSS_DB} \
--runIndex=${run_index} \
--rngRun=${rng_run} \
--outputDir=${OUTPUT_DIR}"
}

run_sweep_value() {
    local sweep_name="$1"
    local sweep_value="$2"
    local alpha="$3"
    local beta="$4"
    local gamma="$5"

    for run in $(seq 1 "${RUNS}"); do
        CASE_INDEX=$((CASE_INDEX + 1))
        local etx_run_index=$((CASE_INDEX * 1000 + run))
        echo "[${sweep_name}=${sweep_value}] run ${run}/${RUNS}: OLSR-ETX"
        run_case "${sweep_name}" "${sweep_value}" "OLSR-ETX" "${alpha}" "${beta}" "${gamma}" "${etx_run_index}" "${run}"
        CASE_INDEX=$((CASE_INDEX + 1))
        local polsr_run_index=$((CASE_INDEX * 1000 + run))
        echo "[${sweep_name}=${sweep_value}] run ${run}/${RUNS}: P-OLSR"
        run_case "${sweep_name}" "${sweep_value}" "P-OLSR" "${alpha}" "${beta}" "${gamma}" "${polsr_run_index}" "${run}"
    done
}

for value in ${ALPHA_VALUES}; do
    run_sweep_value "alpha" "${value}" "${value}" "${BETA}" "${GAMMA}"
done

for value in ${BETA_VALUES}; do
    run_sweep_value "beta" "${value}" "${ALPHA}" "${value}" "${GAMMA}"
done

for value in ${GAMMA_VALUES}; do
    run_sweep_value "gamma" "${value}" "${ALPHA}" "${BETA}" "${value}"
done

python3 scratch/P-OLSR/summarize_protocol_results.py \
    --summary-csv "${SUMMARY_CSV}" \
    --timeseries-csv "${TIMESERIES_CSV}" \
    --output-dir "${OUTPUT_DIR}"

python3 - "${SUMMARY_CSV}" "${SWEEP_CASES_CSV}" "${SWEEP_CSV}" "${FIGURE_DIR}/outage_sweeps.svg" <<'PY'
import csv
import math
import sys
from collections import defaultdict

summary_csv, sweep_cases_csv, sweep_csv, figure_path = sys.argv[1:5]

case_by_key = {}
with open(sweep_cases_csv, newline="", encoding="utf-8") as stream:
    for row in csv.DictReader(stream):
        case_by_key[(row.get("run_index", ""), row.get("protocol", ""))] = row

groups = defaultdict(list)
with open(summary_csv, newline="", encoding="utf-8") as stream:
    for row in csv.DictReader(stream):
        protocol = row.get("protocol", "")
        if protocol not in ("OLSR-ETX", "P-OLSR"):
            continue
        case = case_by_key.get((row.get("run_index", ""), protocol))
        if case is None:
            continue
        try:
            sweep = case["sweep"]
            value = float(case["value"])
            outage = float(row.get("outage_time_s", "nan"))
            goodput = float(row.get("goodput_mbps", "nan"))
            dlr = float(row.get("dlr", "nan"))
            delay = float(row.get("mean_delay_ms", "nan"))
        except ValueError:
            continue
        groups[(sweep, value, protocol)].append((outage, goodput, dlr, delay))

rows = []
for (sweep, value, protocol), values in sorted(groups.items()):
    count = len(values)
    avg_outage = sum(v[0] for v in values) / count
    avg_goodput = sum(v[1] for v in values) / count
    avg_dlr = sum(v[2] for v in values) / count
    avg_delay = sum(v[3] for v in values) / count
    rows.append(
        {
            "sweep": sweep,
            "value": value,
            "protocol": protocol,
            "runs": count,
            "avg_outage_time_s": avg_outage,
            "avg_goodput_mbps": avg_goodput,
            "avg_dlr": avg_dlr,
            "avg_mean_delay_ms": avg_delay,
        }
    )

with open(sweep_csv, "w", newline="", encoding="utf-8") as stream:
    fieldnames = [
        "sweep",
        "value",
        "protocol",
        "runs",
        "avg_outage_time_s",
        "avg_goodput_mbps",
        "avg_dlr",
        "avg_mean_delay_ms",
    ]
    writer = csv.DictWriter(stream, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

panels = ["alpha", "beta", "gamma"]
colors = {"OLSR-ETX": "#d95f02", "P-OLSR": "#1b9e77"}
width, height = 1080, 420
margin_left, margin_top, panel_gap = 60, 55, 45
panel_w = (width - margin_left - 30 - panel_gap * 2) / 3
panel_h = 280

def points_for(sweep, protocol):
    pts = [
        (r["value"], r["avg_outage_time_s"])
        for r in rows
        if r["sweep"] == sweep and r["protocol"] == protocol
    ]
    return sorted(pts)

def scale(value, domain_min, domain_max, range_min, range_max):
    if domain_max == domain_min:
        return (range_min + range_max) / 2
    return range_min + (value - domain_min) * (range_max - range_min) / (domain_max - domain_min)

svg = [
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
    '<rect width="100%" height="100%" fill="white"/>',
    '<text x="540" y="28" text-anchor="middle" font-family="Arial" font-size="20" font-weight="bold">V1.7 Outage Sweeps at Fixed HI</text>',
]

for idx, sweep in enumerate(panels):
    x0 = margin_left + idx * (panel_w + panel_gap)
    y0 = margin_top
    all_pts = points_for(sweep, "OLSR-ETX") + points_for(sweep, "P-OLSR")
    if not all_pts:
        continue
    xs = [p[0] for p in all_pts]
    ys = [p[1] for p in all_pts]
    xmin, xmax = min(xs), max(xs)
    ymax = max(ys) * 1.08 if max(ys) > 0 else 1.0
    ymin = 0.0
    svg.append(f'<text x="{x0 + panel_w / 2:.1f}" y="{y0 - 15}" text-anchor="middle" font-family="Arial" font-size="15" font-weight="bold">{sweep}</text>')
    svg.append(f'<line x1="{x0}" y1="{y0 + panel_h}" x2="{x0 + panel_w}" y2="{y0 + panel_h}" stroke="#333"/>')
    svg.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0 + panel_h}" stroke="#333"/>')
    for frac in (0, 0.25, 0.5, 0.75, 1.0):
        y = y0 + panel_h - frac * panel_h
        val = ymin + frac * (ymax - ymin)
        svg.append(f'<line x1="{x0}" y1="{y:.1f}" x2="{x0 + panel_w}" y2="{y:.1f}" stroke="#e5e5e5"/>')
        svg.append(f'<text x="{x0 - 8}" y="{y + 4:.1f}" text-anchor="end" font-family="Arial" font-size="10">{val:.0f}</text>')
    for protocol in ("OLSR-ETX", "P-OLSR"):
        pts = points_for(sweep, protocol)
        coords = []
        for x_val, y_val in pts:
            px = scale(x_val, xmin, xmax, x0, x0 + panel_w)
            py = scale(y_val, ymin, ymax, y0 + panel_h, y0)
            coords.append((px, py, x_val, y_val))
        if len(coords) >= 2:
            path = " ".join(f"{px:.1f},{py:.1f}" for px, py, _, _ in coords)
            svg.append(f'<polyline points="{path}" fill="none" stroke="{colors[protocol]}" stroke-width="2.5"/>')
        for px, py, x_val, y_val in coords:
            svg.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4" fill="{colors[protocol]}"/>')
        lx = x0 + 12
        ly = y0 + 18 + (0 if protocol == "OLSR-ETX" else 18)
        svg.append(f'<line x1="{lx}" y1="{ly}" x2="{lx + 18}" y2="{ly}" stroke="{colors[protocol]}" stroke-width="2.5"/>')
        svg.append(f'<text x="{lx + 24}" y="{ly + 4}" font-family="Arial" font-size="11">{protocol}</text>')
    for x_val in sorted(set(xs)):
        px = scale(x_val, xmin, xmax, x0, x0 + panel_w)
        svg.append(f'<text x="{px:.1f}" y="{y0 + panel_h + 18}" text-anchor="middle" font-family="Arial" font-size="10">{x_val:g}</text>')
    svg.append(f'<text x="{x0 + panel_w / 2:.1f}" y="{y0 + panel_h + 38}" text-anchor="middle" font-family="Arial" font-size="12">{sweep} value</text>')

svg.append('<text x="18" y="215" transform="rotate(-90 18,215)" text-anchor="middle" font-family="Arial" font-size="13">Average outage time (s)</text>')
svg.append("</svg>")

with open(figure_path, "w", encoding="utf-8") as stream:
    stream.write("\n".join(svg))

print("Wrote sweep average:")
print(f"  {sweep_csv}")
print("Wrote figure:")
print(f"  {figure_path}")
PY

echo "Done."
echo "Output directory: ${OUTPUT_DIR}"
echo "Figure directory: ${FIGURE_DIR}"
