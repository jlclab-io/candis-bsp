#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate and stage independently published Candis components."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

import yaml

try:
    from .stage_registry_extension import LOCAL_DEPENDENCIES, stage_component
except ImportError:
    from stage_registry_extension import LOCAL_DEPENDENCIES, stage_component


ROOT = Path(__file__).resolve().parents[1]
COMPONENTS_DIR = ROOT / "components"
NAMESPACE = "jlclab"
REPOSITORY = "https://github.com/jlclab-io/candis-bsp.git"
REPOSITORY_PREFIX = "https://github.com/jlclab-io/candis-bsp/tree/main/"
SEMVER = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?"
    r"(?:\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$"
)

COMPONENT_DEPENDENCIES = {
    "tg28-esp": (),
    "candis": ("tg28-esp",),
    "candis-camera": (),
    "candis-dock": ("candis",),
}
COMPONENT_ORDER = ("tg28-esp", "candis", "candis-dock", "candis-camera")
FORBIDDEN_ARCHIVE_PARTS = {
    ".git",
    "__pycache__",
    "build",
    "dist",
    "managed_components",
}
FORBIDDEN_ARCHIVE_SUFFIXES = {
    ".bin",
    ".elf",
    ".log",
    ".pyc",
    ".sdkconfig",
}


def component_path(component: str) -> Path:
    return COMPONENTS_DIR / component


def component_manifest(component: str, source: Path | None = None) -> dict:
    path = (source or component_path(component)) / "idf_component.yml"
    manifest = yaml.safe_load(path.read_text())
    if not isinstance(manifest, dict):
        raise ValueError(f"{path} is not a component manifest")
    return manifest


def component_version(component: str, source: Path | None = None) -> str:
    return str(component_manifest(component, source)["version"])


def dependency_closure(component: str) -> list[str]:
    selected: set[str] = set()

    def visit(name: str) -> None:
        if name in selected:
            return
        for dependency in COMPONENT_DEPENDENCIES[name]:
            visit(dependency)
        selected.add(name)

    visit(component)
    return [name for name in COMPONENT_ORDER if name in selected]


def direct_dependency_names(manifest: dict) -> set[str]:
    dependencies = manifest.get("dependencies", {})
    return {name for name in dependencies if name != "idf"}


def validate_changelog(component: str, version: str) -> None:
    path = component_path(component) / "CHANGELOG.md"
    if not path.is_file():
        raise ValueError(f"{path.relative_to(ROOT)} is missing")
    pattern = re.compile(rf"^##\s+(?:\[)?{re.escape(version)}(?:\]|\s|$)", re.MULTILINE)
    if not pattern.search(path.read_text()):
        raise ValueError(
            f"{path.relative_to(ROOT)} must contain a {version} release heading"
        )


def validate_metadata(component: str, require_tag: bool, commit_sha: str | None) -> dict:
    if component not in COMPONENT_DEPENDENCIES:
        raise ValueError(f"unknown component: {component}")

    manifest = component_manifest(component)
    version = str(manifest.get("version", ""))
    if not SEMVER.fullmatch(version):
        raise ValueError(f"invalid SemVer in components/{component}/idf_component.yml")

    expected_path = f"components/{component}"
    if manifest.get("repository") != REPOSITORY:
        raise ValueError(f"{component} must use {REPOSITORY}")
    if manifest.get("repository_info", {}).get("path") != expected_path:
        raise ValueError(f"{component} repository_info.path must be {expected_path}")
    if manifest.get("url") != f"{REPOSITORY_PREFIX}{expected_path}":
        raise ValueError(f"{component} url must point to {expected_path}")
    if component != "tg28-esp" and "esp32s31" not in manifest.get("targets", []):
        raise ValueError(f"{component} must target esp32s31")
    if manifest.get("dependencies", {}).get("idf") != ">=6.1,<6.3":
        raise ValueError(f"{component} must support ESP-IDF >=6.1,<6.3")

    for filename in ("README.md", "LICENSE"):
        if not (component_path(component) / filename).is_file():
            raise ValueError(f"components/{component}/{filename} is missing")
    validate_changelog(component, version)

    unexpected_local_dependencies = {
        name for name in direct_dependency_names(manifest)
        if "/" not in name and name not in COMPONENT_DEPENDENCIES
    }
    if unexpected_local_dependencies:
        names = ", ".join(sorted(unexpected_local_dependencies))
        raise ValueError(f"{component} has unexpected local dependencies: {names}")
    if component in LOCAL_DEPENDENCIES:
        local_dependency = LOCAL_DEPENDENCIES[component]
        if local_dependency not in direct_dependency_names(manifest):
            raise ValueError(f"{component} must declare its local {local_dependency} dependency")

    tag = f"{component}/v{version}"
    if require_tag:
        if not commit_sha:
            raise ValueError("--commit-sha is required with --require-tag")
        tag_commit = subprocess.run(
            ["git", "rev-parse", "--verify", f"{tag}^{{commit}}"],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            text=True,
        ).stdout.strip()
        if tag_commit != commit_sha:
            raise ValueError(f"tag {tag} points to {tag_commit}, not {commit_sha}")

    return {
        "component": component,
        "dependencies": list(COMPONENT_DEPENDENCIES[component]),
        "dependency_closure": dependency_closure(component),
        "package": f"{NAMESPACE}/{component}=={version}",
        "repository_path": expected_path,
        "release_label": tag,
        "version": version,
    }


def command_output(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def safe_extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True)
    with tarfile.open(archive, "r:gz") as source:
        source.extractall(destination, filter="data")


def verify_archive_content(archive: Path) -> None:
    with tarfile.open(archive, "r:gz") as package:
        names = package.getnames()
        for name in names:
            parts = Path(name).parts
            if not parts:
                continue
            if FORBIDDEN_ARCHIVE_PARTS.intersection(parts):
                raise ValueError(f"{archive.name} contains forbidden path {name}")
            if Path(name).suffix in FORBIDDEN_ARCHIVE_SUFFIXES or Path(name).name == "sdkconfig":
                raise ValueError(f"{archive.name} contains build or debug artifact {name}")

    required = {
        "CHANGELOG.md",
        "idf_component.yml",
        "CMakeLists.txt",
        "README.md",
        "LICENSE",
    }
    top_level = {Path(name).parts[0] for name in names if Path(name).parts}
    if not required.issubset(top_level):
        raise ValueError(f"{archive.name} is missing required component files")


def verify_package_manifest(
    component: str,
    package_dir: Path,
    expected_commit: str | None,
) -> dict:
    manifest = component_manifest(component, package_dir)
    expected_path = f"components/{component}"
    if manifest.get("repository_info", {}).get("path") != expected_path:
        raise ValueError(f"packed {component} has the wrong repository path")
    if expected_commit and manifest.get("repository_info", {}).get("commit_sha") != expected_commit:
        raise ValueError(f"packed {component} has the wrong commit SHA")
    if manifest.get("repository") != REPOSITORY:
        raise ValueError(f"packed {component} has the wrong repository URL")

    dependencies = manifest.get("dependencies", {})
    for name, dependency in dependencies.items():
        if name == "idf":
            continue
        if isinstance(dependency, dict) and {"path", "override_path"} & dependency.keys():
            raise ValueError(f"packed {component} contains a filesystem dependency on {name}")
        if name != "idf" and "/" not in name:
            raise ValueError(f"packed {component} contains a local-only dependency on {name}")
    return manifest


def pack_component(
    component: str,
    source: Path,
    packages_root: Path,
    commit_sha: str | None,
) -> tuple[Path, Path]:
    package_output = packages_root / component / "archive"
    extracted_root = packages_root / component / component
    package_output.mkdir(parents=True, exist_ok=True)
    command = [
        "compote", "component", "pack",
        "--project-dir", str(source),
        "--name", component,
        "--repository", REPOSITORY,
        "--repository-path", f"components/{component}",
        "--dest-dir", str(package_output),
    ]
    if commit_sha:
        command.extend(["--commit-sha", commit_sha])
    command_output(command)
    archives = sorted(package_output.glob(f"{component}_*.tgz"))
    if len(archives) != 1:
        raise ValueError(f"expected one packed archive for {component}, found {len(archives)}")
    archive = archives[0]
    verify_archive_content(archive)
    safe_extract(archive, extracted_root)
    verify_package_manifest(component, extracted_root, commit_sha)
    return archive, extracted_root


def prepare(
    component: str,
    output_dir: Path,
    commit_sha: str | None,
) -> dict:
    output_dir = output_dir.resolve()
    if output_dir.exists():
        shutil.rmtree(output_dir)
    staging_root = output_dir / "staging"
    packages_root = output_dir / "packages"
    staging_root.mkdir(parents=True)
    packages_root.mkdir(parents=True)

    staged: list[str] = []
    for name in dependency_closure(component):
        source = component_path(name)
        if name in LOCAL_DEPENDENCIES:
            destination = staging_root / name
            stage_component(name, destination, NAMESPACE)
            source = destination
            staged.append(name)
        pack_component(name, source, packages_root, commit_sha)

    plan = {
        "component": component,
        "dependency_closure": dependency_closure(component),
        "packages": {
            name: str(packages_root / name / name) for name in dependency_closure(component)
        },
        "staged": staged,
        "staging": {name: str(staging_root / name) for name in staged},
        "version": component_version(component),
    }
    (output_dir / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    return plan


def relative_path(from_dir: Path, target: Path) -> str:
    return Path(os.path.relpath(target, from_dir)).as_posix()


def write_consumer_manifest(
    component: str,
    main_dir: Path,
    version: str,
    mode: str,
    packages: dict[str, Path] | None,
) -> None:
    dependencies: dict[str, object] = {}
    if mode == "package":
        if package_dir := (packages or {}).get(component):
            dependencies[component] = {"path": relative_path(main_dir, package_dir)}
        else:
            raise ValueError(f"missing package path for {component}")
        for dependency in dependency_closure(component):
            if dependency == component:
                continue
            dependencies[f"{NAMESPACE}/{dependency}"] = {
                "override_path": relative_path(main_dir, packages[dependency]),
            }
    else:
        dependencies[f"{NAMESPACE}/{component}"] = f"=={version}"
    (main_dir / "idf_component.yml").write_text(
        yaml.safe_dump({"dependencies": dependencies}, sort_keys=False)
    )


def write_consumer_source(component: str, consumer: Path) -> None:
    main = consumer / "main"
    main.mkdir(parents=True)

    if component == "tg28-esp":
        example = ROOT / "examples/tg28"
        for filename in ("CMakeLists.txt", "sdkconfig.defaults"):
            shutil.copy2(example / filename, consumer / filename)
        for filename in ("CMakeLists.txt", "Kconfig.projbuild", "main.c"):
            shutil.copy2(example / "main" / filename, main / filename)
        subprocess.run(["bash", str(ROOT / "tools/copy_example_usb.sh"), str(consumer)], cwd=ROOT, check=True)
        return

    if component == "candis":
        example = ROOT / "examples/display"
        for filename in ("CMakeLists.txt", "sdkconfig.defaults"):
            shutil.copy2(example / filename, consumer / filename)
        for filename in ("CMakeLists.txt", "Kconfig.projbuild", "main.c"):
            shutil.copy2(example / "main" / filename, main / filename)
        subprocess.run(["bash", str(ROOT / "tools/copy_example_usb.sh"), str(consumer)], cwd=ROOT, check=True)
        return

    (consumer / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\n"
        "include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n"
        "idf_build_set_property(MINIMAL_BUILD ON)\n"
        "project(candis_registry_consumer)\n"
    )
    (consumer / "sdkconfig.defaults").write_text('CONFIG_IDF_TARGET="esp32s31"\n')

    if component == "candis-camera":
        (main / "CMakeLists.txt").write_text(
            'idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")\n'
        )
        (main / "main.c").write_text(
            '#include "bsp/candis_camera.h"\n\n'
            'void app_main(void)\n'
            '{\n'
            '    const candis_camera_profile_t *profile =\n'
            '        candis_camera_profile_get(CANDIS_CAMERA_SENSOR_OV5640);\n'
            '    candis_camera_control_info_t control;\n'
            '    int32_t value;\n'
            '    struct v4l2_fmtdesc format;\n'
            '    if (candis_camera_profile_supports_control(profile, CANDIS_CAMERA_CONTROL_HFLIP)) {\n'
            '        (void)candis_camera_control_query(CANDIS_CAMERA_CONTROL_HFLIP, &control);\n'
            '        (void)candis_camera_control_get(CANDIS_CAMERA_CONTROL_HFLIP, &value);\n'
            '        (void)candis_camera_control_set(CANDIS_CAMERA_CONTROL_HFLIP, 1);\n'
            '    }\n'
            '    (void)candis_camera_get_pixel_format(0, &format);\n'
            '    ESP_ERROR_CHECK(candis_camera_stop());\n'
            '}\n'
        )
        return

    (main / "CMakeLists.txt").write_text(
        'idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")\n'
    )
    (main / "main.c").write_text(
        '#include "bsp/candis_dock.h"\n\n'
        'void app_main(void)\n'
        '{\n'
        '    (void)candis_dock_probe_modules(NULL, NULL);\n'
        '}\n'
    )


def build_consumer(
    component: str,
    output_dir: Path,
    mode: str,
    packages: dict[str, Path] | None,
    commit_sha: str | None,
) -> None:
    consumer = output_dir.resolve()
    if consumer.exists():
        shutil.rmtree(consumer)
    version = component_version(component)
    write_consumer_source(component, consumer)
    write_consumer_manifest(component, consumer / "main", version, mode, packages)
    command_output(["idf.py", "--preview", "-C", str(consumer), "build"])
    lock = yaml.safe_load((consumer / "dependencies.lock").read_text())

    if component == "candis-camera":
        if any(name.split("/")[-1] == "candis" for name in lock.get("dependencies", {})):
            raise ValueError("candis-camera must not pull the Candis BSP")

    if mode == "registry":
        managed = consumer / "managed_components" / f"{NAMESPACE}__{component}"
        manifest = component_manifest(component, managed)
        if manifest.get("version") != version:
            raise ValueError(f"Registry installed the wrong {component} version")
        if commit_sha and manifest.get("repository_info", {}).get("commit_sha") != commit_sha:
            raise ValueError(f"Registry installed {component} from the wrong commit")


def check_registry_version(
    component: str,
    source_dir: Path,
    version: str,
    commit_sha: str | None,
    destination: Path,
) -> None:
    command = [
        "compote", "component", "upload",
        "--check-only",
        "--project-dir", str(source_dir.resolve()),
        "--name", component,
        "--namespace", NAMESPACE,
        "--repository", REPOSITORY,
        "--repository-path", f"components/{component}",
        "--dest-dir", str(destination.resolve()),
    ]
    if commit_sha:
        command.extend(["--commit-sha", commit_sha])
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    print("+ " + " ".join(command), flush=True)
    print(result.stdout, end="", flush=True)
    if result.returncode == 0:
        return
    if "already on the registry" in result.stdout:
        raise SystemExit(
            f"Registry version {NAMESPACE}/{component}=={version} already exists; "
            "increment the manifest only for a new formal release"
        )
    raise subprocess.CalledProcessError(result.returncode, command)


def dry_run(
    component: str,
    source_dir: Path,
    commit_sha: str | None,
    destination: Path,
) -> None:
    command = [
        "compote", "component", "upload",
        "--dry-run",
        "--project-dir", str(source_dir.resolve()),
        "--name", component,
        "--namespace", NAMESPACE,
        "--repository", REPOSITORY,
        "--repository-path", f"components/{component}",
        "--dest-dir", str(destination.resolve()),
    ]
    if commit_sha:
        command.extend(["--commit-sha", commit_sha])
    command_output(command)


def promote_stage(component: str, staging: Path, repository_root: Path) -> None:
    if component not in LOCAL_DEPENDENCIES:
        return
    source = staging / component
    destination = repository_root / "components" / component
    if not (source / "idf_component.yml").is_file():
        raise ValueError(f"missing staged component at {source}")
    shutil.rmtree(destination)
    shutil.copytree(source, destination)


def load_plan(path: Path) -> dict:
    plan_file = path / "plan.json" if path.is_dir() else path
    return json.loads(plan_file.read_text())


def package_paths(plan: dict) -> dict[str, Path]:
    return {name: Path(path) for name, path in plan["packages"].items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--component", choices=COMPONENT_DEPENDENCIES, required=True)
    validate_parser.add_argument("--require-tag", action="store_true")
    validate_parser.add_argument("--commit-sha")

    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--component", choices=COMPONENT_DEPENDENCIES, required=True)
    prepare_parser.add_argument("--output-dir", type=Path, required=True)
    prepare_parser.add_argument("--commit-sha")

    consumer_parser = subparsers.add_parser("build-consumer")
    consumer_parser.add_argument("--component", choices=COMPONENT_DEPENDENCIES, required=True)
    consumer_parser.add_argument("--plan", type=Path, required=True)
    consumer_parser.add_argument("--output-dir", type=Path, required=True)
    consumer_parser.add_argument("--mode", choices=("package", "registry"), required=True)
    consumer_parser.add_argument("--commit-sha")

    check_parser = subparsers.add_parser("check-version")
    check_parser.add_argument("--component", choices=COMPONENT_DEPENDENCIES, required=True)
    check_parser.add_argument("--source-dir", type=Path, required=True)
    check_parser.add_argument("--commit-sha")
    check_parser.add_argument("--output-dir", type=Path, required=True)

    dry_parser = subparsers.add_parser("dry-run")
    dry_parser.add_argument("--component", choices=COMPONENT_DEPENDENCIES, required=True)
    dry_parser.add_argument("--source-dir", type=Path, required=True)
    dry_parser.add_argument("--commit-sha")
    dry_parser.add_argument("--output-dir", type=Path, required=True)

    promote_parser = subparsers.add_parser("promote-stage")
    promote_parser.add_argument("--component", choices=COMPONENT_DEPENDENCIES, required=True)
    promote_parser.add_argument("--prepared-dir", type=Path, required=True)
    promote_parser.add_argument("--repository-root", type=Path, required=True)

    args = parser.parse_args()
    try:
        if args.command == "validate":
            print(json.dumps(validate_metadata(args.component, args.require_tag, args.commit_sha)))
        elif args.command == "prepare":
            print(json.dumps(prepare(args.component, args.output_dir, args.commit_sha), indent=2))
        elif args.command == "build-consumer":
            plan = load_plan(args.plan)
            build_consumer(
                args.component,
                args.output_dir,
                args.mode,
                package_paths(plan),
                args.commit_sha,
            )
        elif args.command == "check-version":
            check_registry_version(
                args.component,
                args.source_dir,
                component_version(args.component, args.source_dir),
                args.commit_sha,
                args.output_dir,
            )
        elif args.command == "dry-run":
            dry_run(args.component, args.source_dir, args.commit_sha, args.output_dir)
        elif args.command == "promote-stage":
            promote_stage(args.component, args.prepared_dir / "staging", args.repository_root)
    except (OSError, ValueError, subprocess.CalledProcessError, yaml.YAMLError) as error:
        print(f"Registry release error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
