
default : httpi.pdf

# requires building bovex.exe
BOVEX=../../rephrase

BOVEX_LIB=$(BOVEX)/stdlib.bovex $(BOVEX)/cite.bovex $(BOVEX)/layout.bovex $(BOVEX)/palatino.bovex $(BOVEX)/paper.bib $(BOVEX)/hyph-en-us.tex

default : httpi.pdf

ALL_IMAGES=

httpi.pdf : httpi.bovex httpi.bib $(BOVEX_LIB) $(ALL_IMAGES)
	$(BOVEX)/bovex.exe -I . -v httpi.bovex -o $@

clean :
	rm -f *.o $(OBJECTS) httpi.pdf $(RESIDUE_SVG) $(SOLID_SVG)
