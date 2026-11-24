#!/usr/bin/env bash
# One-time migration for installs older than 5.2.0.
#
# HookCrashers 5.1.2 and earlier read their update manifests from the retired
# lorizz/hookcrashers-releases repository, and 5.1.2 only installs assets
# downloaded from that repository. This script mirrors one stable release of
# lorizz/hookcrashers (the first one that reads from this repository) into the
# legacy repository and points both legacy channel manifests at it. Old installs
# update to that release; from then on they read lorizz/hookcrashers directly.
#
# After running it, archive (do not delete) lorizz/hookcrashers-releases so the
# manifests keep being served to players who launch an old build later.
#
# usage: scripts/publish-legacy-bridge.sh release_5.2.0
set -euo pipefail

TAG="${1:?usage: $0 <stable tag of lorizz/hookcrashers, e.g. release_5.2.0>}"
SOURCE_REPO="lorizz/hookcrashers"
LEGACY_REPO="lorizz/hookcrashers-releases"
ASSETS=(HookCrashers.asi dinput8.dll HookCrashers-v5-dropin.zip HookCrashers-SDK-Win32.zip)

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

prerelease="$(gh release view "$TAG" --repo "$SOURCE_REPO" --json isPrerelease --jq .isPrerelease)"
if [[ "$prerelease" != "false" ]]; then
    echo "error: $TAG is a prerelease; bridge only a stable release" >&2
    exit 1
fi

echo "Downloading $TAG from $SOURCE_REPO..."
for asset in "${ASSETS[@]}"; do
    gh release download "$TAG" --repo "$SOURCE_REPO" --pattern "$asset" --dir "$WORK/assets" --clobber
done
gh release download "$TAG" --repo "$SOURCE_REPO" --pattern stable.json --dir "$WORK/source"

version="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$WORK/source/stable.json")"
expected="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["asset_sha256"])' "$WORK/source/stable.json")"
actual="$(shasum -a 256 "$WORK/assets/HookCrashers.asi" | cut -d' ' -f1)"
if [[ "$expected" != "$actual" ]]; then
    echo "error: HookCrashers.asi does not match the sha256 in stable.json" >&2
    exit 1
fi

echo "Mirroring $TAG ($version) into $LEGACY_REPO..."
gh release delete "$TAG" --repo "$LEGACY_REPO" --yes --cleanup-tag >/dev/null 2>&1 || true
gh release create "$TAG" "$WORK"/assets/* \
    --repo "$LEGACY_REPO" \
    --title "HookCrashers $version (migration)" \
    --notes "HookCrashers moved to https://github.com/$SOURCE_REPO/releases. This copy only lets builds older than 5.2.0 update; later updates come from $SOURCE_REPO." \
    --latest

legacy_base="https://github.com/$LEGACY_REPO/releases/download/$TAG"
gh repo clone "$LEGACY_REPO" "$WORK/legacy" -- --quiet
for channel in release dev; do
    manifest="$([[ $channel == release ]] && echo stable.json || echo testing.json)"
    cat > "$WORK/legacy/$manifest" <<EOF
{
  "name": "HookCrashers",
  "channel": "$channel",
  "version": "$version",
  "asset_url": "$legacy_base/HookCrashers.asi",
  "asset_sha256": "$actual",
  "dropin_url": "$legacy_base/HookCrashers-v5-dropin.zip",
  "sdk_url": "$legacy_base/HookCrashers-SDK-Win32.zip",
  "notes": "HookCrashers moved to https://github.com/$SOURCE_REPO. Update to $version to keep receiving updates."
}
EOF
done
cat > "$WORK/legacy/README.md" <<EOF
# HookCrashers releases (retired)

HookCrashers is now released from https://github.com/$SOURCE_REPO/releases.

This repository only serves the last migration manifests so that installs older
than 5.2.0 can update themselves to $version.
EOF
(
    cd "$WORK/legacy"
    git add stable.json testing.json README.md
    git commit -q -m "Point legacy channels at $version (migration to $SOURCE_REPO)"
    git push -q
)

echo "Done. Next: archive $LEGACY_REPO in its GitHub settings (keep it public; do not delete it)."
