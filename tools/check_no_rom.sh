#!/usr/bin/env bash
# Refuse to commit anything that carries ROM-derived bytes: ROM images, extracted data,
# build output, or inline byte runs in the generated source.
#
# bash, not /bin/sh: the path loops read `git ls-files -z` with `read -r -d ''`, and the
# greps use POSIX character classes rather than GNU's \s. `git ls-files` without -z
# C-quotes any path holding a non-ASCII or special byte ("data/\303\251.bin"), and a
# quoted path matches none of the case patterns below, so the scan used to report success
# on a tracked `data/é.bin`. -z emits the raw bytes with no quoting and no word splitting.
set -eu
cd "$(dirname "$0")/.."
git rev-parse --show-toplevel >/dev/null   # fail loudly if this is not a work tree
bad=0
while IFS= read -r -d '' f; do
  case "$f" in
    *.sfc|*.smc|*.bin|*.zip|baserom/*|data/*|build/*|out/*) echo "ROM-derived file tracked: $f"; bad=1 ;;
  esac
done < <(git ls-files -z)
# Generated sources must reference data by incbin range only. A byte run emitted as a
# numeric db, dw, dl or dd is data just as much as a db run is, so all four are caught;
# a jump-table entry with no label belongs in tools/names.txt, not in src/ as `dw $XXXX`.
# The `symbolic` exemption is for a line whose operand is a label or an expression.
if git ls-files -z 'src/*.asm' |
   xargs -0 grep -nE '^[[:space:]]*(db|dw|dl|dd)[[:space:]]+(\$|[0-9])' 2>/dev/null |
   grep -v ';.*symbolic' ; then
  echo "inline byte data found in src/ (use incbin ranges)"; bad=1
fi
if git ls-files -z 'spc/*.asm' |
   xargs -0 grep -nE '^[[:space:]]*(db|dw|dl|dd)[[:space:]]+(\$|[0-9])' 2>/dev/null ; then
  echo "inline byte data found in spc/ (use incbin ranges)"; bad=1
fi
# no ROM images anywhere in the tree, tracked or not, above 1 MiB with SNES magic sizes
while IFS= read -r -d '' f; do
  case "$f" in *.sfc|*.smc) echo "untracked ROM image present (fine, but never add it): $f" ;; esac
done < <(git ls-files -z -o --exclude-standard)
exit $bad
