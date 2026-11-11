#!/bin/sh
set -eu

gallery_repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
gallery_build="$gallery_repo/build/debug"
cmake --build "$gallery_build" --target VulkanApp
cd "$gallery_build/apps/editor"
exec ./VulkanApp --preparation-gallery
