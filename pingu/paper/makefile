default : pingu.pdf

MAKEPDF=../../pactom/rsvg/rsvg-convert -f pdf

GRAPHICS=

pingu.pdf : pingu.tex pingu.bib $(GRAPHICS)
	pdflatex pingu
	bibtex pingu
	pdflatex pingu
	pdflatex pingu

# With generated PGN including graphical chesspieces, this needs more
# memory for tex. With miktex, try
#   initexmf --edit-config-file=pdflatex
# then add these lines
#   main_memory=5000000
#   extra_mem_bot=5000000
#   font_mem_size=5000000
#   pool_size=5000000
#   buf_size=5000000
# then run
#   initexmf --dump=pdflatex

%.pdf : %.svg
	${MAKEPDF} $< -o $@
