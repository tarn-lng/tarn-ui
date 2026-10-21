#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
revision=da3173c4f012ead629aa03e67d3d9147615fec62
destination="${TARN_SOURCE:-$root/../tarn}"
[[ -d "$destination" ]] || { echo "Missing Tarn repository: $destination" >&2; exit 1; }
[[ $(git -C "$destination" rev-parse HEAD) == "$revision" ]]
[[ -z $(git -C "$destination" status --porcelain --untracked-files=all) ]]
[[ $(git -C "$destination" rev-parse --is-shallow-repository) == false ]]
[[ -z $(git -C "$destination" config --get core.sparseCheckout || true) ]]
[[ -z $(git -C "$destination" config --get-regexp 'remote\..*\.promisor|extensions\.partialclone' || true) ]]
git -C "$destination" fsck --full --no-dangling
# No submodules exist at this revision. Refuse if this invariant changes.
[[ ! -e "$destination/.gitmodules" ]]
printf 'Verified complete clean checkout: %s\n' "$revision"
