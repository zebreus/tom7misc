#!/bin/bash

# Usage: ./macos-update-dylibs.sh <path_to_executable> <path_to_frameworks_dir>
#
# Helps prepare a macOS .app for distribution by copying dynamic
# libraries (dylibs) that the application depends on (e.g., from
# Homebrew or /usr/local) into the bundle's Frameworks directory.
#
# When we build escape.exe with homebrew's SDL (and other dynamic
# libraries), it uses absolute paths like /opt/homebrew/lib/libSDL2.dylib.
# We don't want users to have to install homebrew and the same
# libraries, so we:
#  - Transitively discover all the dylib dependncies of the binary
#    using otool.
#  - Copy them into the .app bundle's Frameworks dir.
#  - Add an "rpath" to the executable so it looks in its Frameworks
#    dir for dylibs.
#  - Rewrites the references in the executable and the dylibs
#    themselves using install_name_tool so they point to the bundled
#    copies via @executable_path.

EXE_PATH="$1"
FRAMEWORKS_DIR="$2"

if [ -z "$EXE_PATH" ] || [ -z "$FRAMEWORKS_DIR" ]; then
    echo "Usage: $0 <path_to_executable> <path_to_frameworks_dir> [dylibs...]"
    exit 1
fi

shift 2

install_name_tool -add_rpath @executable_path/../Frameworks "$EXE_PATH" 2>/dev/null || true

for dylib in "$@"; do
	if [ -f "$dylib" ]; then
		cp "$dylib" "$FRAMEWORKS_DIR/"
		chmod u+w "$FRAMEWORKS_DIR/"$(basename "$dylib")
	fi
done

added=1
while [ "$added" -eq 1 ]; do
	added=0
	for target in "$EXE_PATH" "$FRAMEWORKS_DIR"/*.dylib; do
		if [ ! -e "$target" ]; then continue; fi
		for lib in $(otool -L "$target" | grep -e /opt/homebrew -e /usr/local | awk '{print $1}'); do
			base=$(basename "$lib")
			if [ ! -f "$FRAMEWORKS_DIR/$base" ]; then
				cp "$lib" "$FRAMEWORKS_DIR/"
				chmod u+w "$FRAMEWORKS_DIR/$base"
				added=1
			fi
			install_name_tool -change "$lib" "@executable_path/../Frameworks/$base" "$target"
		done
	done
done
