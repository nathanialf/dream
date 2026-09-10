#!/usr/bin/env bash
# Point git at the versioned hooks in tools/hooks (pre-commit: no-ROM scan + SHA gate +
# progress refresh; pre-push: SHA gate backstop).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
chmod +x "$ROOT"/tools/hooks/*
git -C "$ROOT" config core.hooksPath tools/hooks
echo "hooks installed: core.hooksPath = tools/hooks"
