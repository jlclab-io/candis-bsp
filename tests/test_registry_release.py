# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import tempfile
import unittest

import yaml

from tools.registry_release import (
    COMPONENT_DEPENDENCIES,
    COMPONENT_ORDER,
    NAMESPACE,
    dependency_closure,
    validate_metadata,
    promote_stage,
)
from tools.stage_registry_extension import stage_component


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/registry-release.yml"
GUIDE = ROOT / ".github/workflows/ESP-Registry-Push.md"


def workflow_config() -> dict:
    config = yaml.safe_load(WORKFLOW.read_text())
    return config


def workflow_triggers(config: dict) -> dict:
    return config.get("on", config.get(True, {}))


class RegistryReleaseMetadataTest(unittest.TestCase):
    def test_all_components_have_independent_release_metadata(self):
        for component in COMPONENT_ORDER:
            with self.subTest(component=component):
                metadata = validate_metadata(component, require_tag=False, commit_sha=None)
                self.assertEqual(metadata["package"], f"{NAMESPACE}/{component}==0.1.0")
                self.assertEqual(metadata["release_label"], f"{component}/v0.1.0")

    def test_dependency_closures_match_the_component_boundaries(self):
        self.assertEqual(dependency_closure("tg28-esp"), ["tg28-esp"])
        self.assertEqual(dependency_closure("candis"), ["tg28-esp", "candis"])
        self.assertEqual(
            dependency_closure("candis-dock"),
            ["tg28-esp", "candis", "candis-dock"],
        )
        self.assertEqual(dependency_closure("candis-camera"), ["candis-camera"])

    def test_extension_staging_rewrites_only_the_local_dependency(self):
        for component in ("candis", "candis-dock"):
            with self.subTest(component=component), tempfile.TemporaryDirectory() as temp:
                staged = stage_component(component, Path(temp) / component, NAMESPACE)
                manifest = yaml.safe_load((staged / "idf_component.yml").read_text())
                local_dependency = COMPONENT_DEPENDENCIES[component][0]
                self.assertIn(f"{NAMESPACE}/{local_dependency}", manifest["dependencies"])
                self.assertNotIn(local_dependency, manifest["dependencies"])
                self.assertFalse(
                    any(
                        isinstance(value, dict) and {"path", "override_path"} & value.keys()
                        for value in manifest["dependencies"].values()
                    )
                )
                self.assertEqual(
                    manifest["dependencies"][f"{NAMESPACE}/{local_dependency}"]["version"],
                    "==0.1.0",
                )

    def test_promoted_stage_is_visible_at_the_original_component_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            prepared = root / "prepared"
            stage_component("candis", prepared / "staging/candis", NAMESPACE)
            destination = root / "repository/components/candis"
            destination.mkdir(parents=True)
            promote_stage("candis", prepared / "staging", root / "repository")
            manifest = yaml.safe_load((destination / "idf_component.yml").read_text())
            self.assertIn(f"{NAMESPACE}/tg28-esp", manifest["dependencies"])
            self.assertNotIn("tg28-esp", manifest["dependencies"])


class RegistryReleaseWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.config = workflow_config()

    def test_only_manual_dispatch_can_run_the_workflow(self):
        self.assertEqual(set(workflow_triggers(self.config)), {"workflow_dispatch"})

    def test_manual_inputs_select_one_component_and_default_to_validation(self):
        inputs = workflow_triggers(self.config)["workflow_dispatch"]["inputs"]
        component = inputs["component"]
        self.assertEqual(component["type"], "choice")
        self.assertEqual(set(component["options"]), set(COMPONENT_ORDER))
        self.assertEqual(inputs["publish"]["type"], "boolean")
        self.assertIs(inputs["publish"]["default"], False)

    def test_no_long_lived_registry_credentials_are_referenced(self):
        text = WORKFLOW.read_text()
        self.assertNotIn("IDF_COMPONENT_API_TOKEN", text)
        self.assertNotIn("secrets.", text)

    def test_only_the_environment_protected_publish_job_can_request_an_oidc_token(self):
        top_level_permissions = self.config.get("permissions", {})
        self.assertNotIn("id-token", top_level_permissions)
        publish = self.config["jobs"]["publish"]
        self.assertEqual(publish["environment"], "registry")
        self.assertEqual(publish["permissions"]["id-token"], "write")
        self.assertIn("github.ref == 'refs/heads/main'", publish["if"])
        self.assertIn("inputs.publish", publish["if"])
        for name, job in self.config["jobs"].items():
            if name == "publish":
                continue
            self.assertNotIn("id-token", job.get("permissions", {}))

    def test_official_upload_action_is_pinned_to_a_reviewed_release_commit(self):
        text = WORKFLOW.read_text()
        self.assertIn(
            "espressif/upload-components-ci-action@2ce17d73e35473317c1419dbd4f007aacb593040",
            text,
        )
        self.assertNotIn("upload-components-ci-action@v", text)

    def test_publish_does_not_require_a_repository_tag(self):
        self.assertNotIn("--require-tag", WORKFLOW.read_text())

    def test_publish_checks_for_existing_versions_before_the_official_action(self):
        steps = self.config["jobs"]["publish"]["steps"]
        check_index = next(
            index for index, step in enumerate(steps)
            if "check-version" in step.get("run", "")
        )
        action_index = next(
            index for index, step in enumerate(steps)
            if step.get("uses", "").startswith("espressif/upload-components-ci-action@")
        )
        self.assertLess(check_index, action_index)

    def test_pack_steps_activate_the_esp_idf_environment_before_invoking_compote(self):
        for job_name in ("validate", "publish"):
            with self.subTest(job=job_name):
                steps = self.config["jobs"][job_name]["steps"]
                prepare_steps = [
                    step["run"] for step in steps
                    if "registry_release.py prepare" in step.get("run", "")
                ]
                self.assertEqual(len(prepare_steps), 1)
                self.assertIn(
                    'source "$RUNNER_TEMP/esp-idf/export.sh"',
                    prepare_steps[0],
                )

    def test_python_validation_steps_activate_the_idf_python_environment(self):
        for job_name in ("validate", "publish"):
            with self.subTest(job=job_name):
                steps = self.config["jobs"][job_name]["steps"]
                validation_steps = [
                    step["run"] for step in steps
                    if "registry_release.py validate" in step.get("run", "")
                ]
                self.assertEqual(len(validation_steps), 1)
                self.assertIn(
                    'source "$RUNNER_TEMP/esp-idf/export.sh"',
                    validation_steps[0],
                )

    def test_maintenance_guide_matches_the_release_contract(self):
        guide = GUIDE.read_text()
        for heading in (
            "## 1. Monorepo 多组件发布架构",
            "## 5. OIDC 认证及权限要求",
            "## 10. 首次发布顺序",
            "## 13. 后续新增组件的接入流程",
        ):
            self.assertIn(heading, guide)
        for value in (
            "jlclab-io/candis-bsp",
            "registry-release.yml",
            "environment: registry",
            "2ce17d73e35473317c1419dbd4f007aacb593040",
        ):
            self.assertIn(value, guide)
        self.assertNotIn("/home/", guide)
        self.assertNotIn("/Users/", guide)


if __name__ == "__main__":
    unittest.main()
