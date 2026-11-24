#!/usr/bin/env bash
set -euo pipefail

REPO="${HOOKCRASHERS_REPO:-lorizz/hookcrashers}"
# Empty = latest stable release; "dev" = current development build.
TAG="${HOOKCRASHERS_TAG:-}"
ASSET="${HOOKCRASHERS_ASSET:-HookCrashers-v5-dropin.zip}"
TARGET_DIR="${HOOKCRASHERS_TARGET_DIR:-${HOME}/Library/Application Support/CrossOver/Bottles/Castle Crashers/drive_c/Program Files (x86)/Steam/steamapps/common/CastleCrashers}"
BOTTLE_DIR="${HOOKCRASHERS_BOTTLE_DIR:-${HOME}/Library/Application Support/CrossOver/Bottles/Castle Crashers}"
RESET_SAVE="${HOOKCRASHERS_RESET_SAVE:-0}"

WORK_DIR="$(mktemp -d /tmp/hookcrashers-install.XXXXXX)"
cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

echo "HookCrashers installer"
echo "Repo:   $REPO"
echo "Tag:    ${TAG:-latest stable}"
echo "Target: $TARGET_DIR"
echo "Bottle: $BOTTLE_DIR"
echo "Reset save: $RESET_SAVE"

if ! command -v gh >/dev/null 2>&1; then
    echo "error: GitHub CLI 'gh' is required (brew install gh)." >&2
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

if [[ -z "$TAG" ]]; then
    TAG="$(gh release view --repo "$REPO" --json tagName --jq .tagName)"
    echo "Latest stable release: $TAG"
fi

echo "Downloading $ASSET ($TAG)..."
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

# HookCrashers.yaml and mods/ are created by HookCrashers on first launch.
echo "Installing mods..."
ditto "$WORK_DIR/extract/mods" "$TARGET_DIR/mods"
find "$TARGET_DIR/mods" -name .DS_Store -type f -delete

if [[ "$RESET_SAVE" == "1" ]]; then
    echo "Backing up and resetting Castle Crashers saves..."
    BACKUP_DIR="$TARGET_DIR/save_backups/hc5_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$BACKUP_DIR"
    SAVE_FOUND=0
    while IFS= read -r save_file; do
        SAVE_FOUND=1
        echo "  moving $(basename "$save_file")"
        mv "$save_file" "$BACKUP_DIR/"
    done < <(find "$BOTTLE_DIR/drive_c/Program Files (x86)/Steam/userdata" -path '*/204360/remote/cc_save*.dat*' -type f 2>/dev/null)
    if [[ "$SAVE_FOUND" == "0" ]]; then
        rmdir "$BACKUP_DIR" 2>/dev/null || true
        echo "  no cc_save*.dat files found"
    else
        echo "  backup: $BACKUP_DIR"
    fi
fi

echo "Ensuring Wine loads local dinput8.dll..."
/usr/bin/ruby -e 'path=ARGV[0]; text=File.read(path); section="[Software\\\\Wine\\\\DllOverrides]"; line="\"dinput8\"=\"native,builtin\""; if text.include?(line); exit 0; end; index=text.index(section); abort "DllOverrides section not found" unless index; insert_at=text.index("\n", index); abort "DllOverrides section header malformed" unless insert_at; text.insert(insert_at + 1, line + "\n"); File.write(path, text)' "$USER_REG"
/usr/bin/ruby -e 'path=ARGV[0]; text=File.read(path); section="[Software\\\\Wine\\\\AppDefaults\\\\castle.exe\\\\DllOverrides]"; line="\"dinput8\"=\"native,builtin\""; if text.include?(section); start=text.index(section); following=text[start..]; block=following.lines.take_while.with_index { |value, index| index == 0 || !value.start_with?("[") }; unless block.any? { |value| value.strip == line }; insert_at=text.index("\n", start); text.insert(insert_at + 1, line + "\n"); end; else; marker="[Software\\\\Wine\\\\DllOverrides]"; index=text.index(marker) || text.length; text.insert(index, "#{section} 1783711442\n#time=1dd10a1aa95e999\n#{line}\n\n"); end; File.write(path, text)' "$USER_REG"

echo "Done."
echo "Log will be written by the game to: $TARGET_DIR/HookCrashers.log"
