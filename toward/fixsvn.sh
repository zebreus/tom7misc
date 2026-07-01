#!/bin/bash

# nb: This does not work for files with spaces in them
# like "svn st", but gets only the tracked files in the current directory.
function svntracked() {
  TRACKED=`svn status -vq --depth=files | grep -v \>`

  for file in $(echo "$TRACKED" | sed 's/.*[[:space:]]//'); do
      if [ -f "$file" ]; then
          echo "$file"
      fi
  done
}

SOURCE_FILES=`svntracked | grep '\.\(cc\|h\|svg\)$'`

svn propdel svn:mime-type ${SOURCE_FILES} | grep -v nonexistent
svn propdel svn:executable ${SOURCE_FILES} | grep -v nonexistent

