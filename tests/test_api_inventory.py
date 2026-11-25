"""Host checks for the Candis public API inventory."""

import importlib.util
import json
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "tests" / "bsp_api" / "tools" / "api_inventory.py"
MANIFEST = REPO / "tests" / "bsp_api" / "api_inventory.json"


def load_tool():
    spec = importlib.util.spec_from_file_location("api_inventory", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ApiInventoryTest(unittest.TestCase):
    def test_public_function_link_manifest_is_complete(self):
        inventory = load_tool().build_inventory()
        self.assertEqual([], load_tool().validate(inventory))
        self.assertEqual(250, inventory["totals"]["functions"])

    def test_checked_in_manifest_matches_public_headers(self):
        current = load_tool().build_inventory()
        checked_in = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(current, checked_in)


if __name__ == "__main__":
    unittest.main()
