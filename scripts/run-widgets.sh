#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export LD_LIBRARY_PATH="$root/.build/widgets"
exec "$root/.build/widgets/widgets"
