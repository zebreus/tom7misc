
# default : programs outputs thumbnails
# default : kmlcat
# default : shortest.kml shortest.svg
default : map.exe map.svg
# default : llt.svg

# XXXXXXXXXXXXXXXXXXXXX
# NOTE! I commented out the dependencies on binaries because
# I cannot build mlton on Windows 10 in 2019 :(

all : programs outputs thumbnails pdfs obsolete release

PACTOMLIB=pactom.sml pactom-sig.sml ../sml-lib/files/xml/xml*.sml ../sml-lib/misc/color*.sml ../sml-lib/geom/latlon*.sml ../sml-lib/geom/snappoint*.sml ../sml-lib/geom/latlontree*.sml ../sml-lib/geom/quadtree*.sml ../sml-lib/geom/pointlocation*.sml ../sml-lib/data/undirected-graph*.sml ../sml-lib/util/growarray*.sml ../sml-lib/algo/heap*.sml

# has trouble on new cygwin, because it's a bash builtin?
TIME=

# '/c/program files (x86)/mlton/bin/mlton'
# MLTON=/c/mlton/bin/mlton.bat -target x86_64-w64-mingw32 -codegen amd64
MLTON=/usr/src/mlton/bin/mlton

# Generating programs.
programs : shortestpaths.exe colorize.exe map.exe elevsvg.exe makepoints.exe radial.exe lltsvg.exe kmlcat.exe

kmlcat.exe : kmlcat.sml kmlcat.mlb ../sml-lib/util/*.sml
	${MLTON} -output $@ kmlcat.mlb

shortestpaths.exe : shortestpaths.sml shortestpaths.mlb ${PACTOMLIB} 
	${MLTON} -output $@ shortestpaths.mlb

colorize.exe : colorize.sml colorize.mlb ${PACTOMLIB}
	${MLTON} -output $@ colorize.mlb

map.exe : map.sml map.mlb ${PACTOMLIB}
	${MLTON} -output $@ map.mlb

# XXX need output for this
elevsvg.exe : elevsvg.sml elevsvg.mlb ${PACTOMLIB}
	${MLTON} -output $@ elevsvg.mlb

makepoints.exe : makepoints.sml makepoints.mlb ${PACTOMLIB} ../sml-lib/geom/*.sml
	${MLTON} -output $@ makepoints.mlb

radial.exe : radial.sml radial.mlb ${PACTOMLIB} ../sml-lib/geom/*.sml
	${MLTON} -output $@ radial.mlb

lltsvg.exe : lltsvg.sml lltsvg.mlb ${PACTOMLIB}
	${MLTON} -output $@ lltsvg.mlb

# outputs
# XXX these should explicitly depend on inputs, like pac.kml

outputs : radial.svg points.kml llt.svg map.svg

map.svg : pac.kml pacannotations.kml pac2.kml neighborhoods.kml interstate-logo.svg # map
	${TIME} ./map.exe > $@

radial.svg : pac.kml pac2.kml # radial
	${TIME} ./radial.exe > $@

points.kml : neighborhoods.kml # makepoints 
	${TIME} ./makepoints.exe > $@

# This is getting near the maximum memory for 32 bit machines! Probably can do some
# kind of pre-thinning of the points, or maybe a more efficient kd-tree representation,
# or merge at load time? (Or just use the new C++ version...)
# @MLton max-heap 600m --
shortest.kml shortest.svg :  pac.kml pac2.kml # shortestpaths
	${TIME} ./shortestpaths.exe -kmlout shortest.kml -svgout shortest.svg

llt.svg :  pac.kml pac2.kml # lltsvg
	${TIME} ./lltsvg.exe > $@


# MAKETHUMB=convert -scale 400
# MAKETHUMB="/c/program files/inkscape/inkscape"
MAKETHUMB=rsvg/rsvg-convert --keep-aspect-ratio --width=580
MAKEPDF=rsvg/rsvg-convert -f pdf

# XXX Text rendering is not that good this way. Might want to render at a much
# larger size then downsample with imagemagick?

# Convert SVGs to thumbnails for webpage
thumbnails : map.png shortest.png
pdfs : map.pdf shortest.pdf

map.png : map.svg
	${MAKETHUMB} $< -o $@

shortest.png : shortest.svg
	${MAKETHUMB} $< -o $@

radial.png : radial.svg
	${MAKETHUMB} $< -o $@

%.pdf : %.svg
	${MAKEPDF} $< -o $@

# Obsolete
obsolete : parsexml.exe

parsexml.exe : parsexml.mlb parsexml.sml ${PACTOMLIB}
	${MLTON} -output $@ parsexml.mlb

# For release, we expect the following files in the current directory:
#   pac.kml
#   pac2.kml

RELEASEFILES=release/pactom.kmz release/shortest.svg release/map.svg release/map.png release/shortest.png release/shortest.pdf release/map.pdf release/radial.svg release/radial.pdf release/radial.png
release : ${RELEASEFILES} $(addsuffix .size, ${RELEASEFILES})
	date +"%d %b %Y at %H:%M" | tr -d '\012' > release/updated
upload : release
	scp -P 77 -r release root@spacebar.org:/var/www/pac/

release/%.png : %.png
	cp $< $@

release/%.svg : %.svg
	cp $< $@

release/%.pdf : %.pdf
	cp $< $@

# XXX also annotations, neighborhood, etc.
release/pactom.kmz : pac.kml pac2.kml # kmlcat
	rm -f _pactom.kml
	./kmlcat.exe pac.kml pac2.kml > _pactom.kml
	rm -f release/pactom.kmz
	zip release/pactom.kmz _pactom.kml

# Produces a human-readable size (i.e. "2.7M") and strips newlines.
# These are used on the web page to report the size of downloadable
# files.
release/%.size : release/%
	du --si $^ | cut -f 1 | tr -d '\012' > $@

clean :
	rm -f _pactom.kml release/pactom.kmz release/*.size *~

# clean final generated files too, including the whole release dir
veryclean : clean
	rm -rf release
	mkdir release
	rm -f shortest.kml shortest.svg map.svg shortest.pdf map.pdf radial.svg points.kml radial.pdf *.exe
