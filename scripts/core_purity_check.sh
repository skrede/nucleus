#!/usr/bin/env bash
# Core-purity grep for CI. The core must be domain-neutral and format-agnostic:
# no XML / pugixml coupling and no host vocabulary leaking into it. Parser
# dependencies live only inside their own per-target modules (e.g. lib/xml/),
# never in core. The boundary is now STRUCTURAL -- the source/format adapters live
# in separate include roots and targets (lib/xml, lib/runtime) that core cannot
# reach -- and this gate additionally lint-asserts it: a core file that includes a
# nucleus/sources/ adapter header, or names a format, exits nonzero.
#
# A CMake-script equivalent (scripts/core_purity_check.cmake) is wired as a
# CTest gate; this shell wrapper is for CI pipelines that grep directly.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Forbidden vocabulary: any format NAME (xml), the parser DEPENDENCY (pugi/pugixml),
# and host-specific (vagus, node-vocabulary tags) symbols. The core is format-
# agnostic -- it must not name a format at all; per-format emitters live in their
# own quarantined modules.
pattern='xml|pugi|<vagus>|<node>|\bvagus\b|#include[[:space:]]*[<"]nucleus/sources/'

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
