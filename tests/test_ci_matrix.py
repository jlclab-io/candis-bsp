# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.ci_matrix import (
    ALL_DEFAULT_IDS,
    BUILDS_BY_GROUP,
    CORE_SMOKE_IDS,
    CONFIG_FRAGMENTS,
    builds_for_project,
    groups_for_builds,
    paths_need_docs_check,
    paths_need_host_tests,
    plan_paths,
)


class CiMatrixTest(unittest.TestCase):
    def test_camera_model_builds_use_their_own_dock_profile(self):
        models = [
            "bf20a6", "bf3031", "bf3045", "bf3925", "bf3a03", "gc0308", "gc032a",
            "gc2145", "mt9d111", "ov2640", "ov3640", "ov3660", "ov5640", "sc030iot",
            "sc101iot", "sc2336", "sp0a39",
        ]
        for model in models:
            with self.subTest(model=model):
                self.assertEqual(CONFIG_FRAGMENTS[("camera", model)], (model,))
                sdkconfig = (ROOT / "examples/camera" / f"sdkconfig.{model}").read_text()
                self.assertIn("CONFIG_CANDIS_CAMERA_DOCK_V12=y", sdkconfig)
                self.assertIn(f"CONFIG_CANDIS_CAMERA_PROFILE_{model.upper()}=y", sdkconfig)
                profile = ROOT / "components/candis-camera/src/profiles" / f"candis_camera_profile_{model}.c"
                mode_symbol = profile.read_text().split('.default_kconfig = "', 1)[1].split('"', 1)[0]
                self.assertIn(f"{mode_symbol}=y", sdkconfig)
                self.assertIn(f"camera:{model}", builds_for_project("camera"))

    def test_camera_generic_profile_sources_select_all_models(self):
        self.assertEqual(
            plan_paths(["components/candis-camera/src/candis_camera_profile.c"]),
            builds_for_project("camera"),
        )

    def test_camera_groups_cover_each_build_once(self):
        camera_builds = builds_for_project("camera")
        groups = groups_for_builds(camera_builds)
        self.assertEqual(groups, ["camera", "camera-models-1", "camera-models-2"])
        self.assertEqual([len(BUILDS_BY_GROUP[group]) for group in groups], [6, 9, 8])
        self.assertEqual(len(camera_builds), len(set(camera_builds)))

    def test_camera_optional_sources_select_their_enabled_profiles(self):
        self.assertEqual(
            plan_paths(["examples/camera/main/still_jpeg.c"]),
            ["camera:jpeg", "camera:yuyv"],
        )
        self.assertEqual(
            plan_paths(["examples/camera/main/camera_pixels.h"]),
            builds_for_project("camera"),
        )
        self.assertEqual(
            plan_paths(["examples/camera/main/CMakeLists.txt"]),
            [
                "camera:default",
                "camera:fixture",
                "camera:dvp_all",
                "camera:jpeg",
                "camera:yuyv",
                "camera:uyvy",
                "camera:bf20a6",
                "camera:bf3031",
                "camera:bf3045",
                "camera:bf3925",
                "camera:bf3a03",
                "camera:gc0308",
                "camera:gc032a",
                "camera:gc2145",
                "camera:mt9d111",
                "camera:ov2640",
                "camera:ov3640",
                "camera:ov3660",
                "camera:ov5640",
                "camera:sc030iot",
                "camera:sc101iot",
                "camera:sc2336",
                "camera:sp0a39",
            ],
        )
        self.assertEqual(
            plan_paths(["examples/camera/main/Kconfig.projbuild"]),
            builds_for_project("camera"),
        )

    def test_registry_scripts_select_relevant_builds_and_host_tests(self):
        staging = ["tools/stage_registry_extension.py"]
        self.assertEqual(
            plan_paths(staging),
            ["candis_minimal:default", "display:default", "bsp_api:default", "dock:default"],
        )
        self.assertTrue(paths_need_host_tests(staging))

        tg28_consumer = ["tools/verify_tg28_consumer.sh"]
        self.assertEqual(plan_paths(tg28_consumer), ["tg28:default"])
        self.assertTrue(paths_need_host_tests(tg28_consumer))

    def test_shared_sources_select_all_profiles_for_the_project(self):
        for project in ("adc", "motion", "tf_card", "expansion"):
            with self.subTest(project=project):
                self.assertEqual(
                    plan_paths([f"examples/{project}/main/main.c"]),
                    builds_for_project(project),
                )

    def test_core_component_changes_cover_default_applications(self):
        self.assertEqual(
            plan_paths(["components/candis/src/candis.c"]),
            ALL_DEFAULT_IDS,
        )
        self.assertEqual(
            plan_paths(["components/tg28-esp/src/tg28_esp.c"]),
            ALL_DEFAULT_IDS,
        )

    def test_documentation_changes_use_the_lightweight_check(self):
        readme = ["components/candis/README.md"]
        self.assertEqual(plan_paths(readme), [])
        self.assertFalse(paths_need_host_tests(readme))
        self.assertTrue(paths_need_docs_check(readme))
        self.assertTrue(paths_need_docs_check(["docs/en/api.md", "components/candis/src/candis.c"]))
        checker = ["tools/check_markdown_links.py"]
        self.assertTrue(paths_need_host_tests(checker))
        self.assertTrue(paths_need_docs_check(checker))

    def test_workflow_changes_select_minimum_build_coverage(self):
        self.assertEqual(plan_paths([".github/workflows/full-build.yml"]), CORE_SMOKE_IDS)
        self.assertEqual(
            plan_paths([".github/workflows/registry-release.yml"]),
            [*CORE_SMOKE_IDS, "dock:default"],
        )


if __name__ == "__main__":
    unittest.main()
