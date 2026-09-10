#!/bin/sh
# Refuse to commit anything that carries ROM-derived bytes: ROM images, extracted data,
# build output, or inline byte runs in the generated source.
set -e
cd "$(dirname "$0")/.."
bad=0
tracked=$(git ls-files)
for f in $tracked; do
  case "$f" in
    *.sfc|*.smc|*.bin|*.zip|baserom/*|data/*|build/*|out/*) echo "ROM-derived file tracked: $f"; bad=1 ;;
  esac
done
# generated sources must reference data by incbin range only
if git ls-files 'src/*.asm' | xargs grep -nE '^\s*db\s' 2>/dev/null | grep -v ';.*symbolic' ; then
  echo "inline byte data found in src/ (use incbin ranges)"; bad=1
fi
if git ls-files 'spc/*.asm' | xargs grep -nE '^\s*db\s+\$' 2>/dev/null ; then
  echo "inline byte data found in spc/ (use incbin ranges)"; bad=1
fi
# no ROM images anywhere in the tree, tracked or not, above 1 MiB with SNES magic sizes
for f in $(git ls-files -o --exclude-standard); do
  case "$f" in *.sfc|*.smc) echo "untracked ROM image present (fine, but never add it): $f" ;; esac
done
exit $bad
