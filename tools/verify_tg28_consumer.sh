#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

consumer=$1
component=$2
source_spec=$3
example=$(cd "$(dirname "$0")/../examples/tg28" && pwd)

mkdir -p "$consumer/main"
cp "$example"/{CMakeLists.txt,sdkconfig.defaults} "$consumer/"
cp "$example"/main/{CMakeLists.txt,Kconfig.projbuild,main.c} "$consumer/main/"
bash "$(dirname "$0")/copy_example_usb.sh" "$consumer"
printf 'dependencies:\n  %s:\n    %s\n' "$component" "$source_spec" > "$consumer/main/idf_component.yml"
idf.py --preview -C "$consumer" build
