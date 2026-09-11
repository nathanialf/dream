#!/usr/bin/env bash
# The DLL import gate for the Windows executables, in one place.
#
# The release zip carries the executables and nothing else, so every DLL they import has
# to be one Windows itself provides. This is the allowlist that decides that, and it is
# the only copy: `make win-dlls`, the ci.yml windows job and the release.yml windows job
# all call this script. They used to test `grep -Eiq 'DLL Name: *(lib|SDL3\.dll)'`, which
# flags a DLL whose name starts with lib and SDL3.dll itself and passes everything else,
# so zlib1.dll or an MSYS2 runtime DLL would have shipped in a zip that cannot start on a
# clean Windows machine.
#
#   OBJDUMP=x86_64-w64-mingw32-objdump tools/check_dlls.sh build/win/dream.exe ...
#
# A path that does not exist is reported and skipped, so the Makefile can name both
# executables when only one was built. Being handed nothing that exists is an error.
set -eu
OBJDUMP="${OBJDUMP:-objdump}"

is_system_dll() {
    case "$1" in
      advapi32.dll|gdi32.dll|imm32.dll|kernel32.dll|msvcrt.dll|ole32.dll|\
      oleaut32.dll|setupapi.dll|shell32.dll|user32.dll|version.dll|winmm.dll|\
      ucrtbase.dll|api-ms-win-*|hid.dll|dwmapi.dll|shcore.dll|ws2_32.dll) return 0 ;;
      *) return 1 ;;
    esac
}

bad=0
seen=0
for exe in "$@"; do
    if [ ! -f "$exe" ]; then
        echo "check_dlls: $exe was not built, skipping"
        continue
    fi
    seen=1
    echo "$exe imports:"
    while read -r dll; do
        [ -n "$dll" ] || continue
        if is_system_dll "$(printf '%s' "$dll" | tr 'A-Z' 'a-z')"; then
            echo "    $dll"
        else
            echo "    $dll   <-- NOT a system DLL"
            bad=1
        fi
    done <<EOF
$("$OBJDUMP" -p "$exe" | sed -n 's/^[[:space:]]*DLL Name: //p' | sort -u)
EOF
done

if [ "$seen" -eq 0 ]; then
    echo "check_dlls: none of the executables given exist" >&2
    exit 2
fi
if [ "$bad" -ne 0 ]; then
    echo "check_dlls: the Windows build imports a non-system DLL; the zip ships no runtime" >&2
    exit 1
fi
