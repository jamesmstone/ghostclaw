#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <new-version>"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NEW_VERSION="$1"

sed -i.bak -E "s/project\(ghostclaw VERSION [0-9]+\.[0-9]+\.[0-9]+/project(ghostclaw VERSION ${NEW_VERSION}/" "${ROOT_DIR}/CMakeLists.txt"
rm -f "${ROOT_DIR}/CMakeLists.txt.bak"

echo "Updated CMakeLists.txt version to ${NEW_VERSION}"
