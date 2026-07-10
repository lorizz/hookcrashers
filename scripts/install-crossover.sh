#!/usr/bin/env bash
set -euo pipefail

REPO="${HOOKCRASHERS_REPO:-lorizz/hookcrashers}"
TAG="${HOOKCRASHERS_TAG:-v5.0.0-draft}"
ASSET="${HOOKCRASHERS_ASSET:-HookCrashers-v5-dropin.zip}"
TARGET_DIR="${HOOKCRASHERS_TARGET_DIR:-${HOME}/Library/Application Support/CrossOver/Bottles/Castle Crashers/drive_c/Program Files (x86)/Steam/steamapps/common/CastleCrashers}"

WORK_DIR="$(mktemp -d /tmp/hookcrashers-install.XXXXXX)"
cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

echo "HookCrashers installer"
echo "Repo:   $REPO"
echo "Tag:    $TAG"
echo "Target: $TARGET_DIR"

if ! command -v gh >/dev/null 2>&1; then
    echo "error: GitHub CLI 'gh' is required because the v5 draft is private." >&2
    exit 1
fi

if ! command -v unzip >/dev/null 2>&1; then
    echo "error: unzip is required." >&2
    exit 1
fi

if [[ ! -d "$TARGET_DIR" ]]; then
    echo "error: Castle Crashers target directory does not exist:" >&2
    echo "$TARGET_DIR" >&2
    exit 1
fi

mkdir -p "$WORK_DIR/download" "$WORK_DIR/extract"

echo "Downloading $ASSET..."
gh release download "$TAG" \
    --repo "$REPO" \
    --pattern "$ASSET" \
    --dir "$WORK_DIR/download" \
    --clobber

echo "Extracting..."
unzip -q "$WORK_DIR/download/$ASSET" -d "$WORK_DIR/extract"

for required in HookCrashers.asi dinput8.dll mods; do
    if [[ ! -e "$WORK_DIR/extract/$required" ]]; then
        echo "error: package is missing $required" >&2
        exit 1
    fi
done

echo "Installing HookCrashers.asi..."
cp "$WORK_DIR/extract/HookCrashers.asi" "$TARGET_DIR/HookCrashers.asi"

echo "Installing dinput8.dll..."
cp "$WORK_DIR/extract/dinput8.dll" "$TARGET_DIR/dinput8.dll"

echo "Installing mods..."
ditto "$WORK_DIR/extract/mods" "$TARGET_DIR/mods"
find "$TARGET_DIR/mods" -name .DS_Store -type f -delete

echo "Done."
echo "Log will be written by the game to: $TARGET_DIR/HookCrashers.log"
