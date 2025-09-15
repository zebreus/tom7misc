#!/bin/bash

make -C ../../codec -j 8 catframes.exe || exit -1

ls ../inubs-1159736250-1164519866/*.png ../inubs-1164257722-1164258218/*.png ../inubs-1164256682-1164256698/*.png ../inubs-1159801786-1159785370/*.png ../inubs-1159801786-86748090/*.png ../inubs-1159736250-1163996090/*.png ../inubs-1159736250-1163930558/*.png > inub-frames.txt

../../codec/catframes.exe --frames inub-frames.txt --output inubs1.mov
