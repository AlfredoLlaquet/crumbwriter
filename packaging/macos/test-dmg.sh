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
unset DICPATH CRUMBWRITER_THESAURUS_DIR QT_PLUGIN_PATH QT_QPA_PLATFORM DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
python3 packaging/macos/bundle.py verify "$installed" arm64 x86_64
executable="$installed/Contents/MacOS/Crumb Writer"
"$executable" --version
"$executable" --smoke-test

# Send an actual Finder-style document-open event. Its byte-exact automatic
# backup proves that the editor received and opened the file with its Unicode path.
manuscript="$manuscripts/Lluna – capítol 1.txt"
printf '%s\n' 'La lluna' 'A goose audits the moon for bumps.' > "$manuscript"
printf '%s\n' '{"version":2,"bookmarks":[]}' > "$manuscript.bookmarks"
printf '%s\n' 'Goose, Inspector' > "$manuscript.characters"
printf '%s\n' 'Moon, Luna' > "$manuscript.recurring"
printf '%s\n' 'Cranolo' > "$manuscript.dictionary"
printf '%s\n' 'literal-guillemets=true' > "$manuscript.config"
open -n -a "$installed" "$manuscript"
for ((attempt=0; attempt<100; ++attempt)); do
    backups=("$manuscript".*.bak)
    [[ ${#backups[@]} -gt 0 ]] && break
    sleep 0.2
done
test "${#backups[@]}" -gt 0
cmp "$manuscript" "${backups[0]}"

# LaunchServices delivers another file-open event to the running instance.
second="$manuscripts/Segundo capítulo.txt"
printf '%s\n' 'Otro capítulo' > "$second"
open -a "$installed" "$second"
for ((attempt=0; attempt<100; ++attempt)); do
    more=("$second".*.bak)
    [[ ${#more[@]} -gt 0 ]] && break
    sleep 0.2
done
test "${#more[@]}" -gt 0
cmp "$second" "${more[0]}"
app_pid=$(pgrep -x 'Crumb Writer')
kill "$app_pid"
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
