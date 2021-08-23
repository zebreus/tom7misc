
default: train.exe collate-errors.exe vacuum.exe cull.exe deepen.exe play.exe

# widen.exe
all: train.exe collate-errors.exe vacuum.exe cull.exe get-modelinfo.exe get-layerweights.exe

CC_LIB=../cc-lib

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

# Note, this builds against AMD's distribution of OpenCL, since that's
# the card I had when I started. It also works fine with an NVIDIA GPU,
# and nvidia libs are a bit hard to find since they love CUDA.
AMD_ARCH=x86_64
# AMDSDK="/c/Program Files (x86)/AMD APP SDK"
# mingw wants /c/, but TDM likes c:/
# AMDSDK=c:/Program Files (x86)/AMD APP SDK/3.0
AMDSDK=c:/code/AMD_SDK/

SDLSDK=c:/code/SDL
SDLOPATH=$(SDLSDK)/build
# SDLINCLUDES=-ISDL/include
SDLINCLUDES=-I$(SDLSDK)/include

# XXX maybe only enable -ffast-math for network.o? or call intrinsics there?
CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare $(SDLINCLUDES) -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20 -ffast-math
OPT=-O3

SDLARCH=x64
SDLUTILOBJECTS=$(CC_LIB)/sdl/sdlutil.o $(CC_LIB)/sdl/cursor.o $(CC_LIB)/sdl/font.o
SDLOBJECTS=$(SDLOPATH)/SDL.o $(SDLOPATH)/SDL_error.o $(SDLOPATH)/SDL_fatal.o $(SDLOPATH)/SDL_audio.o $(SDLOPATH)/SDL_audiocvt.o $(SDLOPATH)/SDL_audiodev.o $(SDLOPATH)/SDL_mixer.o $(SDLOPATH)/SDL_mixer_MMX.o $(SDLOPATH)/SDL_mixer_MMX_VC.o $(SDLOPATH)/SDL_mixer_m68k.o $(SDLOPATH)/SDL_wave.o $(SDLOPATH)/SDL_cdrom.o $(SDLOPATH)/SDL_cpuinfo.o $(SDLOPATH)/SDL_active.o $(SDLOPATH)/SDL_events.o $(SDLOPATH)/SDL_expose.o $(SDLOPATH)/SDL_keyboard.o $(SDLOPATH)/SDL_mouse.o $(SDLOPATH)/SDL_quit.o $(SDLOPATH)/SDL_resize.o $(SDLOPATH)/SDL_rwops.o $(SDLOPATH)/SDL_getenv.o $(SDLOPATH)/SDL_iconv.o $(SDLOPATH)/SDL_malloc.o $(SDLOPATH)/SDL_qsort.o $(SDLOPATH)/SDL_stdlib.o $(SDLOPATH)/SDL_string.o $(SDLOPATH)/SDL_thread.o $(SDLOPATH)/SDL_timer.o $(SDLOPATH)/SDL_RLEaccel.o $(SDLOPATH)/SDL_blit.o $(SDLOPATH)/SDL_blit_0.o $(SDLOPATH)/SDL_blit_1.o $(SDLOPATH)/SDL_blit_A.o $(SDLOPATH)/SDL_blit_N.o $(SDLOPATH)/SDL_bmp.o $(SDLOPATH)/SDL_cursor.o $(SDLOPATH)/SDL_gamma.o $(SDLOPATH)/SDL_pixels.o $(SDLOPATH)/SDL_stretch.o $(SDLOPATH)/SDL_surface.o $(SDLOPATH)/SDL_video.o $(SDLOPATH)/SDL_yuv.o $(SDLOPATH)/SDL_yuv_mmx.o $(SDLOPATH)/SDL_yuv_sw.o $(SDLOPATH)/SDL_joystick.o $(SDLOPATH)/SDL_nullevents.o $(SDLOPATH)/SDL_nullmouse.o $(SDLOPATH)/SDL_nullvideo.o $(SDLOPATH)/SDL_diskaudio.o $(SDLOPATH)/SDL_dummyaudio.o $(SDLOPATH)/SDL_sysevents.o $(SDLOPATH)/SDL_sysmouse.o $(SDLOPATH)/SDL_syswm.o $(SDLOPATH)/SDL_wingl.o $(SDLOPATH)/SDL_dibevents.o $(SDLOPATH)/SDL_dibvideo.o $(SDLOPATH)/SDL_dx5events.o $(SDLOPATH)/SDL_dx5video.o $(SDLOPATH)/SDL_dx5yuv.o $(SDLOPATH)/SDL_dibaudio.o $(SDLOPATH)/SDL_dx5audio.o $(SDLOPATH)/SDL_mmjoystick.o $(SDLOPATH)/SDL_syscdrom.o $(SDLOPATH)/SDL_sysmutex.o $(SDLOPATH)/SDL_syssem.o $(SDLOPATH)/SDL_systhread.o $(SDLOPATH)/SDL_syscond.o $(SDLOPATH)/SDL_systimer.o $(SDLOPATH)/SDL_sysloadso.o $(SDLUTILOBJECTS) sdl-win32-main.o

FCEULIB=../fceulib
FCEULIBOBJECTS=$(FCEULIB)/mappers/6.o $(FCEULIB)/mappers/61.o $(FCEULIB)/mappers/24and26.o $(FCEULIB)/mappers/51.o $(FCEULIB)/mappers/69.o $(FCEULIB)/mappers/77.o $(FCEULIB)/mappers/40.o $(FCEULIB)/mappers/mmc2and4.o $(FCEULIB)/mappers/71.o $(FCEULIB)/mappers/79.o $(FCEULIB)/mappers/41.o $(FCEULIB)/mappers/72.o $(FCEULIB)/mappers/80.o $(FCEULIB)/mappers/42.o $(FCEULIB)/mappers/62.o $(FCEULIB)/mappers/73.o $(FCEULIB)/mappers/85.o $(FCEULIB)/mappers/emu2413.o $(FCEULIB)/mappers/46.o $(FCEULIB)/mappers/65.o $(FCEULIB)/mappers/75.o $(FCEULIB)/mappers/50.o $(FCEULIB)/mappers/67.o $(FCEULIB)/mappers/76.o $(FCEULIB)/mappers/tengen.o $(FCEULIB)/utils/memory.o $(FCEULIB)/utils/crc32.o $(FCEULIB)/utils/endian.o $(FCEULIB)/utils/md5.o $(FCEULIB)/utils/xstring.o $(FCEULIB)/boards/mmc1.o $(FCEULIB)/boards/mmc5.o $(FCEULIB)/boards/datalatch.o $(FCEULIB)/boards/mmc3.o $(FCEULIB)/boards/01-222.o $(FCEULIB)/boards/32.o $(FCEULIB)/boards/gs-2013.o $(FCEULIB)/boards/103.o $(FCEULIB)/boards/33.o $(FCEULIB)/boards/h2288.o $(FCEULIB)/boards/106.o $(FCEULIB)/boards/34.o $(FCEULIB)/boards/karaoke.o $(FCEULIB)/boards/108.o $(FCEULIB)/boards/3d-block.o $(FCEULIB)/boards/kof97.o $(FCEULIB)/boards/112.o $(FCEULIB)/boards/411120-c.o $(FCEULIB)/boards/konami-qtai.o $(FCEULIB)/boards/116.o $(FCEULIB)/boards/43.o $(FCEULIB)/boards/ks7012.o $(FCEULIB)/boards/117.o $(FCEULIB)/boards/57.o $(FCEULIB)/boards/ks7013.o $(FCEULIB)/boards/120.o $(FCEULIB)/boards/603-5052.o $(FCEULIB)/boards/ks7017.o $(FCEULIB)/boards/121.o $(FCEULIB)/boards/68.o $(FCEULIB)/boards/ks7030.o $(FCEULIB)/boards/12in1.o $(FCEULIB)/boards/8157.o $(FCEULIB)/boards/ks7031.o $(FCEULIB)/boards/15.o $(FCEULIB)/boards/82.o $(FCEULIB)/boards/ks7032.o $(FCEULIB)/boards/151.o $(FCEULIB)/boards/8237.o $(FCEULIB)/boards/ks7037.o $(FCEULIB)/boards/156.o $(FCEULIB)/boards/830118c.o $(FCEULIB)/boards/ks7057.o $(FCEULIB)/boards/164.o $(FCEULIB)/boards/88.o $(FCEULIB)/boards/le05.o $(FCEULIB)/boards/168.o $(FCEULIB)/boards/90.o $(FCEULIB)/boards/lh32.o $(FCEULIB)/boards/17.o $(FCEULIB)/boards/91.o $(FCEULIB)/boards/lh53.o $(FCEULIB)/boards/170.o $(FCEULIB)/boards/95.o $(FCEULIB)/boards/malee.o $(FCEULIB)/boards/175.o $(FCEULIB)/boards/96.o $(FCEULIB)/boards/176.o $(FCEULIB)/boards/99.o $(FCEULIB)/boards/177.o $(FCEULIB)/boards/178.o $(FCEULIB)/boards/a9746.o $(FCEULIB)/boards/18.o $(FCEULIB)/boards/ac-08.o $(FCEULIB)/boards/n625092.o $(FCEULIB)/boards/183.o $(FCEULIB)/boards/addrlatch.o $(FCEULIB)/boards/novel.o $(FCEULIB)/boards/185.o $(FCEULIB)/boards/ax5705.o $(FCEULIB)/boards/onebus.o $(FCEULIB)/boards/186.o $(FCEULIB)/boards/pec-586.o $(FCEULIB)/boards/187.o $(FCEULIB)/boards/bb.o $(FCEULIB)/boards/sa-9602b.o $(FCEULIB)/boards/189.o $(FCEULIB)/boards/bmc13in1jy110.o $(FCEULIB)/boards/193.o $(FCEULIB)/boards/bmc42in1r.o $(FCEULIB)/boards/sc-127.o $(FCEULIB)/boards/199.o $(FCEULIB)/boards/bmc64in1nr.o $(FCEULIB)/boards/sheroes.o $(FCEULIB)/boards/208.o $(FCEULIB)/boards/bmc70in1.o $(FCEULIB)/boards/sl1632.o $(FCEULIB)/boards/222.o $(FCEULIB)/boards/bonza.o $(FCEULIB)/boards/smb2j.o $(FCEULIB)/boards/225.o $(FCEULIB)/boards/bs-5.o $(FCEULIB)/boards/228.o $(FCEULIB)/boards/cityfighter.o $(FCEULIB)/boards/super24.o $(FCEULIB)/boards/230.o $(FCEULIB)/boards/dance2000.o $(FCEULIB)/boards/n106.o $(FCEULIB)/boards/supervision.o $(FCEULIB)/boards/232.o $(FCEULIB)/boards/t-227-1.o $(FCEULIB)/boards/234.o $(FCEULIB)/boards/deirom.o $(FCEULIB)/boards/t-262.o $(FCEULIB)/boards/sachen.o $(FCEULIB)/boards/235.o $(FCEULIB)/boards/dream.o $(FCEULIB)/boards/244.o $(FCEULIB)/boards/edu2000.o $(FCEULIB)/boards/tf-1201.o $(FCEULIB)/boards/bandai.o $(FCEULIB)/boards/246.o $(FCEULIB)/boards/famicombox.o $(FCEULIB)/boards/transformer.o $(FCEULIB)/boards/252.o $(FCEULIB)/boards/fk23c.o $(FCEULIB)/boards/vrc2and4.o $(FCEULIB)/boards/253.o $(FCEULIB)/boards/ghostbusters63in1.o $(FCEULIB)/boards/vrc7.o $(FCEULIB)/boards/28.o $(FCEULIB)/boards/gs-2004.o $(FCEULIB)/boards/yoko.o $(FCEULIB)/input/arkanoid.o $(FCEULIB)/input/ftrainer.o $(FCEULIB)/input/oekakids.o $(FCEULIB)/input/suborkb.o $(FCEULIB)/input/bworld.o $(FCEULIB)/input/hypershot.o $(FCEULIB)/input/powerpad.o $(FCEULIB)/input/toprider.o $(FCEULIB)/input/cursor.o $(FCEULIB)/input/mahjong.o $(FCEULIB)/input/quiz.o $(FCEULIB)/input/zapper.o $(FCEULIB)/input/fkb.o $(FCEULIB)/input/shadow.o $(FCEULIB)/cart.o $(FCEULIB)/version.o $(FCEULIB)/emufile.o $(FCEULIB)/fceu.o $(FCEULIB)/fds.o $(FCEULIB)/file.o $(FCEULIB)/filter.o $(FCEULIB)/ines.o $(FCEULIB)/input.o $(FCEULIB)/palette.o $(FCEULIB)/ppu.o $(FCEULIB)/sound.o $(FCEULIB)/state.o $(FCEULIB)/unif.o $(FCEULIB)/vsuni.o $(FCEULIB)/x6502.o $(FCEULIB)/git.o $(FCEULIB)/fc.o $(FCEULIB)/emulator.o $(FCEULIB)/headless-driver.o $(FCEULIB)/simplefm2.o $(FCEULIB)/stringprintf.o $(FCEULIB)/simplefm7.o

LINKSDL=-lm -mthreads -luser32 -lgdi32 -lwinmm -ldxguid

CLINCLUDES="-I$(AMDSDK)/include"
CLLIBS='-L${AMDSDK}/lib/${AMD_ARCH}'

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/city/city.o $(CC_LIB)/opt/opt.o $(CC_LIB)/mp3.o $(CC_LIB)/top.o

#  -Wno-write-strings
CPPFLAGS=-DPSS_STYLE=1 -DHAVE_ASPRINTF -m64 $(OPT) -D__MINGW32__ -DHAVE_ALLOCA -DNOWINSTUFF $(SDLINCLUDES) $(PROFILE) $(FLTO) $(CLINCLUDES) -I $(CC_LIB)/ --std=c++20

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

OBJECTS=$(UTIL_OBJECTS) network.o error-history.o modelinfo.o

# without static, can't find lz or lstdcxx maybe?
LFLAGS= -L. $(LINKSDL) -m64 -Wl,--subsystem,console $(CLLIBS) -lz -lOpenCL $(OPT) $(FLTO) $(PROFILE) -lpsapi -static

modelinfo.png : get-modelinfo.exe net0.val
	./get-modelinfo.exe

layerweights-layer0.png : get-layerweights.exe net0.val
	./get-layerweights.exe net0.val layerweights

layerweights : layerweights-layer0.png

train.exe : $(OBJECTS) $(SDLOBJECTS) $(FCEULIBOBJECTS) clutil.o train.o frame-queue.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

play.exe : $(OBJECTS) $(SDLOBJECTS) $(FCEULIBOBJECTS) clutil.o play.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

vacuum.exe : $(OBJECTS) vacuum.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

cull.exe : $(OBJECTS) cull.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

deepen.exe : $(OBJECTS) deepen.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

get-modelinfo.exe : $(OBJECTS) get-modelinfo.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

get-layerweights.exe : $(OBJECTS) get-layerweights.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

collate-errors.exe : collate-errors.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

new-network.exe : new-network.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

conv_test.exe : conv_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o $(UTIL_OBJECTS) $(SDLUTILOBJECTS) $(FCEULIBOBJECTS) $(OBJECTS) *.exe
