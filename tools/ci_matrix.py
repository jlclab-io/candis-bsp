#!/usr/bin/env python3
"""Plan and run the Candis BSP ESP-IDF CI build matrix."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


REPO_ROOT = Path(__file__).resolve().parents[1]
GROUPS = {
    "core": [
        ("candis_minimal", "default"),
        ("display", "default"),
        ("bsp_api", "default"),
        ("audio", "default"),
        ("music_player", "default"),
        ("power", "default"),
        ("motor", "default"),
        ("tg28", "default"),
    ],
    "camera": [
        ("camera", "default"),
        ("camera", "fixture"),
        ("camera", "dvp_all"),
        ("camera", "jpeg"),
        ("camera", "yuyv"),
        ("camera", "uyvy"),
    ],
    "camera-models-1": [
        ("camera", "bf20a6"),
        ("camera", "bf3031"),
        ("camera", "bf3045"),
        ("camera", "bf3925"),
        ("camera", "bf3a03"),
        ("camera", "gc0308"),
        ("camera", "gc032a"),
        ("camera", "gc2145"),
        ("camera", "mt9d111"),
    ],
    "camera-models-2": [
        ("camera", "ov2640"),
        ("camera", "ov3640"),
        ("camera", "ov3660"),
        ("camera", "ov5640"),
        ("camera", "sc030iot"),
        ("camera", "sc101iot"),
        ("camera", "sc2336"),
        ("camera", "sp0a39"),
    ],
    "expansion": [
        ("expansion", "default"),
        ("expansion", "dock_tf"),
        ("dock", "default"),
        ("motion", "default"),
        ("motion", "dock"),
        ("motion", "fixture"),
        ("tf_card", "default"),
        ("tf_card", "fixture"),
        ("adc", "default"),
        ("adc", "fixture"),
        ("baseboard", "default"),
    ],
    "system": [
        ("rtc", "default"),
        ("gpio", "default"),
        ("wifi_scan", "default"),
        ("ble_scan", "default"),
        ("bleprph_wifi_coex", "default"),
        ("sleep", "default"),
        ("flash", "default"),
    ],
}

EXTRA_DEFAULTS = {
    ("motion", "dock"): ("@motion-storage",),
    ("motion", "fixture"): ("@motion-storage", "@motion-fixture"),
}
CONFIG_FRAGMENTS = {
    ("camera", "fixture"): ("fixture",),
    ("camera", "dvp_all"): ("dvp_all",),
    ("camera", "jpeg"): ("fixture", "jpeg"),
    ("camera", "yuyv"): ("fixture", "yuyv", "jpeg"),
    ("camera", "uyvy"): ("fixture", "uyvy"),
    ("camera", "bf20a6"): ("bf20a6",),
    ("camera", "bf3031"): ("bf3031",),
    ("camera", "bf3045"): ("bf3045",),
    ("camera", "bf3925"): ("bf3925",),
    ("camera", "bf3a03"): ("bf3a03",),
    ("camera", "gc0308"): ("gc0308",),
    ("camera", "gc032a"): ("gc032a",),
    ("camera", "gc2145"): ("gc2145",),
    ("camera", "mt9d111"): ("mt9d111",),
    ("camera", "ov2640"): ("ov2640",),
    ("camera", "ov3640"): ("ov3640",),
    ("camera", "ov3660"): ("ov3660",),
    ("camera", "ov5640"): ("ov5640",),
    ("camera", "sc030iot"): ("sc030iot",),
    ("camera", "sc101iot"): ("sc101iot",),
    ("camera", "sc2336"): ("sc2336",),
    ("camera", "sp0a39"): ("sp0a39",),
    ("expansion", "dock_tf"): ("tf_test", "modules"),
    ("tf_card", "fixture"): ("fixture", "write_test"),
    ("adc", "fixture"): ("fixture",),
}


@dataclass(frozen=True)
class Build:
    project: str
    profile: str

    @property
    def build_id(self) -> str:
        return f"{self.project}:{self.profile}"


BUILDS_BY_GROUP = {
    group: [Build(project, profile) for project, profile in builds]
    for group, builds in GROUPS.items()
}
ALL_BUILDS = [build for builds in BUILDS_BY_GROUP.values() for build in builds]
BUILDS_BY_ID = {build.build_id: build for build in ALL_BUILDS}
GROUP_BY_BUILD_ID = {
    build.build_id: group
    for group, builds in BUILDS_BY_GROUP.items()
    for build in builds
}
DEFAULT_BUILD_BY_PROJECT: dict[str, Build] = {}
for build in ALL_BUILDS:
    DEFAULT_BUILD_BY_PROJECT.setdefault(build.project, build)

ALL_BUILD_IDS = [build.build_id for build in ALL_BUILDS]
ALL_DEFAULT_IDS = [build.build_id for build in DEFAULT_BUILD_BY_PROJECT.values()]
CORE_SMOKE_IDS = ["candis_minimal:default", "display:default", "bsp_api:default"]


def builds_for_project(project: str) -> list[str]:
    return [build.build_id for build in ALL_BUILDS if build.project == project]


def groups_for_builds(build_ids: list[str]) -> list[str]:
    selected = set(build_ids)
    return [
        group
        for group, builds in BUILDS_BY_GROUP.items()
        if any(build.build_id in selected for build in builds)
    ]


def plan_paths(paths: list[str]) -> list[str]:
    selected: set[str] = set()

    for raw_path in paths:
        path = PurePosixPath(raw_path)
        parts = path.parts
        suffix = path.suffix.lower()

        if parts[0] == "docs" or suffix in {".md", ".rst", ".adoc"}:
            continue

        if parts[:3] == (".github", "actions", "setup-esp32s31"):
            selected.update(ALL_DEFAULT_IDS)
        elif parts[0] == "components" and len(parts) > 1:
            component = parts[1]
            if component in {"candis", "tg28-esp"}:
                selected.update(ALL_DEFAULT_IDS)
            elif component == "candis-camera":
                selected.update(builds_for_project("camera"))
            elif component == "candis-dock":
                selected.update(
                    {
                        "dock:default",
                        "expansion:dock_tf",
                        "motion:dock",
                    }
                )
            else:
                selected.update(ALL_DEFAULT_IDS)
        elif parts[0] == "examples" and len(parts) > 1:
            project = parts[1]
            if project == "common":
                selected.update(ALL_DEFAULT_IDS)
            elif project in DEFAULT_BUILD_BY_PROJECT:
                if project == "camera" and path.name in {"still_jpeg.c", "still_jpeg.h"}:
                    selected.update({"camera:jpeg", "camera:yuyv"})
                elif project == "camera":
                    selected.update(builds_for_project(project))
                else:
                    selected.update(builds_for_project(project))
            else:
                selected.update(ALL_DEFAULT_IDS)
        elif parts[0] == "tests" and len(parts) > 1:
            if parts[1] == "bsp_api":
                selected.add("bsp_api:default")
        elif parts[:2] == ("tools", "copy_example_usb.sh"):
            selected.update(ALL_DEFAULT_IDS)
        elif parts[:2] == (".github", "actions"):
            selected.update(ALL_DEFAULT_IDS)
        elif parts[:2] == (".github", "workflows"):
            selected.update(CORE_SMOKE_IDS)
            if raw_path == ".github/workflows/registry-release.yml":
                selected.add("dock:default")
            continue
        elif parts[0] == "tools":
            if raw_path == "tools/ci_matrix.py":
                selected.update(CORE_SMOKE_IDS)
            elif raw_path == "tools/stage_registry_extension.py":
                selected.update(CORE_SMOKE_IDS)
                selected.add("dock:default")
            elif raw_path == "tools/verify_tg28_consumer.sh":
                selected.add("tg28:default")
            continue
        elif path.name in {"CMakeLists.txt", "Kconfig", "Kconfig.projbuild"} or suffix in {
            ".c",
            ".h",
            ".cc",
            ".cpp",
            ".cmake",
            ".yml",
            ".yaml",
            ".lock",
        }:
            selected.update(ALL_DEFAULT_IDS)

    return [build_id for build_id in ALL_BUILD_IDS if build_id in selected]


def paths_need_host_tests(paths: list[str]) -> bool:
    for raw_path in paths:
        path = PurePosixPath(raw_path)
        parts = path.parts
        if path.suffix.lower() == ".md":
            continue
        if parts[0] in {"components", "examples", "tests"}:
            return True
        if raw_path in {
            "tools/ci_matrix.py",
            "tools/stage_registry_extension.py",
            "tools/verify_tg28_consumer.sh",
            "tools/check_markdown_links.py",
        }:
            return True
        if parts[0] not in {".github", "docs", "tools"} and path.suffix.lower() in {
            ".c",
            ".h",
            ".cc",
            ".cpp",
            ".cmake",
            ".yml",
            ".yaml",
            ".lock",
        }:
            return True
    return False


def paths_need_docs_check(paths: list[str]) -> bool:
    return any(raw_path == "tools/check_markdown_links.py" or
               PurePosixPath(raw_path).suffix.lower() == ".md"
               for raw_path in paths)


def changed_paths_from_event() -> list[str]:
    event_path = os.environ.get("GITHUB_EVENT_PATH")
    event_name = os.environ.get("GITHUB_EVENT_NAME")
    head = os.environ.get("GITHUB_SHA")
    if not event_path or not event_name or not head:
        raise RuntimeError("GitHub event context is required to calculate changed paths")

    event = json.loads(Path(event_path).read_text())
    if event_name == "push":
        base = event.get("before", "")
    elif event_name == "pull_request":
        base = event["pull_request"]["base"]["sha"]
    else:
        return []

    if not base or set(base) == {"0"}:
        return [""]

    subprocess.run(
        ["git", "fetch", "--no-tags", "--depth=1", "origin", base],
        cwd=REPO_ROOT,
        check=True,
    )
    result = subprocess.run(
        ["git", "diff", "--name-only", "-z", base, head],
        cwd=REPO_ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [item.decode("utf-8", "surrogateescape") for item in result.stdout.split(b"\0") if item]


def extra_default_paths() -> dict[str, Path]:
    output_dir = Path(os.environ["RUNNER_TEMP"]) / "candis-ci-defaults"
    output_dir.mkdir(parents=True, exist_ok=True)
    motion_storage = output_dir / "motion-storage.defaults"
    motion_fixture = output_dir / "motion-fixture.defaults"
    motion_storage.write_text(
        "CONFIG_CANDIS_MOTION_MANAGE_POWER=y\nCONFIG_CANDIS_MOTION_TEST_STORAGE=y\n"
    )
    motion_fixture.write_text("CONFIG_CANDIS_MOTION_FIXTURE=y\n")
    return {
        "@motion-storage": motion_storage,
        "@motion-fixture": motion_fixture,
    }


def run_build(build_id: str) -> None:
    build = BUILDS_BY_ID[build_id]
    project = "tests/bsp_api" if build.project == "bsp_api" else f"examples/{build.project}"
    command = ["idf.py", "--preview", "-C", project]
    build_key = build_id.replace(":", "-")
    run_temp = Path(os.environ["RUNNER_TEMP"]) / "candis-ci"
    sdkconfig = run_temp / "sdkconfig" / build_key
    sdkconfig.parent.mkdir(parents=True, exist_ok=True)
    command.extend(
        [
            "-B",
            str(run_temp / "builds" / build_key),
            "-D",
            f"SDKCONFIG={sdkconfig}",
        ]
    )
    fragments = CONFIG_FRAGMENTS.get((build.project, build.profile), ())
    extra_fragments = EXTRA_DEFAULTS.get((build.project, build.profile), ())

    if fragments or extra_fragments:
        defaults = ["sdkconfig.defaults"]
        defaults.extend(f"sdkconfig.{fragment}" for fragment in fragments)
        extra_paths = extra_default_paths() if extra_fragments else {}
        defaults.extend(str(extra_paths[item]) for item in extra_fragments)
        command.extend(["-D", f"SDKCONFIG_DEFAULTS={';'.join(defaults)}"])

    command.append("build")
    print(f"CI_BUILD {build_id}", flush=True)
    subprocess.run(command, cwd=REPO_ROOT, check=True)
    print(f"CI_BUILD_PASS {build_id}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan_parser = subparsers.add_parser("plan")
    plan_parser.add_argument("--path", action="append", default=[])

    build_parser = subparsers.add_parser("build")
    build_selector = build_parser.add_mutually_exclusive_group(required=True)
    build_selector.add_argument("--group", choices=BUILDS_BY_GROUP)
    build_selector.add_argument("--targets")
    build_parser.add_argument("--only-group", choices=BUILDS_BY_GROUP)

    args = parser.parse_args()
    if args.command == "plan":
        paths = args.path if args.path else changed_paths_from_event()
        if paths == [""]:
            builds = ALL_BUILD_IDS
            host_tests = True
            docs_check = True
        else:
            builds = plan_paths(paths)
            host_tests = paths_need_host_tests(paths)
            docs_check = paths_need_docs_check(paths)
        groups = groups_for_builds(builds)
        print(
            json.dumps(
                {"builds": builds, "groups": groups, "host_tests": host_tests, "docs_check": docs_check},
                separators=(",", ":"),
            )
        )
        return 0

    if args.group and args.only_group:
        parser.error("--only-group can only be used with --targets")

    if args.group:
        builds = [build.build_id for build in BUILDS_BY_GROUP[args.group]]
    else:
        plan = json.loads(args.targets)
        builds = plan["builds"] if isinstance(plan, dict) else plan
        unknown = set(builds) - set(BUILDS_BY_ID)
        if unknown:
            parser.error(f"unknown build targets: {', '.join(sorted(unknown))}")
        if args.only_group:
            builds = [
                build_id
                for build_id in builds
                if GROUP_BY_BUILD_ID[build_id] == args.only_group
            ]

    for build_id in builds:
        run_build(build_id)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"CI matrix error: {error}", file=sys.stderr)
        raise SystemExit(1)
