
default : httpv.pdf install

# requires building bovex.exe
BOVEX=../../rephrase

BOVEX_LIB=$(BOVEX)/stdlib.bovex $(BOVEX)/cite.bovex $(BOVEX)/layout.bovex $(BOVEX)/layout-base.bovex $(BOVEX)/font.bovex $(BOVEX)/hyph-en-us.tex

default : httpv.pdf

install : httpv.pdf
	cp httpv.pdf /var/www/html/httpv-`date +%s`.pdf
	chmod -R a+rX /var/www/html

ALL_IMAGES=advanced.svg back-to-safety.svg check.svg red-triangle.png green-padlock.svg warning-triangle.svg encrypt-growth.svg mbti-ieah.svg

httpv.pdf : httpv.bovex httpv.bib $(BOVEX_LIB) $(ALL_IMAGES) makefile
	$(BOVEX)/bovex.exe -I . -v httpv.bovex -o $@

profile: bovex.prof
	go run github.com/google/pprof@latest -http=0.0.0.0:8080 $(BOVEX)/bovex.exe bovex.prof

bovex.prof : httpv.bovex $(BOVEX)/bovex.exe
	CPUPROFILE=bovex.prof LD_PRELOAD=libprofiler.so.0 $(BOVEX)/bovex.exe -I . -v httpv.bovex -o httpv.pdf

clean :
	rm -f *.o $(OBJECTS) httpv.pdf $(RESIDUE_SVG) $(SOLID_SVG)
