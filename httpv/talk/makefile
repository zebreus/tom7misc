
default : sigbovik/sigbovik.talk

# requires building bovex.exe
BOVEX=../../rephrase

# ALL_IMAGES=advanced.svg back-to-safety.svg check.svg red-triangle.png green-padlock.svg warning-triangle.svg encrypt-growth.svg mbti-ieah.svg

BOVEX_LIB=$(BOVEX)/stdlib.bovex $(BOVEX)/cite.bovex $(BOVEX)/layout.bovex $(BOVEX)/layout-base.bovex $(BOVEX)/font.bovex $(BOVEX)/talk-shared.bovex $(BOVEX)/hyph-en-us.tex

sigbovik/sigbovik.talk : $(BOVEX)/bovex.exe $(BOVEX)/maketalk.exe $(BOVEX)/talk.html $(BOVEX_LIB) sigbovik.bovex *.png
	$(BOVEX)/bovex.exe -I . -v sigbovik.bovex -o sigbovik.talk
	$(BOVEX)/maketalk.exe sigbovik sigbovik.talk

clean :
	rm -f *.o $(OBJECTS) httpv.pdf
