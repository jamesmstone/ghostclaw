#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

if [[ ! -d "${DIST_DIR}" ]]; then
  echo "dist directory does not exist"
  exit 1
fi

cd "${DIST_DIR}"
shasum -a 256 ghostclaw-* > SHA256SUMS
cat SHA256SUMS
