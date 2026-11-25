#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage a Candis component with its monorepo dependency resolved through the Registry."""

import argparse
from pathlib import Path
import re
import shutil

import yaml


LOCAL_DEPENDENCIES = {
    "candis": "tg28-esp",
    "candis-dock": "candis",
}
COMPONENTS = Path(__file__).resolve().parents[1] / "components"


def stage_component(component: str, destination: Path, namespace: str) -> Path:
    """Stage one extension without filesystem dependencies."""
    if component not in LOCAL_DEPENDENCIES:
        raise ValueError(f"{component} does not require Registry staging")
    if not re.fullmatch(r"[a-z0-9]+(?:[_-][a-z0-9]+)*", namespace):
        raise ValueError("invalid Registry namespace")

    source = COMPONENTS / component
    destination = destination.resolve()
    if destination.is_relative_to(source):
        raise ValueError("stage the component outside its source directory")

    manifest_file = "idf_component.yml"
    manifest = yaml.safe_load((source / manifest_file).read_text())
    dependencies = manifest["dependencies"]
    dependency_name = LOCAL_DEPENDENCIES[component]
    dependency = dependencies.pop(dependency_name)
    if dependency.pop("path") != f"../{dependency_name}":
        raise ValueError(f"expected the local {dependency_name} path")
    dependency_manifest = yaml.safe_load((COMPONENTS / dependency_name / manifest_file).read_text())
    dependencies[f"{namespace}/{dependency_name}"] = {
        **dependency,
        "version": f"=={dependency_manifest['version']}",
    }
    if any(isinstance(dep, dict) and {"path", "override_path"} & dep.keys()
           for dep in dependencies.values()):
        raise ValueError("a filesystem dependency remains in the Registry manifest")
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("dist", "build", "__pycache__"))
    (destination / manifest_file).write_text(yaml.safe_dump(manifest, sort_keys=False))
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=LOCAL_DEPENDENCIES)
    parser.add_argument("destination", type=Path, help="new staging directory")
    parser.add_argument("--namespace", required=True)
    args = parser.parse_args()
    try:
        stage_component(args.component, args.destination, args.namespace)
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
