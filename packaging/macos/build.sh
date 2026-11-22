#!/bin/bash
# Native build on each CPU. The resulting bundles are merged after both tests pass.
set -euo pipefail
qt=$1
build=$2
assets=$3
cpu=$(uname -m)
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt" -DCMAKE_OSX_ARCHITECTURES="$cpu" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build "$build" --parallel 3
cmake --install "$build" --prefix "$build/installed"
app="$build/installed/Crumb Writer.app"
resources="$app/Contents/Resources"
python3 packaging/macos/bundle.py prepare "$app" "$qt" "$build/brew-runtime.txt"
# Dictionary staging precedes the matching-source downloads. If a dependency
# mirror fails later, still exercise all available native editor tests.
source_status=0
python3 packaging/shared/stage.py --destination "$resources" --qt-prefix "$qt" \
    --homebrew-runtime-list "$build/brew-runtime.txt" --sources "$build/runtime-sources" || source_status=$?
python3 packaging/macos/bundle.py sign "$app"
python3 packaging/macos/bundle.py verify "$app" "$cpu"

LC_ALL=en_US.UTF-8 DICPATH="$resources/dictionaries" QT_QPA_PLATFORM=offscreen \
    ctest --test-dir "$build" --output-on-failure --timeout 240
env -u DICPATH -u QT_PLUGIN_PATH -u DYLD_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH \
    "$app/Contents/MacOS/Crumb Writer" --smoke-test
if ((source_status != 0)); then exit "$source_status"; fi
mkdir -p "$assets"
COPYFILE_DISABLE=1 tar -czf "$assets/native-$cpu.tar.gz" -C "$build/installed" 'Crumb Writer.app'
COPYFILE_DISABLE=1 tar -cf "$assets/CrumbWriter-macOS-$cpu-Sources.tar" -C "$build/runtime-sources" .
