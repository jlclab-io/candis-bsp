#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$1/components"
cp -R "$repo/examples/common/candis_example_usb" "$1/components/"
