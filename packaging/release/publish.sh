#!/usr/bin/env bash
# All writers to GitHub run after the native test jobs return successfully.
set -euo pipefail
cd dist
cp -- "$BUNDLE" CrumbWriter-build.sigstore.json
installers=(CrumbWriter-Ubuntu-24.04-amd64.deb CrumbWriter-FreeBSD-15-amd64.pkg
    CrumbWriter-Windows-x64-Setup.exe CrumbWriter-macOS-Universal.dmg)
for asset in "${installers[@]}" SHA256SUMS; do
    gh attestation verify "$asset" --repo "$GH_REPO" --bundle CrumbWriter-build.sigstore.json
done
sha256sum --check SHA256SUMS
commit=$(git rev-parse HEAD)
release_info=$(gh api --paginate "repos/$GH_REPO/releases?per_page=100" |
    jq -cs --arg tag "$TAG" '[.[][] | select(.tag_name == $tag)][0] // empty')
if [[ -z $release_info ]]; then
    notes=$(mktemp)
    sed -e "s|@REPOSITORY@|$GH_REPO|g" -e "s|@TAG@|$TAG|g" ../packaging/release/notes.md > "$notes"
    gh release create "$TAG" --draft --target "$commit" --title "Crumb Writer ${TAG#v}" --notes-file "$notes"
    release_info=$(gh release view "$TAG" --json databaseId,isDraft,targetCommitish |
        jq '{id:.databaseId, draft:.isDraft, target_commitish:.targetCommitish}')
fi
if [[ $(jq -r '.draft' <<< "$release_info") != true ]]; then
    echo "::error::$TAG has already been published. A new release requires a new version."
    exit 1
fi
[[ $(jq -r '.target_commitish' <<< "$release_info") = "$commit" ]]
release_id=$(jq -r '.id' <<< "$release_info")
# A retry can replace only generated assets in an unpublished draft for this commit.
gh release upload "$TAG" ./* --clobber
remote=$(gh api --paginate "repos/$GH_REPO/releases/$release_id/assets?per_page=100" |
    jq -scr '.[][] | [.name,.digest] | @tsv' | sort)
local_assets=$(for file in *; do
    digest=$(sha256sum "$file" | cut -d ' ' -f 1)
    printf '%s\tsha256:%s\n' "$file" "$digest"
done | sort)
if [[ $remote != "$local_assets" ]]; then
    echo "::error::Draft assets differ from the verified local files."
    exit 1
fi
gh release edit "$TAG" --draft=false --latest
# Immutable releases have a second, GitHub-generated release attestation.
# It can take a moment to appear after the draft becomes public.
verified=false
for delay in 2 4 8 16 30 30; do
    if gh release verify "$TAG"; then verified=true; break; fi
    sleep "$delay"
done
if [[ $verified != true ]]; then
    echo "::error::The release was published but GitHub could not verify its immutable release record."
    exit 1
fi
for asset in "${installers[@]}"; do gh release verify-asset "$TAG" "$asset"; done
gh release view "$TAG" --json url,isDraft,isImmutable
