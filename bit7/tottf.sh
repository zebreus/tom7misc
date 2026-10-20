#!/bin/bash

# See:
# https://fontforge.org/docs/scripting/scripting-alpha.html

if command -v fontforge >/dev/null 2>&1; then
    FONTFORGE="fontforge"
    INPUT_SFD="$1"
    OUTPUT_TTF="$2"
else
    # Windows.
    FONTFORGE="/c/Program Files (x86)/FontForgeBuilds/bin/fontforge.exe"
    # Also need to convert paths.
    INPUT_SFD=$(cygpath -w "$1")
    OUTPUT_TTF=$(cygpath -w "$2")
fi

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 font.sfd font.ttf"
    exit 1
fi


FONTFORGE_VERBOSE=1

"${FONTFORGE}" -lang=ff -script - <<EOF
Print("Starting...");
Open("${INPUT_SFD}")
Generate("${OUTPUT_TTF}")
Close()
Print("OK");
Quit(0)
EOF

