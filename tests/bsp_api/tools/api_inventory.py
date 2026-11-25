#!/usr/bin/env python3
"""Build and validate the Candis public-API inventory.

The inventory is derived from the public include directories so a new header
cannot silently disappear from the device test manifest. Function linkage is
checked separately against main/api_symbols.c.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
TEST_ROOT = REPO / "tests" / "bsp_api"
SYMBOL_FILE = TEST_ROOT / "main" / "api_symbols.c"
DEFAULT_OUTPUT = TEST_ROOT / "api_inventory.json"
TEST_MAIN = TEST_ROOT / "main"

COMPONENTS = {
    "candis": REPO / "components" / "candis" / "include",
    "candis-camera": REPO / "components" / "candis-camera" / "include",
    "candis-dock": REPO / "components" / "candis-dock" / "include",
    "tg28-esp": REPO / "components" / "tg28-esp" / "include",
}

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
EXTERN_C_OPEN_RE = re.compile(r'extern\s+"C"\s*\{')
FUNCTION_RE = re.compile(
    r"(?:^|\n)\s*(?:extern\s+)?([A-Za-z_][\w\s*]*?)"
    r"\b([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*;",
    re.M,
)
MACRO_RE = re.compile(r"^\s*#define\s+([A-Za-z_]\w*)(?:\s*\([^)]*\))?", re.M)
ALIAS_RE = re.compile(
    r"^\s*typedef\s+(?!struct\b|enum\b|union\b)([^;]+?)\s+"
    r"([A-Za-z_]\w*)\s*;",
    re.M,
)


def strip_comments(text: str) -> str:
    """Remove comments while preserving newlines for stable line numbers."""
    return COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)


def normalize(text: str) -> str:
    return " ".join(text.split())


def line_at(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def doc_comment_before(lines: list[str], line_number: int) -> str:
    index = line_number - 2
    while index >= 0 and not lines[index].strip():
        index -= 1
    if index < 0 or "*/" not in lines[index]:
        return ""
    end = index
    while index >= 0 and "/**" not in lines[index]:
        index -= 1
    if index < 0:
        return ""
    comment = "\n".join(lines[index : end + 1])
    comment = re.sub(r"^/?\*\*?|\*?/$", "", comment)
    comment = comment.replace("/**", "").replace("*/", "")
    comment = re.sub(r"(?m)^\s*\*\s?", "", comment)
    return normalize(comment)


def test_references() -> dict[str, list[str]]:
    references: dict[str, list[str]] = {}
    for path in sorted(TEST_MAIN.glob("*.c")):
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for name in re.findall(r'"([A-Za-z_]\w*)"', line):
                references.setdefault(name, []).append(
                    f"{path.relative_to(REPO)}:{line_number}"
                )
            for name in re.findall(r"\b([A-Za-z_]\w*)\b", line):
                references.setdefault(name, []).append(
                    f"{path.relative_to(REPO)}:{line_number}"
                )
    return references


def match_braces(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unbalanced aggregate declaration")


def aggregate_typedefs(text: str) -> list[tuple[str, str, str, int]]:
    found: list[tuple[str, str, str, int]] = []
    pattern = re.compile(r"typedef\s+(struct|enum|union)\b")
    for match in pattern.finditer(text):
        opening = text.find("{", match.end())
        if opening < 0:
            continue
        try:
            closing = match_braces(text, opening)
        except ValueError:
            continue
        tail = text[closing + 1 : text.find(";", closing)]
        for name in re.findall(r"[A-Za-z_]\w*", tail):
            found.append((name, match.group(1), normalize(text[match.start() : closing + 1]), match.start()))
    return found


def parse_header(path: Path, component: str) -> tuple[list[dict], list[dict], list[dict]]:
    raw = path.read_text(encoding="utf-8")
    text = strip_comments(raw)
    raw_lines = raw.splitlines()
    # extern "C" is a linkage wrapper, not a C type scope.
    text_for_scan = EXTERN_C_OPEN_RE.sub(lambda match: " " * len(match.group(0)), text)

    functions: list[dict] = []
    for match in FUNCTION_RE.finditer(text_for_scan):
        name = match.group(2)
        if name in {"if", "for", "while", "switch", "void"}:
            continue
        functions.append(
            {
                "component": component,
                "name": name,
                "kind": "function",
                "header": str(path.relative_to(REPO)),
                "line": line_at(text, match.start(2)),
                "return_type": normalize(match.group(1)),
                "parameters": normalize(match.group(3)),
                "doc": doc_comment_before(raw_lines, line_at(text, match.start(2))),
            }
        )

    macros: list[dict] = []
    for match in MACRO_RE.finditer(raw):
        macros.append(
            {
                "component": component,
                "name": match.group(1),
                "kind": "macro",
                "header": str(path.relative_to(REPO)),
                "line": line_at(raw, match.start(1)),
                "doc": doc_comment_before(raw_lines, line_at(raw, match.start(1))),
            }
        )

    types: list[dict] = []
    for match in ALIAS_RE.finditer(text):
        types.append(
            {
                "component": component,
                "name": match.group(2),
                "kind": "type",
                "header": str(path.relative_to(REPO)),
                "line": line_at(text, match.start(2)),
                "declaration": normalize(match.group(0)),
                "doc": doc_comment_before(raw_lines, line_at(text, match.start(2))),
            }
        )
    for name, aggregate, declaration, offset in aggregate_typedefs(text):
        types.append(
            {
                "component": component,
                "name": name,
                "kind": aggregate,
                "header": str(path.relative_to(REPO)),
                "line": line_at(text, offset),
                "declaration": declaration,
                "doc": doc_comment_before(raw_lines, line_at(text, offset)),
            }
        )
    return functions, macros, types


def symbol_names() -> list[str]:
    text = SYMBOL_FILE.read_text(encoding="utf-8")
    names = re.findall(r"API_SYMBOL\(([A-Za-z_]\w*)\)", text)
    # Ignore the text in the API_SYMBOL macro definition.
    return sorted(set(name for name in names if name != "function"))


def build_inventory() -> dict:
    references = test_references()
    components: dict[str, dict] = {}
    all_functions: list[dict] = []
    all_macros: list[dict] = []
    all_types: list[dict] = []
    for component, include_root in COMPONENTS.items():
        headers = sorted(include_root.rglob("*.h"))
        entry = {"headers": [str(path.relative_to(REPO)) for path in headers]}
        functions: list[dict] = []
        macros: list[dict] = []
        types: list[dict] = []
        for header in headers:
            parsed_functions, parsed_macros, parsed_types = parse_header(header, component)
            functions.extend(parsed_functions)
            macros.extend(parsed_macros)
            types.extend(parsed_types)
        entry["functions"] = sorted(functions, key=lambda item: (item["header"], item["line"]))
        entry["macros"] = sorted(macros, key=lambda item: (item["header"], item["line"]))
        entry["types"] = sorted(types, key=lambda item: (item["header"], item["line"]))
        for category in ("functions", "macros", "types"):
            for item in entry[category]:
                item["test_references"] = references.get(item["name"], [])
        entry["totals"] = {
            "functions": len(entry["functions"]),
            "macros": len(entry["macros"]),
            "types": len(entry["types"]),
        }
        components[component] = entry
        all_functions.extend(entry["functions"])
        all_macros.extend(entry["macros"])
        all_types.extend(entry["types"])

    return {
        "schema": 1,
        "source": "public include directories",
        "components": components,
        "totals": {
            "functions": len(all_functions),
            "macros": len(all_macros),
            "types": len(all_types),
        },
        "function_manifest": sorted(item["name"] for item in all_functions),
    }


def validate(inventory: dict) -> list[str]:
    errors: list[str] = []
    declared = set(inventory["function_manifest"])
    if len(declared) != len(inventory["function_manifest"]):
        errors.append("duplicate public function names make the flat symbol manifest ambiguous")
    manifest = set(symbol_names())
    missing = sorted(declared - manifest)
    extra = sorted(manifest - declared)
    if missing:
        errors.append("missing from main/api_symbols.c: " + ", ".join(missing))
    if extra:
        errors.append("not declared in public headers: " + ", ".join(extra))
    for component, entry in inventory["components"].items():
        if not entry["functions"]:
            errors.append(f"{component}: no public functions found")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="compare against api_symbols.c")
    args = parser.parse_args()

    inventory = build_inventory()
    errors = validate(inventory)
    if args.check:
        if errors:
            for error in errors:
                print(f"API_INVENTORY_ERROR {error}", file=sys.stderr)
            return 1
        print(
            "API_INVENTORY_OK "
            f"functions={inventory['totals']['functions']} "
            f"macros={inventory['totals']['macros']} "
            f"types={inventory['totals']['types']}"
        )
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(inventory, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(
        f"API_INVENTORY_WRITTEN {args.output} "
        f"functions={inventory['totals']['functions']} "
        f"macros={inventory['totals']['macros']} "
        f"types={inventory['totals']['types']}"
    )
    if errors:
        for error in errors:
            print(f"API_INVENTORY_ERROR {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
