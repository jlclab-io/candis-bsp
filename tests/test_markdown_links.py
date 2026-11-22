# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.check_markdown_links import check_markdown_links


class MarkdownLinksTest(unittest.TestCase):
    def test_relative_targets_and_heading_anchors(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            docs = root / "docs"
            docs.mkdir()
            (docs / "guide.md").write_text(
                "# First Heading\n\n<a id=\"custom\"></a>\n\n## First Heading\n"
            )
            (root / "README.md").write_text(
                "# Setup\n\n# Setup\n\n"
                "[guide](docs/guide.md#first-heading) "
                "[duplicate](#setup-1) [html](docs/guide.md#custom)\n\n"
                "[external](https://example.com/path#external)\n\n"
                "```md\n[ignored](missing.md#nowhere)\n```\n"
            )
            self.assertEqual(check_markdown_links(root), [])

    def test_reports_missing_relative_targets_and_anchors(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text(
                "# Existing\n\n[missing file](missing.md)\n[missing anchor](README.md#absent)\n"
            )
            issues = check_markdown_links(root)
            self.assertEqual(len(issues), 2)
            self.assertTrue(any("missing link target" in issue for issue in issues))
            self.assertTrue(any("missing anchor #absent" in issue for issue in issues))


if __name__ == "__main__":
    unittest.main()
