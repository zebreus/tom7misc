
default: slides

# not comparison -- it's so slow!!
all : slides/video.talk lorem/lorem.talk knuth/knuth.talk editor/editor.talk torment/torment.talk tom7/tom7.talk gamefaq/gamefaq.talk features/features.talk motherbrain/motherbrain.talk

slides : slides/video.talk

slides/video.talk : video.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. makecodeimage.exe maketalk.exe bovex.exe
	cd .. && ./makecodeimage.exe 3840 2160 video/code-4k.png video/code-4k-self.png
	../bovex.exe -I . -v video.bovex -o video.talk
	../maketalk.exe video video.talk 3840 2160
	cp video/*.png /d/video/rephrase/slides/

# Some expensive animations are split out
lorem/lorem.talk : lorem.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v lorem.bovex -o lorem.talk
	../maketalk.exe lorem lorem.talk 3840 2160

knuth/knuth.talk : knuth.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v knuth.bovex -o knuth.talk
	../maketalk.exe knuth knuth.talk 3840 2160

editor/editor.talk : editor.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v editor.bovex -o editor.talk
	../maketalk.exe editor editor.talk 3840 2160

torment/torment.talk : torment.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v torment.bovex -o torment.talk
	../maketalk.exe torment torment.talk 3840 2160

tom7/tom7.talk : tom7.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v tom7.bovex -o tom7.talk
	../maketalk.exe tom7 tom7.talk 3840 2160

gamefaq/gamefaq.talk : gamefaq.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v gamefaq.bovex -o gamefaq.talk
	../maketalk.exe gamefaq gamefaq.talk 3840 2160

comparison/comparison.talk : comparison.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v comparison.bovex -o comparison.talk
	../maketalk.exe comparison comparison.talk 3840 2160

features/features.talk : features.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v features.bovex -o features.talk
	../maketalk.exe features features.talk 3840 2160

motherbrain/motherbrain.talk : motherbrain.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v motherbrain.bovex -o motherbrain.talk
	../maketalk.exe motherbrain motherbrain.talk 3840 2160

clean :
	rm code-4k.png code-4k-self.png

