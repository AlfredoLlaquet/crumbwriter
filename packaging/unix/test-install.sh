#!/bin/sh
# Run as root on a fresh target with Xvfb and a font installed. Dependencies
# of the editor itself must come from the two downloaded package files. An
# optional second argument selects an existing display for the GUI checks.
set -eu
packages=$(cd "$1" && pwd)
external_display=${2:-}
work=$(mktemp -d)
editor_pid= server_pid=
cleanup() {
    [ -z "$editor_pid" ] || kill "$editor_pid" 2>/dev/null || :
    [ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT

case $(uname -s) in
    Linux)
        set -- "$packages"/crumbwriter_*_amd64.deb
        test "$#" = 1
        test -f "$1"
        release=$1
        fixture="$packages/UpgradeFixture.deb"
        prefix=/usr
        expected=$(dpkg-deb -f "$release" Version)
        product_version=${expected%-*}
        distro=$(. /etc/os-release && printf '%s%s' "$ID" "$VERSION_ID")
        test "$expected" = "$product_version-$distro"
        test "$(basename "$release")" = "crumbwriter_${expected}_amd64.deb"
        install_package() { apt-get install -y --no-install-recommends "$1"; }
        installed_version() { dpkg-query -W -f='${Version}' crumbwriter; }
        remove_package() { apt-get purge -y crumbwriter; }
        verify_docs() {
            # Minimal distribution images can omit optional /usr/share/doc files.
            # Check their presence in the download and the installed standard copyright file.
            dpkg-deb --fsys-tarfile "$release" | tar -tf - > "$work/contents"
            grep -Fx './usr/share/doc/crumbwriter/CrumbWriter.txt' "$work/contents"
            grep -Fx './usr/share/doc/crumbwriter/COPYING-MyThes' "$work/contents"
            grep -Fx './usr/share/doc/crumbwriter/COPYING-BSD-first' "$work/contents"
            test -s /usr/share/doc/crumbwriter/copyright
        }
        export LC_ALL=C.UTF-8
        ;;
    FreeBSD)
        major=$(freebsd-version -u | cut -d . -f1)
        set -- "$packages"/crumbwriter-*-FreeBSD-"$major"-amd64.pkg
        test "$#" = 1
        test -f "$1"
        release=$1
        fixture="$packages/UpgradeFixture.pkg"
        prefix=/usr/local
        expected=$(pkg query -F "$release" '%v')
        product_version=$expected
        test "$(basename "$release")" = "crumbwriter-$expected-FreeBSD-$major-amd64.pkg"
        install_package() { pkg install -y "$1"; }
        installed_version() { pkg query '%v' crumbwriter; }
        remove_package() { pkg delete -y crumbwriter; }
        verify_docs() {
            test -s "$prefix/share/doc/crumbwriter/CrumbWriter.txt"
            test -s "$prefix/share/doc/crumbwriter/COPYING-MyThes"
            test -s "$prefix/share/doc/crumbwriter/COPYING-BSD-first"
        }
        export LC_ALL=en_US.UTF-8
        ;;
    *) exit 1 ;;
esac
test ! -e "$prefix/bin/crumbwriter"
export HOME="$work/home" XDG_CONFIG_HOME="$work/home/config" XDG_RUNTIME_DIR="$work/runtime"
mkdir -p "$HOME/Manuscripts" "$XDG_CONFIG_HOME/crumbwriter" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cd "$HOME/Manuscripts"
name='Lluna – capítol 1.txt'
printf '%s\n' 'Lluna//Crumb 1: The Inspector: A goose audits the moon for bumps.' > "$name"
printf '%s\n' '{"version":2,"bookmarks":[]}' > "$name.bookmarks"
printf '%s\n' 'Núria, the inspector' > "$name.characters"
printf '%s\n' 'Moon, Luna' > "$name.recurring"
printf '%s\n' 'Lluna' > "$name.dictionary"
printf '%s\n' 'literal-guillemets=true' > "$name.config"

install_package "$fixture"
test "$(installed_version)" = 0.0.1
test -f "$prefix/share/crumbwriter/upgrade-fixture"
if [ -n "$external_display" ]; then
    DISPLAY=$external_display
else
    Xvfb -screen 0 1280x800x24 -nolisten tcp -displayfd 1 > "$work/display" &
    server_pid=$!
    attempt=0
    while [ ! -s "$work/display" ]; do
        test "$attempt" -lt 60
        kill -0 "$server_pid"
        attempt=$((attempt + 1))
        sleep 1
    done
    DISPLAY=":$(cat "$work/display")"
fi
export DISPLAY QT_QPA_PLATFORM=xcb
crumbwriter -- "$PWD/$name" &
editor_pid=$!
attempt=0
while :; do
    set -- "$name".*.bak
    [ ! -f "$1" ] || break
    test "$attempt" -lt 60
    kill -0 "$editor_pid"
    attempt=$((attempt + 1))
    sleep 1
done
cmp "$name" "$1"
kill "$editor_pid"
wait "$editor_pid" || :
editor_pid=
# Preserve writer-owned material, including the actual saved preferences.
settings="$XDG_CONFIG_HOME/crumbwriter/settings.ini"
test -s "$settings"
mkdir "$work/before"
cp ./* "$work/before/"
cp "$settings" "$work/settings"

install_package "$release"
test "$(installed_version)" = "$expected"
test "$(crumbwriter --version)" = "Crumb Writer $product_version"
test ! -e "$prefix/share/crumbwriter/upgrade-fixture"
desktop="$prefix/share/applications/org.thecrumbs.CrumbWriter.desktop"
grep -Fx 'Exec=crumbwriter %F' "$desktop"
test -s "$prefix/share/icons/hicolor/256x256/apps/org.thecrumbs.CrumbWriter.png"
verify_docs
crumbwriter --smoke-test
for file in "$work/before/"*; do cmp "$file" "$(basename "$file")"; done
cmp "$settings" "$work/settings"

remove_package
test ! -e "$prefix/bin/crumbwriter"
test ! -e "$desktop"
for file in "$work/before/"*; do cmp "$file" "$(basename "$file")"; done
cmp "$settings" "$work/settings"
