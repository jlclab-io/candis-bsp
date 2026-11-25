#!/usr/bin/env python3
"""Turn serial API_CASE records into a per-API acceptance matrix."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
DEFAULT_INVENTORY = REPO / "tests" / "bsp_api" / "api_inventory.json"
ASPECTS = ("normal", "boundary", "error", "lifecycle", "hardware")
STATUSES = ("not_run", "na", "pass", "blocked", "fail")
STATUS_LABELS = {
    "not_run": "NOT TESTED",
    "na": "N/A",
    "pass": "PASS",
    "blocked": "BLOCKED",
    "fail": "FAIL",
}
PRECEDENCE = {"not_run": 0, "na": 1, "pass": 2, "blocked": 3, "fail": 4}
ANSI_RE = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")
CASE_RE = re.compile(
    r"API_CASE\|(?P<component>[^|]+)\|(?P<api>[^|]+)\|(?P<aspect>[^|]+)\|"
    r"(?P<status>[^|]+)\|(?P<meta>.*)$"
)


def normalize_status(value: str) -> str:
    value = value.strip().lower().replace(" ", "_")
    return value if value in STATUSES else "not_run"


def better(left: str, right: str) -> str:
    return right if PRECEDENCE[right] > PRECEDENCE[left] else left


def parse_log(path: Path) -> tuple[dict[tuple[str, str, str], str], list[str], int]:
    records: dict[tuple[str, str, str], str] = {}
    idf_versions: list[str] = []
    malformed = 0
    raw = path.read_bytes()
    text = ANSI_RE.sub(b"", raw).decode("utf-8", "replace")
    for line in text.splitlines():
        match = CASE_RE.search(line)
        if not match:
            continue
        fields = match.groupdict()
        aspect = fields["aspect"].strip().lower()
        if aspect not in ASPECTS:
            malformed += 1
            continue
        status = normalize_status(fields["status"])
        key = (fields["component"].strip(), fields["api"].strip(), aspect)
        records[key] = better(records.get(key, "not_run"), status)
        idf_match = re.search(r"idf=([^|]+)", fields["meta"])
        if idf_match:
            idf = idf_match.group(1).strip()
            if idf not in idf_versions:
                idf_versions.append(idf)
    if not records:
        raise ValueError("no API_CASE records found in log")
    return records, idf_versions, malformed


def final_status(aspect_statuses: dict[str, str]) -> str:
    status = "not_run"
    for value in aspect_statuses.values():
        status = better(status, value)
    return status


def summarize_rows(rows: list[dict]) -> dict[str, dict]:
    summaries: dict[str, dict] = {}
    for component in sorted({row["component"] for row in rows}):
        component_rows = [row for row in rows if row["component"] == component]
        summary = {
            "total": len(component_rows),
            "designed": sum(row["designed"] for row in component_rows),
            "executed": sum(row["executed"] for row in component_rows),
            "hardware_verified": sum(row["hardware_verified"] for row in component_rows),
            "pass": sum(row["final"] == "pass" for row in component_rows),
            "fail": sum(row["final"] == "fail" for row in component_rows),
            "blocked": sum(row["final"] == "blocked" for row in component_rows),
            "not_tested": sum(row["final"] == "not_run" for row in component_rows),
            "na": sum(row["final"] == "na" for row in component_rows),
        }
        executed = summary["executed"]
        summary["test_coverage"] = summary["designed"] / summary["total"] if summary["total"] else 0.0
        summary["hardware_coverage"] = (
            summary["hardware_verified"] / summary["total"] if summary["total"] else 0.0
        )
        summary["pass_rate"] = summary["pass"] / executed if executed else 0.0
        summaries[component] = summary
    return summaries


def build_rows(inventory: dict, records: dict[tuple[str, str, str], str], idf_versions: list[str],
               evidence: str) -> list[dict]:
    rows: list[dict] = []
    idf_text = ",".join(idf_versions) if idf_versions else "unknown"
    for component, entry in inventory["components"].items():
        for function in entry["functions"]:
            api = function["name"]
            aspects = {aspect: records.get((component, api, aspect), "not_run") for aspect in ASPECTS}
            case_count = sum(value != "not_run" for value in aspects.values())
            designed = case_count > 0
            executed = any(value in {"pass", "fail", "blocked"} for value in aspects.values())
            final = final_status(aspects)
            references = function.get("test_references", [])
            reference_text = ", ".join(references[:3]) if references else "none"
            rows.append(
                {
                    "component": component,
                    "api": api,
                    **aspects,
                    "idf": idf_text,
                    "final": final,
                    "designed": designed,
                    "executed": executed,
                    "hardware_verified": aspects["hardware"] == "pass",
                    "evidence": f"{evidence}: {case_count} aspect record(s); tests={reference_text}",
                }
            )
    return rows


def render_markdown(rows: list[dict], summaries: dict[str, dict], inventory: dict,
                    source: Path) -> str:
    lines = [
        "# Candis BSP Public API Test Matrix",
        "",
        f"Runtime evidence: `{source}`",
        "",
        "## Component summary",
        "",
        "| Component | Functions | Designed | Executed | HW verified | PASS | FAIL | BLOCKED | NOT TESTED | N/A | Test coverage | HW coverage | Pass rate |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for component, summary in summaries.items():
        lines.append(
            f"| {component} | {summary['total']} | {summary['designed']} | {summary['executed']} | "
            f"{summary['hardware_verified']} | {summary['pass']} | {summary['fail']} | "
            f"{summary['blocked']} | {summary['not_tested']} | {summary['na']} | "
            f"{summary['test_coverage']:.1%} | {summary['hardware_coverage']:.1%} | "
            f"{summary['pass_rate']:.1%} |"
        )
    lines.extend(
        [
            "",
            "## Per-API matrix",
            "",
            "| Component | API | Normal | Boundary | Error | Lifecycle | Hardware | IDF | Final | Evidence |",
            "|---|---|---|---|---|---|---|---|---|---|",
        ]
    )
    lines.extend(
        [
            "",
            "## Non-runtime public interfaces",
            "",
            "| Component | Macros | Macro refs | Types | Type refs |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    for component, entry in inventory["components"].items():
        macros = entry["macros"]
        types = entry["types"]
        macro_refs = sum(bool(item.get("test_references")) for item in macros)
        type_refs = sum(bool(item.get("test_references")) for item in types)
        lines.append(f"| {component} | {len(macros)} | {macro_refs} | {len(types)} | {type_refs} |")
    for row in rows:
        lines.append(
            f"| {row['component']} | `{row['api']}` | {STATUS_LABELS[row['normal']]} | "
            f"{STATUS_LABELS[row['boundary']]} | {STATUS_LABELS[row['error']]} | "
            f"{STATUS_LABELS[row['lifecycle']]} | {STATUS_LABELS[row['hardware']]} | "
            f"{row['idf']} | **{STATUS_LABELS[row['final']]}** | {row['evidence']} |"
        )
    return "\n".join(lines) + "\n"


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=(
                "component", "api", "normal", "boundary", "error", "lifecycle",
                "hardware", "idf", "final", "evidence",
            ),
        )
        writer.writeheader()
        for row in rows:
            writer.writerow({name: row[name] for name in writer.fieldnames})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--strict", action="store_true", help="also fail on BLOCKED or missing hardware evidence")
    args = parser.parse_args()

    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    opportunities: dict[tuple[str, str, str], str] = defaultdict(str)
    try:
        records, idf_versions, malformed = parse_log(args.log)
    except ValueError as error:
        print(f"API_REPORT_ERROR {error}", file=sys.stderr)
        return 1
    rows = build_rows(inventory, records, idf_versions, args.log.name)
    summaries = summarize_rows(rows)
    markdown = render_markdown(rows, summaries, inventory, args.log)
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown, encoding="utf-8")
    else:
        print(markdown)
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        write_csv(args.csv, rows)

    totals = {
        key: sum(summary[key] for summary in summaries.values())
        for key in ("total", "designed", "executed", "hardware_verified", "pass", "fail", "blocked", "not_tested", "na")
    }
    print("API_REPORT_SUMMARY " + " ".join(f"{key}={value}" for key, value in totals.items()) +
          f" malformed={malformed}", file=sys.stderr)
    if totals["fail"] or totals["not_tested"]:
        return 1
    if args.strict and (totals["blocked"] or totals["hardware_verified"] < totals["total"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
