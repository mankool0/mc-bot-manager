#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CMAKE_FILE="$SCRIPT_DIR/manager/CMakeLists.txt"
GRADLE_FILE="$SCRIPT_DIR/client/gradle.properties"

usage() {
    echo "Usage: $0 [--patch|--minor|--major] [--tag] [--dry-run]"
    echo ""
    echo "  Bump type is inferred from the merge commit message if not specified:"
    echo "    fix/...       -> patch"
    echo "    add/feat/...  -> minor"
    echo "  --patch, --minor, --major override the inferred bump type"
    echo ""
    echo "  --tag      Create a git commit and tag after bumping"
    echo "  --dry-run  Show what would happen without making changes"
    exit 1
}

BUMP_TYPE=""
DO_TAG=false
DRY_RUN=false

for arg in "$@"; do
    case $arg in
        --major)   BUMP_TYPE="major" ;;
        --minor)   BUMP_TYPE="minor" ;;
        --patch)   BUMP_TYPE="patch" ;;
        --tag)     DO_TAG=true ;;
        --dry-run) DRY_RUN=true ;;
        --help|-h) usage ;;
        *) echo "Unknown argument: $arg"; usage ;;
    esac
done

# Infer bump type from merge commit message if not specified
if [ -z "$BUMP_TYPE" ]; then
    COMMIT_MSG=$(git -C "$SCRIPT_DIR" log -1 --pretty=%s)
    if echo "$COMMIT_MSG" | grep -iE '^(feat|add|feature)' > /dev/null; then
        BUMP_TYPE="minor"
    elif echo "$COMMIT_MSG" | grep -iE '^fix' > /dev/null; then
        BUMP_TYPE="patch"
    else
        echo "Cannot infer bump type from commit: $COMMIT_MSG"
        echo "Use --patch, --minor, or --major to specify."
        exit 1
    fi
fi

LAST_TAG=$(git -C "$SCRIPT_DIR" describe --tags --abbrev=0 --match="v*" 2>/dev/null || echo "")

bump_version() {
    local current="$1"
    local type="$2"
    local major minor patch
    major=$(echo "$current" | cut -d. -f1)
    minor=$(echo "$current" | cut -d. -f2)
    patch=$(echo "$current" | cut -d. -f3)

    case "$type" in
        major) major=$((major + 1)); minor=0; patch=0 ;;
        minor) minor=$((minor + 1)); patch=0 ;;
        patch) patch=$((patch + 1)) ;;
    esac
    echo "$major.$minor.$patch"
}

echo "Bump type  : $BUMP_TYPE"
[ -n "$LAST_TAG" ] && echo "Since tag  : $LAST_TAG" || echo "Since tag  : (none)"
echo ""

MANAGER_CURRENT=$(grep -oP 'project\(mc-bot-manager VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE")
MANAGER_NEW=$(bump_version "$MANAGER_CURRENT" "$BUMP_TYPE")
echo "Manager    : $MANAGER_CURRENT -> $MANAGER_NEW"

CLIENT_CURRENT=$(grep -oP '^mod_version=\K[0-9]+\.[0-9]+\.[0-9]+' "$GRADLE_FILE")
CLIENT_NEW=$(bump_version "$CLIENT_CURRENT" "$BUMP_TYPE")
echo "Client     : $CLIENT_CURRENT -> $CLIENT_NEW"

if $DRY_RUN; then
    echo ""
    echo "(dry run - no changes made)"
    exit 0
fi

sed -i "s/project(mc-bot-manager VERSION [0-9][0-9.]*/project(mc-bot-manager VERSION $MANAGER_NEW/" "$CMAKE_FILE"
sed -i "s/^mod_version=.*/mod_version=$CLIENT_NEW/" "$GRADLE_FILE"

if $DO_TAG; then
    git -C "$SCRIPT_DIR" add manager/CMakeLists.txt client/gradle.properties
    git -C "$SCRIPT_DIR" commit -m "bump version to $MANAGER_NEW"
    git -C "$SCRIPT_DIR" tag "v$MANAGER_NEW"
    echo ""
    echo "Tagged     : v$MANAGER_NEW"
fi
