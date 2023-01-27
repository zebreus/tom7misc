# Makefile made by tom7.
default: bench.exe emulator_test.exe

all: emulator_test.exe bench.exe

# -fno-strict-aliasing
# -Wstrict-overflow=3
# -std=c++11
# -DDISABLE_SOUND=1 -DDISABLE_VIDEO=1
CXXFLAGS=-std=c++20 -Wall -Wno-deprecated -I/usr/local/include

# XXX -O2
OPT=-g -O2  # -O3 -fno-strict-aliasing
# try -flto!
FLTO=

ARCH=-m64
# ARCH=-m32

ifdef OSX
CXX=g++
CC=gcc
else
ifdef LINUX
CXX=g++
CC=gcc
PLATFORMLINK=-lpthread -static
else
# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

# PLATFORMCFLAGS= -D__MINGW32__ -D_GLIBCXX_HAS_GTHREADS -mthreads
PLATFORMCFLAGS=
# without static, can't find lz or lstdcxx maybe?
PLATFORMLINK=-mthreads -Wl,--subsystem,console -lwinpthread -L. -static
endif
endif

# Suppress compilation commands, but show some indication of progress.
# (Explicitly invoke bash to get shell builtin, since on OS X echo
# otherwise treats -n literally.)
%.o : %.cc ../fluint8.h x6502.h
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INSTRUMENT) -c -o $@ $<
	@bash -c "echo -n '.'"

# AOT_INSTRUMENTATION is cheap but not free instrumentation in x6502
# core that's used to histogram the PC locations that are executed.
INSTRUMENT=-DAOT_INSTRUMENTATION=1

# If you don't have SDL, you can leave these out, and maybe it still works.
LINKSDL= -mno-cygwin -lm -luser32 -lgdi32 -lwinmm -ldxguid

CC_LIB=../../cc-lib

# Emulator uses city, yeah? See if we can make this work with no deps.
INCLUDES=-I ".." -I "$(CC_LIB)" -I "$(CC_LIB)/city" -I "."

# tom7 added -mthreads on 9/7!
CPPFLAGS=-DPSS_STYLE=1 -DDUMMY_UI $(ARCH) $(OPT) $(PLATFORMCFLAGS) -DHAVE_ALLOCA -DNOWINSTUFF $(INCLUDES) $(PROFILE) $(FLTO) --std=c++20

GRAD_OBJECTS=../fluint8.o

# Should just be used for testing/utilities.
CCLIBOBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/city/city.o $(CC_LIB)/rle.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/wavesave.o $(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/ansi.o

MAPPEROBJECTS=mappers/6.o mappers/61.o mappers/24and26.o mappers/51.o mappers/69.o mappers/77.o mappers/40.o mappers/mmc2and4.o mappers/71.o mappers/79.o mappers/41.o mappers/72.o mappers/80.o mappers/42.o mappers/62.o mappers/73.o mappers/85.o mappers/emu2413.o mappers/46.o mappers/65.o mappers/75.o mappers/50.o mappers/67.o mappers/76.o mappers/tengen.o

# XXX: Probably a bunch of this can go?
UTILSOBJECTS= utils/memory.o utils/crc32.o utils/endian.o utils/md5.o utils/xstring.o

# unused -
# boards/a9711.o
# boards/n-c22m.o
# boards/179.o
# only used via old _init interface:
# boards/subor.o

BOARDSOBJECTS=boards/mmc1.o boards/mmc5.o boards/datalatch.o boards/mmc3.o boards/01-222.o boards/32.o boards/gs-2013.o boards/103.o boards/33.o boards/h2288.o boards/106.o boards/34.o boards/karaoke.o boards/108.o boards/3d-block.o boards/kof97.o boards/112.o boards/411120-c.o boards/konami-qtai.o boards/116.o boards/43.o boards/ks7012.o boards/117.o boards/57.o boards/ks7013.o boards/120.o boards/603-5052.o boards/ks7017.o boards/121.o boards/68.o boards/ks7030.o boards/12in1.o boards/8157.o boards/ks7031.o boards/15.o boards/82.o boards/ks7032.o boards/151.o boards/8237.o boards/ks7037.o boards/156.o boards/830118c.o boards/ks7057.o boards/164.o boards/88.o boards/le05.o boards/168.o boards/90.o boards/lh32.o boards/17.o boards/91.o boards/lh53.o boards/170.o boards/95.o boards/malee.o boards/175.o boards/96.o boards/176.o boards/99.o boards/177.o boards/178.o boards/a9746.o boards/18.o boards/ac-08.o boards/n625092.o boards/183.o boards/addrlatch.o boards/novel.o boards/185.o boards/ax5705.o boards/onebus.o boards/186.o boards/pec-586.o boards/187.o boards/bb.o boards/sa-9602b.o boards/189.o boards/bmc13in1jy110.o boards/193.o boards/bmc42in1r.o boards/sc-127.o boards/199.o boards/bmc64in1nr.o boards/sheroes.o boards/208.o boards/bmc70in1.o boards/sl1632.o boards/222.o boards/bonza.o boards/smb2j.o boards/225.o boards/bs-5.o boards/228.o boards/cityfighter.o boards/super24.o boards/230.o boards/dance2000.o boards/n106.o boards/supervision.o boards/232.o boards/t-227-1.o boards/234.o boards/deirom.o boards/t-262.o boards/sachen.o boards/235.o boards/dream.o boards/244.o boards/edu2000.o boards/tf-1201.o boards/bandai.o boards/246.o boards/famicombox.o boards/transformer.o boards/252.o boards/fk23c.o boards/vrc2and4.o boards/253.o boards/ghostbusters63in1.o boards/vrc7.o boards/28.o boards/gs-2004.o boards/yoko.o

# old-style, not converted
# boards/tengen.o

INPUTOBJECTS=input/arkanoid.o input/ftrainer.o input/oekakids.o input/suborkb.o input/bworld.o input/hypershot.o input/powerpad.o input/toprider.o input/cursor.o input/mahjong.o input/quiz.o input/zapper.o input/fkb.o input/shadow.o

FCEUOBJECTS=cart.o version.o emufile.o fceu.o fds.o file.o filter.o ines.o input.o palette.o sound.o state.o unif.o vsuni.o x6502.o git.o fc.o ppu.o

#  $(DRIVERS_COMMON_OBJECTS)
EMUOBJECTS=$(FCEUOBJECTS) $(MAPPEROBJECTS) $(UTILSOBJECTS) $(PALLETESOBJECTS) $(BOARDSOBJECTS) $(INPUTOBJECTS) $(GRAD_OBJECTS)

# included in all tests, etc.
BASEOBJECTS=$(CCLIBOBJECTS)

FCEULIB_OBJECTS=emulator.o headless-driver.o stringprintf.o trace.o tracing.o
# simplefm2.o emulator.o util.o

OBJECTS=$(BASEOBJECTS) $(EMUOBJECTS) $(FCEULIB_OBJECTS)

LFLAGS= $(ARCH) $(PLATFORMLINK) -lz $(OPT) $(FLTO) $(PROFILE) # -Wl,--subsystem,console
# -static -Wl,--subsystem,console
# -Wl,--subsystem,console
# -static -fwhole-program
# -static

emulator_test.exe : $(OBJECTS) test-util.o emulator_test.o simplefm2.o simplefm7.o
	$(CXX) $^ -o $@ $(LFLAGS)

bench.exe : $(OBJECTS) test-util.o bench.o simplefm2.o simplefm7.o
	$(CXX) $^ -o $@ $(LFLAGS)

test : emulator_test.exe
	time ./emulator_test.exe

clean :
	rm -f *_test.exe bench.exe *.o $(EMUOBJECTS) $(CCLIBOBJECTS) gmon.out

veryclean : clean
	rm -f trace.bin
