#!/bin/sh

set +e
set +x

make clean
make -j

./heap_test.exe
./rle_test.exe
./interval-tree_test.exe
./threadutil_test.exe
./color-util_test.exe
./textsvg_test.exe
./lines_test.exe
./image_test.exe
./util_test.exe
./randutil_test.exe
./json_test.exe
./arcfour_test.exe
./lastn-buffer_test.exe
./list-util_test.exe
./edit-distance_test.exe
./re2_test.exe
# ./webserver_test.exe
./stb_image_bench.exe
./process-util_test.exe
./stb_truetype_test.exe
./packrect_test.exe
./xml_test.exe
./mp3_test.exe
./top_test.exe
./bounds_test.exe
./optional-iterator_test.exe
./tuple-util_test.exe
./bitbuffer_test.exe
./ansi_test.exe
./factorization_test.exe
./atomic-util_test.exe
./interval-cover_test.exe
./set-util_test.exe
./hashing_test.exe
./subprocess_test.exe
./city_test.exe
./autoparallel_test.exe
./int128_test.exe
./do-not-optimize_test.exe
./pcg_test.exe
./montgomery64_test.exe
./numbers_test.exe
./work-queue_test.exe
./csv_test.exe
./pdf_test.exe
./parser-combinators_test.exe
./functional-map_test.exe
./boxes-and-glue_test.exe
./qr-code_test.exe
./image-resize_test.exe
./integer-voronoi_test.exe
./status-bar_test.exe
./top_n_test.exe
./map-util_test.exe
./interval-cover-util_test.exe
./array-util_test.exe
./sorting-network_test.exe
./ansi-image_test.exe
./base_test.exe
./auto-histo_test.exe
./nice_test.exe
./byte-set_test.exe
./byte-set_bench.exe
./zip_test.exe
./png_test.exe
./wikipedia_test.exe
./symmetric-matrix_test.exe
./utf8_test.exe
./string-table_test.exe
./unicode-data_test.exe
