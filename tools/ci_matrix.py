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
        ("diagnostics", "features"),
        ("audio", "default"),
        ("music_player", "default"),
        ("power", "default"),
        ("motor", "default"),
        ("tg28", "default"),
    ],
    "camera": [
        ("camera", "default"),
        ("camera", "fixture"),
        ("camera", "jpeg"),
        ("camera", "yuyv"),
        ("camera", "controls"),
        ("camera", "manual_controls"),
        ("camera", "manual"),
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
        ("sleep", "default"),
        ("flash", "default"),
    ],
}

EXTRA_DEFAULTS = {
    ("motion", "dock"): ("@motion-storage",),
    ("motion", "fixture"): ("@motion-storage", "@motion-fixture"),
}
CONFIG_FRAGMENTS = {
    ("diagnostics", "features"): ("features",),
    ("camera", "fixture"): ("fixture",),
    ("camera", "jpeg"): ("fixture", "jpeg"),
    ("camera", "yuyv"): ("fixture", "yuyv", "jpeg", "manual"),
    ("camera", "controls"): ("fixture", "bench", "controls", "uyvy"),
    ("camera", "manual_controls"): ("fixture", "controls", "uyvy", "manual"),
    ("camera", "manual"): ("fixture", "manual"),
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
            if component == "candis":
                selected.update(CORE_SMOKE_IDS)
            elif component == "candis-camera":
                selected.update(builds_for_project("camera"))
            elif component == "candis_dock":
                selected.update(
                    {
                        "dock:default",
                        "expansion:dock_tf",
                        "motion:dock",
                    }
                )
            elif component == "tg28-esp":
                selected.add("tg28:default")
            else:
                selected.update(ALL_DEFAULT_IDS)
        elif parts[0] == "examples" and len(parts) > 1:
            project = parts[1]
            if project == "common":
                selected.update(ALL_DEFAULT_IDS)
            elif project in DEFAULT_BUILD_BY_PROJECT:
                if path.name.startswith("sdkconfig") or path.name == "Kconfig.projbuild":
                    selected.update(builds_for_project(project))
                else:
                    selected.add(DEFAULT_BUILD_BY_PROJECT[project].build_id)
            else:
                selected.update(ALL_DEFAULT_IDS)
        elif parts[0] == "tests" and len(parts) > 1:
            if parts[1] == "bsp_api":
                selected.add("bsp_api:default")
        elif parts[:2] == ("tools", "copy_example_usb.sh"):
            selected.update(ALL_DEFAULT_IDS)
        elif parts[:2] == (".github", "actions"):
            selected.update(ALL_DEFAULT_IDS)
        elif parts[:2] == (".github", "workflows") or parts[0] == "tools":
            if raw_path == "tools/ci_matrix.py":
                selected.update(CORE_SMOKE_IDS)
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
        if parts[0] in {"components", "examples", "tests"}:
            return True
        if raw_path == "tools/ci_matrix.py":
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
        else:
            builds = plan_paths(paths)
            host_tests = paths_need_host_tests(paths)
        groups = groups_for_builds(builds)
        print(
            json.dumps(
                {"builds": builds, "groups": groups, "host_tests": host_tests},
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
