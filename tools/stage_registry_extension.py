#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage one extension with a Registry dependency on the corresponding Candis BSP."""

import argparse
from pathlib import Path
import re
import shutil

import yaml


EXTENSIONS = ("candis_dock", "candis_gamepad")
COMPONENTS = Path(__file__).resolve().parents[1] / "components"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=EXTENSIONS)
    parser.add_argument("destination", type=Path, help="new staging directory")
    parser.add_argument("--namespace", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9]+(?:[_-][a-z0-9]+)*", args.namespace):
        parser.error("invalid Registry namespace")

    source = COMPONENTS / args.component
    destination = args.destination.resolve()
    if destination.is_relative_to(source):
        parser.error("stage the component outside its source directory")

    manifest_file = "idf_component.yml"
    main_manifest = yaml.safe_load((COMPONENTS / "candis" / manifest_file).read_text())
    manifest = yaml.safe_load((source / manifest_file).read_text())
    dependencies = manifest["dependencies"]
    candis = dependencies.pop("candis")
    if candis.pop("path") != "../candis":
        parser.error("expected the local Candis BSP path")
    dependencies[f"{args.namespace}/candis"] = {
        "version": f"=={main_manifest['version']}",
        **candis,
    }
    if any(isinstance(dep, dict) and {"path", "override_path"} & dep.keys()
           for dep in dependencies.values()):
        parser.error("a filesystem dependency remains in the Registry manifest")
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("dist", "build", "__pycache__"))
    (destination / manifest_file).write_text(yaml.safe_dump(manifest, sort_keys=False))


if __name__ == "__main__":
    main()
