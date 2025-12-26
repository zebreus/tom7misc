#!/bin/bash

# See:
# https://fontforge.org/docs/scripting/scripting-alpha.html

FONTFORGE="/c/Program Files (x86)/FontForgeBuilds/bin/fontforge.exe"

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 font.sfd font.ttf"
    exit 1
fi

INPUT_SFD=$(cygpath -w "$1")
OUTPUT_TTF=$(cygpath -w "$2")

FONTFORGE_VERBOSE=1

"${FONTFORGE}" -lang=ff -script - <<EOF
Print("Starting...");
Open("${INPUT_SFD}")
Generate("${OUTPUT_TTF}")
Close()
Print("OK");
Quit(0)
EOF

