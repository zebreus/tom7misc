
default: slides

slides : slides/video.talk

slides/video.talk : video.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png
	make -j -C .. makecodeimage.exe maketalk.exe bovex.exe
	cd .. && ./makecodeimage.exe 3840 2160 video/code-4k.png video/code-4k-self.png
	../bovex.exe -I . -v video.bovex -o video.talk
	../maketalk.exe video video.talk

clean :
	rm code-4k.png code-4k-self.png
