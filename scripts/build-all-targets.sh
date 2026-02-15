#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

TARGETS=(
  "x86_64-linux-gnu"
  "aarch64-linux-gnu"
  "x86_64-apple-darwin"
  "aarch64-apple-darwin"
  "x86_64-linux-musl"
  "x86_64-w64-mingw32"
)

mkdir -p "${DIST_DIR}"

for target in "${TARGETS[@]}"; do
  echo "==> Building for ${target}"
  build_dir="${ROOT_DIR}/build-${target}"
  toolchain="${ROOT_DIR}/cmake/toolchains/${target}.cmake"

  cmake -S "${ROOT_DIR}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Dist \
    -DCMAKE_TOOLCHAIN_FILE="${toolchain}" \
    -DBUILD_STATIC=ON

  cmake --build "${build_dir}" --parallel

  artifact="ghostclaw-${target}"
  if [[ "${target}" == *"mingw32"* ]]; then
    cp "${build_dir}/ghostclaw.exe" "${DIST_DIR}/${artifact}.exe"
  else
    cp "${build_dir}/ghostclaw" "${DIST_DIR}/${artifact}"
  fi

done

echo "==> Build complete"
ls -lh "${DIST_DIR}"
