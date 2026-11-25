#!/bin/bash
# Run on fresh native runners without installing development packages.
set -euo pipefail
shopt -s nullglob
image=$1
scratch=$(mktemp -d)
mount="$scratch/volume"
applications="$scratch/User Applications"
installed="$applications/Crumb Writer.app"
manuscripts="$scratch/Manuscrits d’Alfredo"
app_pid=
clean_up() {
    if [[ -n "$app_pid" ]]; then kill "$app_pid" 2>/dev/null || true; fi
    hdiutil detach "$mount" -quiet 2>/dev/null || true
    rm -rf "$scratch"
}
trap clean_up EXIT
mkdir "$mount" "$applications" "$manuscripts"
hdiutil attach -nobrowse -readonly -mountpoint "$mount" "$image"
test "$(readlink "$mount/Applications")" = /Applications
cmp LICENSE "$mount/LICENSE"
ditto "$mount/Crumb Writer.app" "$installed"
cmp data/bsd-first-LICENSE "$installed/Contents/Resources/third-party/BSD-first/LICENSE"
unset DICPATH CRUMBWRITER_THESAURUS_DIR QT_PLUGIN_PATH QT_QPA_PLATFORM DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
python3 packaging/macos/bundle.py verify "$installed" arm64 x86_64
executable="$installed/Contents/MacOS/Crumb Writer"
"$executable" --version
"$executable" --smoke-test
xcrun swiftc "$(dirname "$0")/test-window.swift" -o "$scratch/test-window"

wait_for_open() {
    local file=$1 backup attempt
    for ((attempt=0; attempt<100; ++attempt)); do
        for backup in "$file".*.bak; do cmp "$file" "$backup"; return; done
        sleep 0.2
    done
    echo "Missing backup for $file" >&2
    return 1
}
native_frame() {
    local frame attempt
    for ((attempt=0; attempt<50; ++attempt)); do
        if frame=$("$scratch/test-window" frame "$app_pid" 2>"$scratch/window.log"); then
            printf '%s\n' "$frame"
            return
        fi
        sleep 0.2
    done
    cat "$scratch/window.log" >&2
    return 1
}

# LaunchServices delivers the same multi-file open event as Finder. Each
# manuscript must open, and WindowServer must see just one document window.
manuscript="$manuscripts/Lluna – capítol 1.txt"
printf '%s\n' 'La lluna' 'A goose audits the moon for bumps.' > "$manuscript"
printf '%s\n' '{"version":2,"bookmarks":[]}' > "$manuscript.bookmarks"
printf '%s\n' 'Goose, Inspector' > "$manuscript.characters"
printf '%s\n' 'Moon, Luna' > "$manuscript.recurring"
printf '%s\n' 'Cranolo' > "$manuscript.dictionary"
printf '%s\n' 'literal-guillemets=true' > "$manuscript.config"
notes="$manuscripts/Reading notes.txt"
printf '%s\n' 'A separate plain-text manuscript.' > "$notes"
open -n -a "$installed" "$manuscript" "$notes"
wait_for_open "$manuscript"
wait_for_open "$notes"
app_pid=$(pgrep -x 'Crumb Writer')
initial_frame=$(native_frame)

# LaunchServices delivers another file-open event to the running instance.
second="$manuscripts/Segundo capítulo.txt"
printf '%s\n' 'Otro capítulo' > "$second"
open -a "$installed" "$second"
wait_for_open "$second"
test "$(native_frame)" = "$initial_frame"
"$executable" -- "$second" "$manuscript"
test "$(native_frame)" = "$initial_frame"
"$scratch/test-window" quit "$app_pid"
for ((attempt=0; attempt<50; ++attempt)); do
    kill -0 "$app_pid" 2>/dev/null || break
    sleep 0.2
done
if kill -0 "$app_pid" 2>/dev/null; then exit 1; fi
app_pid=

prefs="$HOME/Library/Preferences/crumbwriter/settings.ini"
test -s "$prefs"
shasum -a 256 "$prefs" "$manuscripts/"* > "$scratch/expected.sha256"
rm -rf "$installed"
ditto "$mount/Crumb Writer.app" "$installed"
"$executable" --smoke-test
shasum -a 256 -c "$scratch/expected.sha256"
rm -rf "$installed"
test ! -e "$installed"
shasum -a 256 -c "$scratch/expected.sha256"
