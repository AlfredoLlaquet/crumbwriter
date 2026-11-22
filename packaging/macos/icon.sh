#!/bin/sh
# Build the Finder icon from the same favicon artwork used on the other desktops.
set -eu
icons=$1
output=$2
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
iconset="$scratch/CrumbWriter.iconset"
mkdir "$iconset"
for logical in 16 32 128 256 512; do
    for scale in 1 2; do
        pixels=$((logical * scale))
        source="$icons/${pixels}x${pixels}/apps/org.thecrumbs.CrumbWriter.png"
        name="icon_${logical}x${logical}"
        if [ "$scale" = 2 ]; then name="$name@2x"; fi
        if [ -f "$source" ]; then
            cp "$source" "$iconset/$name.png"
        else
            sips -s format png -z "$pixels" "$pixels" "$icons/256x256/apps/org.thecrumbs.CrumbWriter.png" \
                --out "$iconset/$name.png" >/dev/null
        fi
    done
done
iconutil -c icns "$iconset" --output "$output"
