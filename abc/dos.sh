#!/bin/bash

make -C dosbox && dosbox/src/dosbox -c "mount d: dos" -c "d:" -c "dir"
