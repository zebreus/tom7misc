default : hardiness.exe dangerzones.exe stochastic.exe bootdata.exe solve.exe minus-results.exe minus-validate.exe minus-map.exe minus-query.exe asm7.exe

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  # XXX necessary?
  PLATFORM_CXXFLAGS=-D__MINGW32__
  PLATFORM_LFLAGS=-lpsapi -Wl,--subsystem,console -static
  CL_CFLAGS="-I$(AMDSDK)/include"
  CL_LFLAGS='-L${AMDSDK}/lib/${AMD_ARCH}' -lOpenCL
else
  # Linux
  CXX=g++
  CC=gcc

  # Check if g++ is actually clang
  ifeq ($(shell $(CXX) -v 2>&1 | grep "clang version" | wc -l), 1)
    CLANG=true
    PLATFORM_LDFLAGS += -Wl,--stack,167772160
  else
    # g++ says it supports --size but it doesn't work
    PLATFORM_LDFLAGS += -Wl,-z,stack-size=167772160
  endif
endif

CC_LIB=../cc-lib
SQLITE=../sqlite
CODEC=../codec

OPT=-O2

# ASAN=-g -fsanitize=address
# OPT=-O

# -fno-strict-aliasing

AOT=-DAOT_INSTRUMENTATION=1

CXXFLAGS=$(ASAN) $(OPT) $(AOT) $(PLATFORM_CFLAGS) -DDISABLE_SOUND=1 -DDUMMY_UI -march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare $(SDLINCLUDES) -I. -I$(CC_LIB) -I$(CC_LIB) -I$(SQLITE) --std=c++20
LFLAGS=$(ASAN) $(PLATFORM_CFLAGS) $(PLATFORM_LDFLAGS) -lz

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(RE2_OBJECTS) $(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/ansi.o $(CC_LIB)/csv.o $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/ansi-image.o $(CC_LIB)/image-resize.o

CODEC_OBJECTS=$(CODEC)/mov.o

SQLITE_OBJECTS=$(SQLITE)/sqlite3.o $(SQLITE)/database.o

FCEULIB=../fceulib
FCEULIB_OBJECTS=$(FCEULIB)/mappers/6.o $(FCEULIB)/mappers/61.o $(FCEULIB)/mappers/24and26.o $(FCEULIB)/mappers/51.o $(FCEULIB)/mappers/69.o $(FCEULIB)/mappers/77.o $(FCEULIB)/mappers/40.o $(FCEULIB)/mappers/mmc2and4.o $(FCEULIB)/mappers/71.o $(FCEULIB)/mappers/79.o $(FCEULIB)/mappers/41.o $(FCEULIB)/mappers/72.o $(FCEULIB)/mappers/80.o $(FCEULIB)/mappers/42.o $(FCEULIB)/mappers/62.o $(FCEULIB)/mappers/73.o $(FCEULIB)/mappers/85.o $(FCEULIB)/mappers/emu2413.o $(FCEULIB)/mappers/46.o $(FCEULIB)/mappers/65.o $(FCEULIB)/mappers/75.o $(FCEULIB)/mappers/50.o $(FCEULIB)/mappers/67.o $(FCEULIB)/mappers/76.o $(FCEULIB)/mappers/tengen.o $(FCEULIB)/utils/memory.o $(FCEULIB)/utils/crc32.o $(FCEULIB)/utils/endian.o $(FCEULIB)/utils/md5.o $(FCEULIB)/utils/xstring.o $(FCEULIB)/boards/mmc1.o $(FCEULIB)/boards/mmc5.o $(FCEULIB)/boards/datalatch.o $(FCEULIB)/boards/mmc3.o $(FCEULIB)/boards/01-222.o $(FCEULIB)/boards/32.o $(FCEULIB)/boards/gs-2013.o $(FCEULIB)/boards/103.o $(FCEULIB)/boards/33.o $(FCEULIB)/boards/h2288.o $(FCEULIB)/boards/106.o $(FCEULIB)/boards/34.o $(FCEULIB)/boards/karaoke.o $(FCEULIB)/boards/108.o $(FCEULIB)/boards/3d-block.o $(FCEULIB)/boards/kof97.o $(FCEULIB)/boards/112.o $(FCEULIB)/boards/411120-c.o $(FCEULIB)/boards/konami-qtai.o $(FCEULIB)/boards/116.o $(FCEULIB)/boards/43.o $(FCEULIB)/boards/ks7012.o $(FCEULIB)/boards/117.o $(FCEULIB)/boards/57.o $(FCEULIB)/boards/ks7013.o $(FCEULIB)/boards/120.o $(FCEULIB)/boards/603-5052.o $(FCEULIB)/boards/ks7017.o $(FCEULIB)/boards/121.o $(FCEULIB)/boards/68.o $(FCEULIB)/boards/ks7030.o $(FCEULIB)/boards/12in1.o $(FCEULIB)/boards/8157.o $(FCEULIB)/boards/ks7031.o $(FCEULIB)/boards/15.o $(FCEULIB)/boards/82.o $(FCEULIB)/boards/ks7032.o $(FCEULIB)/boards/151.o $(FCEULIB)/boards/8237.o $(FCEULIB)/boards/ks7037.o $(FCEULIB)/boards/156.o $(FCEULIB)/boards/830118c.o $(FCEULIB)/boards/ks7057.o $(FCEULIB)/boards/164.o $(FCEULIB)/boards/88.o $(FCEULIB)/boards/le05.o $(FCEULIB)/boards/168.o $(FCEULIB)/boards/90.o $(FCEULIB)/boards/lh32.o $(FCEULIB)/boards/17.o $(FCEULIB)/boards/91.o $(FCEULIB)/boards/lh53.o $(FCEULIB)/boards/170.o $(FCEULIB)/boards/95.o $(FCEULIB)/boards/malee.o $(FCEULIB)/boards/175.o $(FCEULIB)/boards/96.o $(FCEULIB)/boards/176.o $(FCEULIB)/boards/99.o $(FCEULIB)/boards/177.o $(FCEULIB)/boards/178.o $(FCEULIB)/boards/a9746.o $(FCEULIB)/boards/18.o $(FCEULIB)/boards/ac-08.o $(FCEULIB)/boards/n625092.o $(FCEULIB)/boards/183.o $(FCEULIB)/boards/addrlatch.o $(FCEULIB)/boards/novel.o $(FCEULIB)/boards/185.o $(FCEULIB)/boards/ax5705.o $(FCEULIB)/boards/onebus.o $(FCEULIB)/boards/186.o $(FCEULIB)/boards/pec-586.o $(FCEULIB)/boards/187.o $(FCEULIB)/boards/bb.o $(FCEULIB)/boards/sa-9602b.o $(FCEULIB)/boards/189.o $(FCEULIB)/boards/bmc13in1jy110.o $(FCEULIB)/boards/193.o $(FCEULIB)/boards/bmc42in1r.o $(FCEULIB)/boards/sc-127.o $(FCEULIB)/boards/199.o $(FCEULIB)/boards/bmc64in1nr.o $(FCEULIB)/boards/sheroes.o $(FCEULIB)/boards/208.o $(FCEULIB)/boards/bmc70in1.o $(FCEULIB)/boards/sl1632.o $(FCEULIB)/boards/222.o $(FCEULIB)/boards/bonza.o $(FCEULIB)/boards/smb2j.o $(FCEULIB)/boards/225.o $(FCEULIB)/boards/bs-5.o $(FCEULIB)/boards/228.o $(FCEULIB)/boards/cityfighter.o $(FCEULIB)/boards/super24.o $(FCEULIB)/boards/230.o $(FCEULIB)/boards/dance2000.o $(FCEULIB)/boards/n106.o $(FCEULIB)/boards/supervision.o $(FCEULIB)/boards/232.o $(FCEULIB)/boards/t-227-1.o $(FCEULIB)/boards/234.o $(FCEULIB)/boards/deirom.o $(FCEULIB)/boards/t-262.o $(FCEULIB)/boards/sachen.o $(FCEULIB)/boards/235.o $(FCEULIB)/boards/dream.o $(FCEULIB)/boards/244.o $(FCEULIB)/boards/edu2000.o $(FCEULIB)/boards/tf-1201.o $(FCEULIB)/boards/bandai.o $(FCEULIB)/boards/246.o $(FCEULIB)/boards/famicombox.o $(FCEULIB)/boards/transformer.o $(FCEULIB)/boards/252.o $(FCEULIB)/boards/fk23c.o $(FCEULIB)/boards/vrc2and4.o $(FCEULIB)/boards/253.o $(FCEULIB)/boards/ghostbusters63in1.o $(FCEULIB)/boards/vrc7.o $(FCEULIB)/boards/28.o $(FCEULIB)/boards/gs-2004.o $(FCEULIB)/boards/yoko.o $(FCEULIB)/input/arkanoid.o $(FCEULIB)/input/ftrainer.o $(FCEULIB)/input/oekakids.o $(FCEULIB)/input/suborkb.o $(FCEULIB)/input/bworld.o $(FCEULIB)/input/hypershot.o $(FCEULIB)/input/powerpad.o $(FCEULIB)/input/toprider.o $(FCEULIB)/input/cursor.o $(FCEULIB)/input/mahjong.o $(FCEULIB)/input/quiz.o $(FCEULIB)/input/zapper.o $(FCEULIB)/input/fkb.o $(FCEULIB)/input/shadow.o $(FCEULIB)/cart.o $(FCEULIB)/version.o $(FCEULIB)/emufile.o $(FCEULIB)/fceu.o $(FCEULIB)/fds.o $(FCEULIB)/file.o $(FCEULIB)/filter.o $(FCEULIB)/ines.o $(FCEULIB)/input.o $(FCEULIB)/palette.o $(FCEULIB)/ppu.o $(FCEULIB)/sound.o $(FCEULIB)/state.o $(FCEULIB)/unif.o $(FCEULIB)/vsuni.o $(FCEULIB)/x6502.o $(FCEULIB)/git.o $(FCEULIB)/fc.o $(FCEULIB)/emulator.o $(FCEULIB)/headless-driver.o $(FCEULIB)/simplefm2.o $(FCEULIB)/stringprintf.o $(FCEULIB)/simplefm7.o $(FCEULIB)/opcodes.o

# SQLITE is huge, so only link it selectively
OBJECTS=$(FCEULIB_OBJECTS) $(CC_LIB_OBJECTS)

$(FCEULIB)/%.o : $(FCEULIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(SQLITE)/%.o : $(SQLITE)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."


hardiness.exe : hardiness.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

dangerzones.exe : dangerzones.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

stochastic.exe : stochastic.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

bootdata.exe : bootdata.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

collage.exe : collage.o mario-util.o $(OBJECTS) $(SQLITE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

solve.exe : solve.o mario-util.o $(OBJECTS) $(SQLITE_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

minus-results.exe : minus-results.o mario-util.o $(OBJECTS) $(SQLITE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

minus-validate.exe : minus-validate.o mario-util.o $(OBJECTS) $(SQLITE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

minus-map.exe : minus-map.o mario-util.o $(OBJECTS) $(SQLITE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

minus-query.exe : minus-query.o $(OBJECTS) $(SQLITE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

diffemu.exe : diffemu.o mario-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

asm7.exe : asm7.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

genmodel.exe : genmodel.o mario-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

mario.rom : mario.asm asm7.exe
	./asm7.exe mario.asm $@

clean :
	rm -f *.o $(OBJECTS) $(SQLITE_OBJECTS)
