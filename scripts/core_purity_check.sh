#!/usr/bin/env bash
# Core-purity grep for CI. The core must be domain-neutral and format-agnostic:
# no XML / pugixml coupling and no host vocabulary leaking into it. Parser
# dependencies live only inside their own per-target modules (e.g. lib/xml/),
# never in core. The boundary is now STRUCTURAL -- the source/format adapters live
# in separate include roots and targets (lib/xml, lib/runtime) that core cannot
# reach -- and this gate additionally lint-asserts it: a core file that includes a
# per-module adapter header (nucleus/xml/, nucleus/env/, nucleus/argv/,
# nucleus/runtime/), or names a format, exits nonzero.
#
# A CMake-script equivalent (scripts/core_purity_check.cmake) is wired as a
# CTest gate; this shell wrapper is for CI pipelines that grep directly.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Forbidden vocabulary: any format NAME (xml) and the parser DEPENDENCY
# (pugi/pugixml), plus the structural adapter-include assertion. The core is
# format-agnostic -- it must not name a format at all; per-format emitters live in
# their own quarantined modules. Kept in lockstep with the CMake gate
# (scripts/core_purity_check.cmake).
pattern='xml|pugi|#include[[:space:]]*[<"]nucleus/(env|argv|runtime)/'

# Host-application tokens are not committed here; they are appended from an
# uncommitted, gitignored local file (one token per line) if it is present. A
# missing file leaves the format-only default, so a fresh clone and the local
# ctest gate pass unmodified -- the read is append-if-present, never require.
local_tokens_file="$root/scripts/purity_tokens.local"
if [[ -f "$local_tokens_file" ]]; then
    while IFS= read -r token || [[ -n "$token" ]]; do
        [[ -z "$token" ]] && continue
        pattern="$pattern|$token"
    done < "$local_tokens_file"
fi

# The quarantined module that is ALLOWED to mention parser/host vocabulary
# because it IS the wrapper: the xml format module (lib/xml/). It lives outside
# lib/core, so it is not even scanned here; this exclusion is belt-and-suspenders.
quarantine='lib/xml/'

hits="$(grep -rinE "$pattern" \
        "$root/lib/core/include/nucleus" "$root/lib/core/src/nucleus" 2>/dev/null \
        | grep -v "$quarantine" || true)"

if [[ -n "$hits" ]]; then
    echo "Core-purity violations (forbidden format/host vocabulary in core):"
    echo "$hits"
    exit 1
fi

echo "core-purity check: clean (0 forbidden-symbol hits in core)"
