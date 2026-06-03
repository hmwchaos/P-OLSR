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
    ("goodput", ("avg_goodput_mbps", "goodput_mbps"), "Goodput (Mbit/s)"),
    ("pdr", ("avg_pdr", "pdr"), "Packet delivery ratio"),
    ("dlr", ("avg_dlr", "dlr", "loss_ratio"), "Loss ratio"),
    ("outage_time", ("avg_outage_time_s", "outage_time_s"), "Outage time (s)"),
    ("mean_delay", ("avg_mean_delay_ms", "mean_delay_ms"), "Mean end-to-end delay (ms)"),
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
        "--timeseries-csv",
        default="",
        help="Goodput time-series CSV path. Defaults to protocol-csv directory/polsr_goodput_timeseries.csv.",
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


def render_goodput_timeseries_svg(rows: Sequence[Dict[str, str]]) -> str:
    width = 1100
    height = 690
    margin_left = 94
    margin_right = 46
    margin_top = 64
    margin_bottom = 118
    plot_width = width - margin_left - margin_right
    plot_height = height - margin_top - margin_bottom

    value_column = find_column(rows, ("avg_goodput_mbps", "goodput_mbps"))
    if value_column is None:
        return ""

    buckets: Dict[Tuple[str, float], List[float]] = defaultdict(list)
    scenario = ""
    for row in rows:
        protocol = row.get("protocol", "unknown")
        time = to_float(row.get("time_s", ""))
        goodput = to_float(row.get(value_column, ""))
        if time is None or goodput is None:
            continue
        if not scenario:
            scenario = row.get("scenario", "")
        buckets[(protocol, time)].append(goodput)

    series: Dict[str, List[Tuple[float, float]]] = defaultdict(list)
    for (protocol, time), values in buckets.items():
        series[protocol].append((time, sum(values) / len(values)))
    for points in series.values():
        points.sort()

    all_times = [time for points in series.values() for time, _ in points]
    all_values = [value for points in series.values() for _, value in points]
    x_min = min(all_times) if all_times else 0.0
    x_max = max(all_times) if all_times else 1.0
    if x_max <= x_min:
        x_max = x_min + 1.0
    y_max = max(max(all_values) * 1.18, 1.0) if all_values else 1.0

    def sx(time: float) -> float:
        return margin_left + (time - x_min) / (x_max - x_min) * plot_width

    def sy(value: float) -> float:
        return margin_top + plot_height - (value / y_max) * plot_height

    protocol_styles = {
        "OLSR": ("#c62828", "asterisk"),
        "OLSR-ETX": ("#1565c0", "square"),
        "P-OLSR": ("#008a1c", "circle"),
    }
    preferred_order = ("OLSR", "OLSR-ETX", "P-OLSR")
    protocols_to_plot = [protocol for protocol in preferred_order if protocol in series]
    protocols_to_plot.extend(protocol for protocol in series if protocol not in protocols_to_plot)

    title = f"Goodput during an emulation"
    if scenario:
        title += f" ({scenario})"
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        svg_text(width / 2, 34, title, size=22),
    ]

    for index in range(7):
        value = y_max * index / 6
        y = sy(value)
        parts.append(
            f'<line x1="{margin_left}" y1="{y:.1f}" x2="{width - margin_right}" y2="{y:.1f}" '
            'stroke="#c8c8c8" stroke-width="1"/>'
        )
        parts.append(svg_text(margin_left - 12, y + 4, nice_number(value), size=12, anchor="end"))

    for index in range(9):
        time = x_min + (x_max - x_min) * index / 8
        x = sx(time)
        parts.append(
            f'<line x1="{x:.1f}" y1="{margin_top}" x2="{x:.1f}" '
            f'y2="{margin_top + plot_height}" stroke="#c8c8c8" stroke-width="1"/>'
        )
        parts.append(svg_text(x, margin_top + plot_height + 28, nice_number(time), size=12))

    parts.append(
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" '
        f'y2="{margin_top + plot_height}" stroke="#111" stroke-width="1.4"/>'
    )
    parts.append(
        f'<line x1="{margin_left}" y1="{margin_top + plot_height}" '
        f'x2="{width - margin_right}" y2="{margin_top + plot_height}" '
        'stroke="#111" stroke-width="1.4"/>'
    )
    parts.append(svg_text(width / 2, height - 42, "time [s]", size=18))
    parts.append(
        f'<text x="28" y="{margin_top + plot_height / 2:.1f}" font-family="Arial, sans-serif" '
        'font-size="18" text-anchor="middle" fill="#222" '
        'transform="rotate(-90 28 '
        f'{margin_top + plot_height / 2:.1f})">Goodput [Mbits/s]</text>'
    )

    legend_x = width - 382
    legend_y = height - 332
    legend_w = 302
    legend_h = 262
    parts.append(
        f'<rect x="{legend_x}" y="{legend_y}" width="{legend_w}" height="{legend_h}" '
        'fill="#ffffff" stroke="#222" stroke-width="1"/>'
    )
    parts.append(svg_text(legend_x + legend_w / 2, legend_y + 30, "Goodput", size=18))

    averages: Dict[str, float] = {}
    for protocol in protocols_to_plot:
        points = series.get(protocol, [])
        if not points:
            continue
        color, marker = protocol_styles.get(protocol, ("#333", "circle"))
        point_text = " ".join(f"{sx(time):.1f},{sy(value):.1f}" for time, value in points)
        parts.append(
            f'<polyline points="{point_text}" fill="none" stroke="{color}" stroke-width="1.2"/>'
        )
        for time, value in points[:: max(1, len(points) // 140)]:
            x = sx(time)
            y = sy(value)
            if marker == "asterisk":
                parts.append(svg_text(x, y + 4, "*", size=17))
            elif marker == "square":
                parts.append(
                    f'<rect x="{x - 4.0:.1f}" y="{y - 4.0:.1f}" width="8" height="8" '
                    f'fill="none" stroke="{color}" stroke-width="1.1"/>'
                )
            else:
                parts.append(
                    f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.4" fill="none" '
                    f'stroke="{color}" stroke-width="1.1"/>'
                )
        averages[protocol] = sum(value for _, value in points) / len(points)

    for protocol, average in averages.items():
        color, _ = protocol_styles.get(protocol, ("#333", "circle"))
        y = sy(average)
        dash = "2 3" if protocol == "OLSR" else "7 5"
        parts.append(
            f'<line x1="{margin_left}" y1="{y:.1f}" x2="{width - margin_right}" y2="{y:.1f}" '
            f'stroke="{color}" stroke-width="1.1" stroke-dasharray="{dash}"/>'
        )

    legend_rows = []
    for protocol in protocols_to_plot:
        color, _ = protocol_styles.get(protocol, ("#333", "circle"))
        legend_rows.append((protocol, color, "solid"))
    for protocol in protocols_to_plot:
        color, _ = protocol_styles.get(protocol, ("#333", "circle"))
        legend_rows.append((f"Average Goodput {protocol}", color, "dash"))
    for index, (label, color, style) in enumerate(legend_rows):
        y = legend_y + 58 + index * 30
        parts.append(
            f'<line x1="{legend_x + 24}" y1="{y}" x2="{legend_x + 76}" y2="{y}" '
            f'stroke="{color}" stroke-width="1.2"'
            + (' stroke-dasharray="5 4"' if style == "dash" else "")
            + "/>"
        )
        if style == "solid":
            marker = protocol_styles.get(label, ("#333", "circle"))[1]
            if marker == "asterisk":
                parts.append(svg_text(legend_x + 50, y + 5, "*", size=17))
            elif marker == "square":
                parts.append(
                    f'<rect x="{legend_x + 46}" y="{y - 4}" width="8" height="8" '
                    f'fill="none" stroke="{color}" stroke-width="1.1"/>'
                )
            else:
                parts.append(
                    f'<circle cx="{legend_x + 50}" cy="{y}" r="4.4" fill="none" '
                    f'stroke="{color}" stroke-width="1.1"/>'
                )
        parts.append(svg_text(legend_x + 96, y + 5, label, size=15, anchor="start"))

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


def generate_goodput_timeseries_figure(csv_path: Path, output_dir: Path) -> List[Path]:
    rows = read_rows(csv_path)
    if not rows:
        return []
    output_dir.mkdir(parents=True, exist_ok=True)
    svg = render_goodput_timeseries_svg(rows)
    path = output_dir / "protocol_goodput_timeseries.svg"
    path.write_text(svg, encoding="utf-8", newline="\n")
    return [path]


def main() -> int:
    args = parse_args()
    base_dir = Path(args.base_dir).resolve()
    figure_dir = Path(args.figure_dir).resolve() if args.figure_dir else base_dir / "figures"
    protocol_csv = (
        Path(args.protocol_csv).resolve()
        if args.protocol_csv
        else base_dir / "protocol-results" / "polsr_protocol_summary.csv"
    )
    timeseries_csv = (
        Path(args.timeseries_csv).resolve()
        if args.timeseries_csv
        else protocol_csv.parent / "polsr_goodput_timeseries.csv"
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
    outputs.extend(generate_goodput_timeseries_figure(timeseries_csv, figure_dir))

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
