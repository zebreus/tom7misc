
default : chrisamaconf/chrisamaconf.talk

# requires building bovex.exe
BOVEX=../../rephrase

# ALL_IMAGES=advanced.svg back-to-safety.svg check.svg red-triangle.png green-padlock.svg warning-triangle.svg encrypt-growth.svg mbti-ieah.svg

BOVEX_LIB=$(BOVEX)/stdlib.bovex $(BOVEX)/color.bovex $(BOVEX)/cite.bovex $(BOVEX)/layout.bovex $(BOVEX)/layout-base.bovex $(BOVEX)/font.bovex $(BOVEX)/talk-shared.bovex $(BOVEX)/hyph-en-us.tex

chrisamaconf/chrisamaconf.talk : $(BOVEX)/bovex.exe $(BOVEX)/maketalk.exe $(BOVEX)/talk.html $(BOVEX_LIB) chrisamaconf.bovex *.png
	$(BOVEX)/bovex.exe -I . -v chrisamaconf.bovex -o chrisamaconf.talk
	$(BOVEX)/maketalk.exe chrisamaconf chrisamaconf.talk

clean :
	rm -f *.o $(OBJECTS) httpv.pdf
