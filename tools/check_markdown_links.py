#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check repository-local Markdown links and heading anchors."""

from __future__ import annotations

import argparse
import html
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
import unicodedata
from urllib.parse import unquote, urlsplit


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
FENCE_RE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
ATX_HEADING_RE = re.compile(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$")
SETEXT_HEADING_RE = re.compile(r"^\s{0,3}(=+|-+)\s*$")
INLINE_CODE_RE = re.compile(r"(`+)(.*?)\1")
INLINE_LINK_RE = re.compile(r"!?\[[^\]]*\]\(\s*(<[^>]+>|[^)\s]+)(?:\s+[^)]*)?\)")
REFERENCE_LINK_RE = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*(<[^>]+>|\S+)")
HTML_ID_RE = re.compile(r"\b(?:id|name)\s*=\s*([\"'])(.*?)\1", re.IGNORECASE)


class _AnchorParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.anchors: set[str] = set()

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            if name.lower() in {"id", "name"} and value:
                self.anchors.add(value)

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.handle_starttag(tag, attrs)


def _heading_slug(text: str) -> str:
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = INLINE_CODE_RE.sub(lambda match: match.group(2), text)
    text = re.sub(r"<[^>]*>", "", text)
    text = html.unescape(text).lower()
    text = unicodedata.normalize("NFKC", text)
    characters = [char if char.isalnum() or char in " -" else "" for char in text]
    return re.sub(r"\s+", "-", "".join(characters)).strip("-")


def _document_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    duplicate_counts: dict[str, int] = {}
    lines = text.splitlines()
    visible_lines: list[str] = []
    fence_character = ""
    fence_length = 0

    for line in lines:
        fence = FENCE_RE.match(line)
        if fence:
            marker = fence.group(1)
            if not fence_character:
                fence_character = marker[0]
                fence_length = len(marker)
            elif marker[0] == fence_character and len(marker) >= fence_length:
                fence_character = ""
                fence_length = 0
            continue
        if not fence_character:
            visible_lines.append(line)

    for index, line in enumerate(visible_lines):
        heading = ATX_HEADING_RE.match(line)
        if heading:
            slug = _heading_slug(heading.group(1))
        elif index + 1 < len(visible_lines) and SETEXT_HEADING_RE.match(visible_lines[index + 1]):
            slug = _heading_slug(line.strip())
        else:
            slug = ""
        if slug:
            duplicate = duplicate_counts.get(slug, 0)
            anchors.add(slug if duplicate == 0 else f"{slug}-{duplicate}")
            duplicate_counts[slug] = duplicate + 1
        for match in HTML_ID_RE.finditer(line):
            anchors.add(html.unescape(match.group(2)))

    parser = _AnchorParser()
    parser.feed("\n".join(visible_lines))
    anchors.update(parser.anchors)
    return anchors


def _markdown_files(root: Path) -> list[Path]:
    excluded = {".git", "build", "managed_components", "node_modules", "dist"}
    return sorted(
        path for path in root.rglob("*.md")
        if not excluded.intersection(path.relative_to(root).parts)
    )


def check_markdown_links(root: Path) -> list[str]:
    root = root.resolve()
    documents = _markdown_files(root)
    anchors = {path: _document_anchors(path.read_text(errors="replace")) for path in documents}
    document_set = set(documents)
    issues: list[str] = []

    for source in documents:
        text = source.read_text(errors="replace")
        visible_lines: list[str] = []
        fence_character = ""
        fence_length = 0
        for line in text.splitlines():
            fence = FENCE_RE.match(line)
            if fence:
                marker = fence.group(1)
                if not fence_character:
                    fence_character = marker[0]
                    fence_length = len(marker)
                elif marker[0] == fence_character and len(marker) >= fence_length:
                    fence_character = ""
                    fence_length = 0
                visible_lines.append("")
            elif fence_character:
                visible_lines.append("")
            else:
                visible_lines.append(INLINE_CODE_RE.sub("", line))

        for line_number, line in enumerate(visible_lines, start=1):
            targets = [match.group(1) for match in INLINE_LINK_RE.finditer(line)]
            reference = REFERENCE_LINK_RE.match(line)
            if reference:
                targets.append(reference.group(1))
            for raw_target in targets:
                target = raw_target[1:-1] if raw_target.startswith("<") and raw_target.endswith(">") else raw_target
                parsed = urlsplit(html.unescape(target))
                if parsed.scheme or parsed.netloc:
                    continue
                relative_path = unquote(parsed.path)
                if relative_path.startswith("/"):
                    destination = (root / relative_path.lstrip("/")).resolve()
                elif relative_path:
                    destination = (source.parent / relative_path).resolve()
                else:
                    destination = source
                try:
                    destination.relative_to(root)
                except ValueError:
                    issues.append(f"{source.relative_to(root)}:{line_number}: link leaves repository: {target}")
                    continue
                if destination.is_dir():
                    readme = destination / "README.md"
                    index = destination / "index.md"
                    destination = readme if readme.is_file() else index if index.is_file() else destination
                if not destination.exists():
                    issues.append(f"{source.relative_to(root)}:{line_number}: missing link target: {target}")
                    continue
                fragment = unquote(parsed.fragment)
                if fragment and destination in document_set and fragment not in anchors[destination]:
                    issues.append(f"{source.relative_to(root)}:{line_number}: missing anchor #{fragment} in {destination.relative_to(root)}")

    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPOSITORY_ROOT)
    args = parser.parse_args()
    issues = check_markdown_links(args.root)
    if issues:
        print("\n".join(issues), file=sys.stderr)
        return 1
    print("Markdown relative links and anchors are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
