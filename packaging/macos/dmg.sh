#!/bin/bash
# Produce one drag-to-Applications disk image from the two independently tested builds.
set -euo pipefail
archives=$1
output=$2
working=$(mktemp -d)
trap 'rm -rf "$working"' EXIT
for cpu in arm64 x86_64; do
    mkdir "$working/$cpu"
    tar -xzf "$archives/native-$cpu.tar.gz" -C "$working/$cpu"
done
payload="$working/disk"
mkdir "$payload"
python3 packaging/macos/bundle.py merge "$working/arm64/Crumb Writer.app" \
    "$working/x86_64/Crumb Writer.app" "$payload/Crumb Writer.app"
ln -s /Applications "$payload/Applications"
cp LICENSE "$payload/LICENSE"
cp packaging/macos/Read-me.txt "$payload/Read Me.txt"
hdiutil create -quiet -fs HFS+ -format UDZO -volname 'Crumb Writer' -srcfolder "$payload" "$output"
hdiutil verify "$output"
