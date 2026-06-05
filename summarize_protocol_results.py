#!/usr/bin/env python3
"""Summarize repeated P-OLSR protocol experiments.

The script uses only Python's standard library so it can run inside the ns-3 VM.
It writes protocol-level averages and averaged goodput time series for repeated
OLSR-ETX/P-OLSR runs.
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


SUMMARY_METRICS = (
    "tx_packets",
    "rx_packets",
    "lost_packets",
    "pdr",
    "dlr",
    "goodput_mbps",
    "mean_delay_ms",
    "paper_tx_packets",
    "paper_rx_packets",
    "paper_lost_packets",
    "paper_pdr",
    "paper_dlr",
    "outage_time_s",
    "outage_ratio",
    "delayed_over_max_packets",
)

CONFIG_FIELDS = (
    "hello_interval_s",
    "prediction_alpha",
    "prediction_beta",
    "prediction_gamma",
    "hello_deadline_factor",
    "use_prediction_for_routing",
    "use_prediction_for_mpr",
    "wireless_profile",
    "paper_dlr_max_extra_loss_db",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Average repeated protocol experiment CSV files.")
    parser.add_argument("--summary-csv", required=True, help="Input polsr_protocol_summary.csv path.")
    parser.add_argument("--timeseries-csv", required=True, help="Input polsr_goodput_timeseries.csv path.")
    parser.add_argument("--output-dir", default="", help="Output directory. Defaults to summary CSV directory.")
    return parser.parse_args()


def read_rows(path: Path) -> List[Dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def to_float(value: str) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def mean(values: Iterable[float]) -> float:
    numbers = list(values)
    return sum(numbers) / len(numbers) if numbers else 0.0


def sample_std(values: Iterable[float]) -> float:
    numbers = list(values)
    if len(numbers) < 2:
        return 0.0
    avg = mean(numbers)
    variance = sum((value - avg) ** 2 for value in numbers) / (len(numbers) - 1)
    return math.sqrt(variance)


def stable_value(rows: Sequence[Dict[str, str]], field: str) -> str:
    values = sorted({row.get(field, "") for row in rows if row.get(field, "") != ""})
    if len(values) <= 1:
        return values[0] if values else ""
    return "|".join(values)


def write_summary_average(rows: Sequence[Dict[str, str]], output_path: Path) -> None:
    groups: Dict[Tuple[str, str], List[Dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[(row.get("scenario", ""), row.get("protocol", ""))].append(row)

    fieldnames = ["scenario", "protocol", "runs", *CONFIG_FIELDS]
    for metric in SUMMARY_METRICS:
        fieldnames.extend((f"avg_{metric}", f"std_{metric}"))

    with output_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for (scenario, protocol), group_rows in sorted(groups.items()):
            out: Dict[str, str | int | float] = {
                "scenario": scenario,
                "protocol": protocol,
                "runs": len(group_rows),
            }
            for field in CONFIG_FIELDS:
                out[field] = stable_value(group_rows, field)
            for metric in SUMMARY_METRICS:
                values = [
                    value
                    for value in (to_float(row.get(metric, "")) for row in group_rows)
                    if value is not None
                ]
                out[f"avg_{metric}"] = mean(values)
                out[f"std_{metric}"] = sample_std(values)
            writer.writerow(out)


def write_timeseries_average(rows: Sequence[Dict[str, str]], output_path: Path) -> None:
    groups: Dict[Tuple[str, str, Tuple[str, ...], float, float], List[Dict[str, str]]] = defaultdict(
        list
    )
    for row in rows:
        time = to_float(row.get("time_s", ""))
        window = to_float(row.get("window_s", ""))
        if time is None or window is None:
            continue
        config_values = tuple(row.get(field, "") for field in CONFIG_FIELDS)
        groups[(row.get("scenario", ""), row.get("protocol", ""), config_values, window, time)].append(
            row
        )

    fieldnames = [
        "scenario",
        "protocol",
        *CONFIG_FIELDS,
        "window_s",
        "time_s",
        "runs",
        "avg_tx_packets",
        "avg_rx_packets",
        "avg_rx_bytes",
        "avg_goodput_mbps",
        "std_goodput_mbps",
    ]
    with output_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for (scenario, protocol, config_values, window, time), group_rows in sorted(groups.items()):
            goodput_values = [
                value
                for value in (to_float(row.get("goodput_mbps", "")) for row in group_rows)
                if value is not None
            ]
            config_out = dict(zip(CONFIG_FIELDS, config_values))
            writer.writerow(
                {
                    "scenario": scenario,
                    "protocol": protocol,
                    **config_out,
                    "window_s": window,
                    "time_s": time,
                    "runs": len(group_rows),
                    "avg_tx_packets": mean(
                        value
                        for value in (to_float(row.get("tx_packets", "")) for row in group_rows)
                        if value is not None
                    ),
                    "avg_rx_packets": mean(
                        value
                        for value in (to_float(row.get("rx_packets", "")) for row in group_rows)
                        if value is not None
                    ),
                    "avg_rx_bytes": mean(
                        value
                        for value in (to_float(row.get("rx_bytes", "")) for row in group_rows)
                        if value is not None
                    ),
                    "avg_goodput_mbps": mean(goodput_values),
                    "std_goodput_mbps": sample_std(goodput_values),
                }
            )


def main() -> int:
    args = parse_args()
    summary_csv = Path(args.summary_csv).resolve()
    timeseries_csv = Path(args.timeseries_csv).resolve()
    output_dir = Path(args.output_dir).resolve() if args.output_dir else summary_csv.parent
    output_dir.mkdir(parents=True, exist_ok=True)

    summary_rows = read_rows(summary_csv)
    timeseries_rows = read_rows(timeseries_csv)
    if not summary_rows:
        print(f"No summary rows found: {summary_csv}")
        return 1
    if not timeseries_rows:
        print(f"No time-series rows found: {timeseries_csv}")
        return 1

    summary_out = output_dir / "polsr_protocol_average.csv"
    timeseries_out = output_dir / "polsr_goodput_timeseries_average.csv"
    write_summary_average(summary_rows, summary_out)
    write_timeseries_average(timeseries_rows, timeseries_out)

    print("Wrote averages:")
    print(f"  {summary_out}")
    print(f"  {timeseries_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
