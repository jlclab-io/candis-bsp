"""Host checks for the serial API test report generator."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / "tests" / "bsp_api" / "tools" / "api_test_report.py"
INVENTORY = REPO / "tests" / "bsp_api" / "api_inventory.json"


def load_tool():
    spec = importlib.util.spec_from_file_location("api_test_report", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ApiTestReportTest(unittest.TestCase):
    def test_status_precedence_and_not_tested_accounting(self):
        tool = load_tool()
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        first = inventory["components"]["candis"]["functions"][0]["name"]
        second = inventory["components"]["candis"]["functions"][1]["name"]
        log = (
            f"API_CASE|candis|{first}|normal|PASS|idf=6.2|evidence=ok\n"
            f"API_CASE|candis|{first}|hardware|BLOCKED|idf=6.2|evidence=fixture absent\n"
            f"API_CASE|candis|{first}|hardware|FAIL|idf=6.2|evidence=regression\n"
            f"API_CASE|candis|{second}|normal|PASS|idf=6.2|evidence=ok\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runtime.log"
            path.write_text(log, encoding="utf-8")
            records, idf_versions, malformed = tool.parse_log(path)
        rows = tool.build_rows(inventory, records, idf_versions, "runtime.log")
        rows_by_name = {row["api"]: row for row in rows}
        self.assertEqual("fail", rows_by_name[first]["final"])
        self.assertEqual("pass", rows_by_name[second]["final"])
        self.assertEqual(0, malformed)
        totals = tool.summarize_rows(rows)["candis"]
        self.assertEqual(107, totals["total"])
        self.assertEqual(2, totals["designed"])
        self.assertEqual(1, totals["fail"])
        self.assertEqual(105, totals["not_tested"])


if __name__ == "__main__":
    unittest.main()
