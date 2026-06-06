#!/usr/bin/env bash
# Core-purity grep for CI. The core must be domain-neutral and format-agnostic:
# no XML / pugixml coupling and no host vocabulary leaking into it. Parser
# dependencies live only inside their own source modules (e.g. a future
# src/nucleus/xml/), never in core. This script greps the public headers and
# core implementation for forbidden symbols and exits nonzero on any hit.
#
# A CMake-script equivalent (scripts/core_purity_check.cmake) is wired as a
# CTest gate; this shell wrapper is for CI pipelines that grep directly.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Forbidden vocabulary: format-specific (xml/pugi) and host-specific (vagus,
# node-vocabulary tags) symbols.
pattern='pugi|xml|<vagus>|<node>|\bvagus\b'

# The quarantined module that is ALLOWED to mention parser/host vocabulary
# because it IS the wrapper (does not exist yet; excluded so the gate stays
# correct once the xml source module lands).
quarantine='src/nucleus/xml/'

hits="$(grep -rinE "$pattern" \
        "$root/include/nucleus" "$root/src/nucleus" 2>/dev/null \
        | grep -v "$quarantine" || true)"

if [[ -n "$hits" ]]; then
    echo "Core-purity violations (forbidden format/host vocabulary in core):"
    echo "$hits"
    exit 1
fi

echo "core-purity check: clean (0 forbidden-symbol hits in core)"
