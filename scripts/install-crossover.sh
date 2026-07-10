#!/usr/bin/env bash
set -euo pipefail

REPO="${HOOKCRASHERS_REPO:-lorizz/hookcrashers}"
TAG="${HOOKCRASHERS_TAG:-v5.0.0-draft}"
ASSET="${HOOKCRASHERS_ASSET:-HookCrashers-v5-dropin.zip}"
PDB_ASSET="${HOOKCRASHERS_PDB_ASSET:-HookCrashers.pdb}"
TARGET_DIR="${HOOKCRASHERS_TARGET_DIR:-${HOME}/Library/Application Support/CrossOver/Bottles/Castle Crashers/drive_c/Program Files (x86)/Steam/steamapps/common/CastleCrashers}"
BOTTLE_DIR="${HOOKCRASHERS_BOTTLE_DIR:-${HOME}/Library/Application Support/CrossOver/Bottles/Castle Crashers}"

WORK_DIR="$(mktemp -d /tmp/hookcrashers-install.XXXXXX)"
cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

echo "HookCrashers installer"
echo "Repo:   $REPO"
echo "Tag:    $TAG"
echo "Target: $TARGET_DIR"
echo "Bottle: $BOTTLE_DIR"

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

USER_REG="$BOTTLE_DIR/user.reg"
if [[ ! -f "$USER_REG" ]]; then
    echo "error: CrossOver bottle registry was not found:" >&2
    echo "$USER_REG" >&2
    exit 1
fi

mkdir -p "$WORK_DIR/download" "$WORK_DIR/extract"

echo "Downloading $ASSET..."
gh release download "$TAG" \
    --repo "$REPO" \
    --pattern "$ASSET" \
    --dir "$WORK_DIR/download" \
    --clobber

echo "Downloading $PDB_ASSET..."
if ! gh release download "$TAG" \
    --repo "$REPO" \
    --pattern "$PDB_ASSET" \
    --dir "$WORK_DIR/download" \
    --clobber; then
    echo "warning: $PDB_ASSET was not downloaded; crash reports will have fewer symbols." >&2
fi

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

if [[ -f "$WORK_DIR/download/$PDB_ASSET" ]]; then
    echo "Installing HookCrashers.pdb..."
    cp "$WORK_DIR/download/$PDB_ASSET" "$TARGET_DIR/HookCrashers.pdb"
fi

echo "Installing mods..."
ditto "$WORK_DIR/extract/mods" "$TARGET_DIR/mods"
find "$TARGET_DIR/mods" -name .DS_Store -type f -delete

echo "Ensuring Wine loads local dinput8.dll..."
/usr/bin/ruby -e 'path=ARGV[0]; text=File.read(path); section="[Software\\\\Wine\\\\DllOverrides]"; line="\"dinput8\"=\"native,builtin\""; if text.include?(line); exit 0; end; index=text.index(section); abort "DllOverrides section not found" unless index; insert_at=text.index("\n", index); abort "DllOverrides section header malformed" unless insert_at; text.insert(insert_at + 1, line + "\n"); File.write(path, text)' "$USER_REG"
/usr/bin/ruby -e 'path=ARGV[0]; text=File.read(path); section="[Software\\\\Wine\\\\AppDefaults\\\\castle.exe\\\\DllOverrides]"; line="\"dinput8\"=\"native,builtin\""; if text.include?(section); start=text.index(section); following=text[start..]; block=following.lines.take_while.with_index { |value, index| index == 0 || !value.start_with?("[") }; unless block.any? { |value| value.strip == line }; insert_at=text.index("\n", start); text.insert(insert_at + 1, line + "\n"); end; else; marker="[Software\\\\Wine\\\\DllOverrides]"; index=text.index(marker) || text.length; text.insert(index, "#{section} 1783711442\n#time=1dd10a1aa95e999\n#{line}\n\n"); end; File.write(path, text)' "$USER_REG"

echo "Done."
echo "Log will be written by the game to: $TARGET_DIR/HookCrashers.log"
