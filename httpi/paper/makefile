
default : httpi.pdf

# requires building bovex.exe
BOVEX=../../rephrase

BOVEX_LIB=$(BOVEX)/stdlib.bovex $(BOVEX)/cite.bovex $(BOVEX)/layout.bovex $(BOVEX)/layout-base.bovex $(BOVEX)/palatino.bovex $(BOVEX)/hyph-en-us.tex

default : httpi.pdf

ALL_IMAGES=advanced.svg back-to-safety.svg check.svg red-triangle.png

httpi.pdf : httpi.bovex httpi.bib $(BOVEX_LIB) $(ALL_IMAGES) makefile
	$(BOVEX)/bovex.exe -I . -v httpi.bovex -o $@
	cp httpi.pdf /var/www/html/`date +%s`.pdf
	chmod -R a+rX /var/www/html

clean :
	rm -f *.o $(OBJECTS) httpi.pdf $(RESIDUE_SVG) $(SOLID_SVG)
