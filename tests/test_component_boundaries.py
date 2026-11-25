# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ROOT / "components"


def direct_dependencies(path: Path) -> set[str]:
    dependencies: set[str] = set()
    in_dependencies = False
    for line in path.read_text().splitlines():
        if line == "dependencies:":
            in_dependencies = True
            continue
        if in_dependencies and line and not line.startswith(" "):
            break
        if in_dependencies and line.startswith("  ") and not line.startswith("    "):
            name = line.strip().split(":", 1)[0]
            if name:
                dependencies.add(name)
    return dependencies


class ComponentBoundaryTest(unittest.TestCase):
    def test_candis_natively_depends_on_tg28(self):
        dependencies = direct_dependencies(COMPONENTS / "candis/idf_component.yml")
        self.assertIn("tg28-esp", dependencies)

    def test_tg28_remains_independent(self):
        dependencies = direct_dependencies(COMPONENTS / "tg28-esp/idf_component.yml")
        self.assertNotIn("candis", dependencies)
        self.assertNotIn("candis-dock", dependencies)
        self.assertNotIn("candis-camera", dependencies)

    def test_example_board_component_is_not_reintroduced(self):
        self.assertFalse(any(COMPONENTS.rglob("candis_example_board")))

    def test_dock_header_includes_its_public_state_type(self):
        header = (COMPONENTS / "candis-dock/include/bsp/candis_dock.h").read_text()
        self.assertIn('#include "bsp/candis_types.h"', header)


if __name__ == "__main__":
    unittest.main()
