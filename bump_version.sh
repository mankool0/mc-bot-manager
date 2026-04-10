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
    echo "  --manager, --client  Manually specify which components to bump (overrides auto-detection)"
    echo "  If neither is specified, components are inferred from files changed since the last version tag"
    echo ""
    echo "  --tag      Create a git commit and tag after bumping"
    echo "  --dry-run  Show what would happen without making changes"
    exit 1
}

BUMP_TYPE=""
DO_TAG=false
DRY_RUN=false
FORCE_MANAGER=false
FORCE_CLIENT=false

for arg in "$@"; do
    case $arg in
        --major)   BUMP_TYPE="major" ;;
        --minor)   BUMP_TYPE="minor" ;;
        --patch)   BUMP_TYPE="patch" ;;
        --manager) FORCE_MANAGER=true ;;
        --client)  FORCE_CLIENT=true ;;
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

# Find files changed since last version tag
LAST_TAG=$(git -C "$SCRIPT_DIR" describe --tags --abbrev=0 --match="v*" 2>/dev/null || echo "")
if [ -n "$LAST_TAG" ]; then
    CHANGED_FILES=$(git -C "$SCRIPT_DIR" diff --name-only "$LAST_TAG"..HEAD)
else
    CHANGED_FILES=$(git -C "$SCRIPT_DIR" diff --name-only HEAD~1..HEAD 2>/dev/null || echo "")
fi

BUMP_MANAGER=false
BUMP_CLIENT=false
if $FORCE_MANAGER || $FORCE_CLIENT; then
    $FORCE_MANAGER && BUMP_MANAGER=true
    $FORCE_CLIENT  && BUMP_CLIENT=true
else
    for f in $CHANGED_FILES; do
        case "$f" in
            manager/*) BUMP_MANAGER=true ;;
            client/*)  BUMP_CLIENT=true ;;
        esac
    done
fi

if ! $BUMP_MANAGER && ! $BUMP_CLIENT; then
    echo "No version-relevant files changed (manager/, client/)."
    echo "Files changed: $(echo "$CHANGED_FILES" | tr '\n' ' ')"
    exit 0
fi

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

CHANGED_VERSIONED_FILES=()

if $BUMP_MANAGER; then
    CURRENT=$(grep -oP 'project\(mc-bot-manager VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE")
    NEW=$(bump_version "$CURRENT" "$BUMP_TYPE")
    echo "Manager    : $CURRENT -> $NEW"
    if ! $DRY_RUN; then
        sed -i "s/project(mc-bot-manager VERSION [0-9][0-9.]*/project(mc-bot-manager VERSION $NEW/" "$CMAKE_FILE"
        CHANGED_VERSIONED_FILES+=("manager/CMakeLists.txt")
    fi
    MANAGER_NEW="$NEW"
fi

if $BUMP_CLIENT; then
    CURRENT=$(grep -oP '^mod_version=\K[0-9]+\.[0-9]+\.[0-9]+' "$GRADLE_FILE")
    NEW=$(bump_version "$CURRENT" "$BUMP_TYPE")
    echo "Client     : $CURRENT -> $NEW"
    if ! $DRY_RUN; then
        sed -i "s/^mod_version=.*/mod_version=$NEW/" "$GRADLE_FILE"
        CHANGED_VERSIONED_FILES+=("client/gradle.properties")
    fi
    CLIENT_NEW="$NEW"
fi

if $DRY_RUN; then
    echo ""
    echo "(dry run - no changes made)"
    exit 0
fi

if $DO_TAG; then
    git -C "$SCRIPT_DIR" add "${CHANGED_VERSIONED_FILES[@]}"
    TAG_VERSION="${MANAGER_NEW:-$CLIENT_NEW}"
    git -C "$SCRIPT_DIR" commit -m "bump version to $TAG_VERSION"
    git -C "$SCRIPT_DIR" tag "v$TAG_VERSION"
    echo ""
    echo "Tagged     : v$TAG_VERSION"
fi
