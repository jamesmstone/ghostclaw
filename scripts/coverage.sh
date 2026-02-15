#!/bin/bash
# Generate code coverage report for GhostClaw
# Usage: ./scripts/coverage.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-coverage"
COVERAGE_DIR="$PROJECT_ROOT/coverage"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Check for required tools
if ! command -v lcov &> /dev/null; then
    echo "Error: lcov is required but not installed."
    echo "Install with: brew install lcov (macOS) or apt-get install lcov (Linux)"
    exit 1
fi

# Clean previous coverage data
log_info "Cleaning previous coverage data..."
rm -rf "$BUILD_DIR" "$COVERAGE_DIR"
mkdir -p "$COVERAGE_DIR"

# Configure with coverage enabled
log_info "Configuring build with coverage..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DGHOSTCLAW_ENABLE_COVERAGE=ON \
    -DGHOSTCLAW_ENABLE_LTO=OFF

# Build
log_info "Building..."
cmake --build "$BUILD_DIR" -j

# Run tests
log_info "Running tests..."
cd "$BUILD_DIR"
ctest --output-on-failure || true

# Generate coverage report
log_info "Generating coverage report..."
cd "$PROJECT_ROOT"

# Capture coverage data
lcov --capture \
    --directory "$BUILD_DIR" \
    --output-file "$COVERAGE_DIR/coverage.info" \
    --ignore-errors mismatch,gcov

# Remove external code from coverage
lcov --remove "$COVERAGE_DIR/coverage.info" \
    '/usr/*' \
    '*/tests/*' \
    '*/benches/*' \
    '*/references/*' \
    '*/_deps/*' \
    --output-file "$COVERAGE_DIR/coverage.info" \
    --ignore-errors unused

# Generate HTML report
log_info "Generating HTML report..."
genhtml "$COVERAGE_DIR/coverage.info" \
    --output-directory "$COVERAGE_DIR/html" \
    --title "GhostClaw Code Coverage" \
    --legend \
    --show-details

# Print summary
log_info "Coverage summary:"
lcov --list "$COVERAGE_DIR/coverage.info"

log_success "Coverage report generated at: $COVERAGE_DIR/html/index.html"
echo ""
echo "To view the report, open:"
echo "  open $COVERAGE_DIR/html/index.html  (macOS)"
echo "  xdg-open $COVERAGE_DIR/html/index.html  (Linux)"
