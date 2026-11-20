#!/bin/sh
# Build the same KWin script for both native desktop test jobs.
set -eu
destination=${1:?usage: build-krohnkite.sh output-directory}
mkdir -p "$destination"
destination=$(cd "$destination" && pwd)
checkout=$(mktemp -d)
trap 'rm -rf "$checkout"' EXIT
revision=e345c9a4b456745cd1c4100b5e34d46ab063de3d

git init --quiet "$checkout"
cd "$checkout"
git fetch --quiet --depth=1 https://codeberg.org/anametologin/Krohnkite.git "$revision" ||
    git fetch --quiet --depth=1 https://github.com/XelorR/Krohnkite.git "$revision"
git checkout --quiet --detach "$revision"
npm ci --ignore-scripts --no-audit --no-fund
npm run tsc
make PROJECT_VER=0.9.11.0 PROJECT_REV=e345c9a pkg
cp -R pkg/. "$destination/"
