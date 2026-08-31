#!/bin/bash

set +x

make -j 20

./letters_test.exe
./prop_test.exe
./chessprop_test.exe
./circuit_test.exe
./cell-library_test.exe
./layout_test.exe
./layout-canvas_test.exe
./render-circuit_test.exe
./aig_test.exe
./minitable_test.exe
./simplification_test.exe
./aiger_test.exe
./verilog_test.exe
./drc_test.exe

