#!/usr/bin/env python3
"""Generate SVG figures from P-OLSR reproduction CSV files.

The script intentionally uses only Python's standard library so it can run in
the ns-3 VM without installing matplotlib or other dependencies.
"""

from __future__ import annotations

import argparse
import csv
import html
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


MetricSpec = Tuple[str, Sequence[str], str]


MODEL_METRICS: Sequence[MetricSpec] = (
    ("goodput", ("avg_goodput_mbps",), "Average goodput (Mbit/s)"),
    ("pdr", ("avg_pdr",), "Packet delivery ratio"),
    ("dlr", ("avg_dlr",), "Datagram loss rate"),
    ("outage_time", ("avg_outage_time_s",), "Outage time (s)"),
    ("route_changes", ("avg_route_changes",), "Route changes"),
    ("hello_overhead", ("estimated_hello_bytes",), "Estimated Hello overhead (bytes)"),
)


PROTOCOL_METRICS: Sequence[MetricSpec] = (
    ("goodput", ("goodput_mbps",), "Goodput (Mbit/s)"),
    ("pdr", ("pdr",), "Packet delivery ratio"),
    ("dlr", ("dlr", "loss_ratio"), "Loss ratio"),
    ("mean_delay", ("mean_delay_ms",), "Mean end-to-end delay (ms)"),
)


COLORS = (
    "#2457a6",
    "#c7532c",
    "#2c8b57",
    "#8d5cb8",
    "#d59f26",
    "#4b7f85",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate P-OLSR result SVG figures.")
    parser.add_argument(
        "--base-dir",
        default=str(Path(__file__).resolve().parent),
        help="P-OLSR directory. Defaults to this script's directory.",
    )
    parser.add_argument(
        "--protocol-csv",
        default="",
        help="Protocol-level CSV path. Defaults to base-dir/protocol-results/polsr_protocol_summary.csv.",
    )
    parser.add_argument(
        "--figure-dir",
        default="",
        help="Figure output directory. Defaults to base-dir/figures.",
    )
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


def find_column(rows: Sequence[Dict[str, str]], candidates: Sequence[str]) -> str | None:
    if not rows:
        return None
    available = set(rows[0].keys())
    for column in candidates:
        if column in available:
            return column
    return None


def aggregate(
    rows: Sequence[Dict[str, str]],
    value_column: str,
) -> Tuple[List[str], List[str], Dict[Tuple[str, str], float]]:
    buckets: Dict[Tuple[str, str], List[float]] = defaultdict(list)
    scenarios = []
    protocols = []

    for row in rows:
        scenario = row.get("scenario", "unknown")
        protocol = row.get("protocol", "unknown")
        value = to_float(row.get(value_column, ""))
        if value is None:
            continue
        if scenario not in scenarios:
            scenarios.append(scenario)
        if protocol not in protocols:
            protocols.append(protocol)
        buckets[(scenario, protocol)].append(value)

    values = {
        key: sum(numbers) / len(numbers)
        for key, numbers in buckets.items()
        if numbers
    }
    return scenarios, protocols, values


def nice_number(value: float) -> str:
    if abs(value) >= 1000:
        return f"{value:.0f}"
    if abs(value) >= 10:
        return f"{value:.1f}"
    return f"{value:.3f}".rstrip("0").rstrip(".")


def svg_text(x: float, y: float, text: str, size: int = 12, anchor: str = "middle") -> str:
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="Arial, sans-serif" '
        f'font-size="{size}" text-anchor="{anchor}" fill="#222">{html.escape(text)}</text>'
    )


def render_grouped_bar_svg(
    title: str,
    scenarios: Sequence[str],
    protocols: Sequence[str],
    values: Dict[Tuple[str, str], float],
) -> str:
    width = 980
    height = 560
    margin_left = 92
    margin_right = 40
    margin_top = 64
    margin_bottom = 110
    plot_width = width - margin_left - margin_right
    plot_height = height - margin_top - margin_bottom

    max_value = max(values.values()) if values else 1.0
    max_value = max(max_value, 1.0e-9)
    y_max = max_value * 1.18

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        svg_text(width / 2, 34, title, size=21),
    ]

    # Grid and y-axis labels.
    for index in range(6):
        value = y_max * index / 5
        y = margin_top + plot_height - (value / y_max) * plot_height
        parts.append(
            f'<line x1="{margin_left}" y1="{y:.1f}" x2="{width - margin_right}" y2="{y:.1f}" '
            'stroke="#d7dce2" stroke-width="1"/>'
        )
        parts.append(svg_text(margin_left - 10, y + 4, nice_number(value), size=11, anchor="end"))

    parts.append(
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" '
        f'y2="{margin_top + plot_height}" stroke="#30343b" stroke-width="1.2"/>'
    )
    parts.append(
        f'<line x1="{margin_left}" y1="{margin_top + plot_height}" '
        f'x2="{width - margin_right}" y2="{margin_top + plot_height}" '
        'stroke="#30343b" stroke-width="1.2"/>'
    )

    group_width = plot_width / max(len(scenarios), 1)
    bar_group_width = group_width * 0.72
    bar_width = min(70.0, bar_group_width / max(len(protocols), 1))

    for scenario_index, scenario in enumerate(scenarios):
        group_x = margin_left + group_width * scenario_index + group_width / 2
        start_x = group_x - (bar_width * len(protocols)) / 2
        for protocol_index, protocol in enumerate(protocols):
            value = values.get((scenario, protocol), 0.0)
            bar_height = (value / y_max) * plot_height
            x = start_x + protocol_index * bar_width
            y = margin_top + plot_height - bar_height
            color = COLORS[protocol_index % len(COLORS)]
            parts.append(
                f'<rect x="{x + 3:.1f}" y="{y:.1f}" width="{max(bar_width - 6, 1):.1f}" '
                f'height="{bar_height:.1f}" fill="{color}" rx="2"/>'
            )
            parts.append(svg_text(x + bar_width / 2, max(y - 5, margin_top + 12), nice_number(value), size=10))
        parts.append(svg_text(group_x, margin_top + plot_height + 28, scenario, size=13))

    legend_x = margin_left
    legend_y = height - 54
    for index, protocol in enumerate(protocols):
        x = legend_x + index * 230
        color = COLORS[index % len(COLORS)]
        parts.append(f'<rect x="{x}" y="{legend_y}" width="16" height="16" fill="{color}" rx="2"/>')
        parts.append(svg_text(x + 22, legend_y + 13, protocol, size=12, anchor="start"))

    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def generate_figures(
    csv_path: Path,
    output_dir: Path,
    prefix: str,
    metrics: Sequence[MetricSpec],
) -> List[Path]:
    rows = read_rows(csv_path)
    if not rows:
        return []

    output_dir.mkdir(parents=True, exist_ok=True)
    written: List[Path] = []
    for metric_name, columns, title in metrics:
        column = find_column(rows, columns)
        if column is None:
            continue
        scenarios, protocols, values = aggregate(rows, column)
        if not values:
            continue
        svg = render_grouped_bar_svg(title, scenarios, protocols, values)
        path = output_dir / f"{prefix}_{metric_name}.svg"
        path.write_text(svg, encoding="utf-8", newline="\n")
        written.append(path)
    return written


def main() -> int:
    args = parse_args()
    base_dir = Path(args.base_dir).resolve()
    figure_dir = Path(args.figure_dir).resolve() if args.figure_dir else base_dir / "figures"
    protocol_csv = (
        Path(args.protocol_csv).resolve()
        if args.protocol_csv
        else base_dir / "protocol-results" / "polsr_protocol_summary.csv"
    )

    outputs: List[Path] = []
    outputs.extend(
        generate_figures(
            base_dir / "results" / "polsr_reproduction_summary.csv",
            figure_dir,
            "model",
            MODEL_METRICS,
        )
    )
    outputs.extend(
        generate_figures(
            protocol_csv,
            figure_dir,
            "protocol",
            PROTOCOL_METRICS,
        )
    )

    if not outputs:
        print("No CSV input was found. Run the reproduction experiments first.")
        return 1

    print("Generated SVG figures:")
    for path in outputs:
        try:
            display_path = path.relative_to(base_dir)
        except ValueError:
            display_path = path
        print(f"  {display_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
