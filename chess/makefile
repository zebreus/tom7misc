
default: chess_test.exe tournament.exe longest.exe
all: chess_test.exe rungames.exe tournament.exe longest.exe pairs.exe chessmaster.exe makeperm.exe maketable.exe chessreduce.exe testplayer.exe elo.exe makecommonbook.exe packhisto.exe repack.exe validatepack.exe makealmanac.exe bigrat_test.exe

UNAME := $(shell uname -o)
ifeq ($(UNAME),Msys)
  # for 64 bits on windows (msys2).
  CXX=g++
  CC=gcc
  # FLTO=-flto=thin
  # I think PSS_STYLE may be dead?
  CXXFLAGS=-D__MINGW32__ -march=native -mtune=native -Wextra -Wno-unused-parameter -I. $(FLTO) -DPSS_STYLE=2 -DHAVE_ALLOCA -D_WIN32_WINNT=0x0602
  LFLAGS=-lwinpthread -lpsapi -L. -static libz.a -Wl,--subsystem,console -L.
else
  # Linux
  CXX=g++
  CC=gcc
  # no-psabi suppresses warnings all over STL in GCC 7.1 for ARM (e.g. pi)
  # it can probably be removed in a few years?
  CXXFLAGS=-march=native -mtune=native -Wno-psabi
  LFLAGS=-lz
endif

CC_LIB=../cc-lib
FCEULIB=../fceulib

INCLUDES=-I. -I $(CC_LIB) -I $(CC_LIB)/city -I $(FCEULIB)
CXXFLAGS+=-O3 --std=c++20 -Wno-deprecated -Wno-sign-compare -I. -Wall $(INCLUDES)

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

BIGNUM_OBJECTS=$(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o

CCLIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/city/city.o $(CC_LIB)/textsvg.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/image.o $(CC_LIB)/subprocess.o $(CC_LIB)/ansi.o $(CC_LIB)/nice.o $(BIGNUM_OBJECTS) $(RE2_OBJECTS)


FCEULIB_OBJECTS=$(FCEULIB)/mappers/6.o $(FCEULIB)/mappers/61.o $(FCEULIB)/mappers/24and26.o $(FCEULIB)/mappers/51.o $(FCEULIB)/mappers/69.o $(FCEULIB)/mappers/77.o $(FCEULIB)/mappers/40.o $(FCEULIB)/mappers/mmc2and4.o $(FCEULIB)/mappers/71.o $(FCEULIB)/mappers/79.o $(FCEULIB)/mappers/41.o $(FCEULIB)/mappers/72.o $(FCEULIB)/mappers/80.o $(FCEULIB)/mappers/42.o $(FCEULIB)/mappers/62.o $(FCEULIB)/mappers/73.o $(FCEULIB)/mappers/85.o $(FCEULIB)/mappers/emu2413.o $(FCEULIB)/mappers/46.o $(FCEULIB)/mappers/65.o $(FCEULIB)/mappers/75.o $(FCEULIB)/mappers/50.o $(FCEULIB)/mappers/67.o $(FCEULIB)/mappers/76.o $(FCEULIB)/mappers/tengen.o $(FCEULIB)/utils/memory.o $(FCEULIB)/utils/crc32.o $(FCEULIB)/utils/endian.o $(FCEULIB)/utils/md5.o $(FCEULIB)/utils/xstring.o $(FCEULIB)/boards/mmc1.o $(FCEULIB)/boards/mmc5.o $(FCEULIB)/boards/datalatch.o $(FCEULIB)/boards/mmc3.o $(FCEULIB)/boards/01-222.o $(FCEULIB)/boards/32.o $(FCEULIB)/boards/gs-2013.o $(FCEULIB)/boards/103.o $(FCEULIB)/boards/33.o $(FCEULIB)/boards/h2288.o $(FCEULIB)/boards/106.o $(FCEULIB)/boards/34.o $(FCEULIB)/boards/karaoke.o $(FCEULIB)/boards/108.o $(FCEULIB)/boards/3d-block.o $(FCEULIB)/boards/kof97.o $(FCEULIB)/boards/112.o $(FCEULIB)/boards/411120-c.o $(FCEULIB)/boards/konami-qtai.o $(FCEULIB)/boards/116.o $(FCEULIB)/boards/43.o $(FCEULIB)/boards/ks7012.o $(FCEULIB)/boards/117.o $(FCEULIB)/boards/57.o $(FCEULIB)/boards/ks7013.o $(FCEULIB)/boards/120.o $(FCEULIB)/boards/603-5052.o $(FCEULIB)/boards/ks7017.o $(FCEULIB)/boards/121.o $(FCEULIB)/boards/68.o $(FCEULIB)/boards/ks7030.o $(FCEULIB)/boards/12in1.o $(FCEULIB)/boards/8157.o $(FCEULIB)/boards/ks7031.o $(FCEULIB)/boards/15.o $(FCEULIB)/boards/82.o $(FCEULIB)/boards/ks7032.o $(FCEULIB)/boards/151.o $(FCEULIB)/boards/8237.o $(FCEULIB)/boards/ks7037.o $(FCEULIB)/boards/156.o $(FCEULIB)/boards/830118c.o $(FCEULIB)/boards/ks7057.o $(FCEULIB)/boards/164.o $(FCEULIB)/boards/88.o $(FCEULIB)/boards/le05.o $(FCEULIB)/boards/168.o $(FCEULIB)/boards/90.o $(FCEULIB)/boards/lh32.o $(FCEULIB)/boards/17.o $(FCEULIB)/boards/91.o $(FCEULIB)/boards/lh53.o $(FCEULIB)/boards/170.o $(FCEULIB)/boards/95.o $(FCEULIB)/boards/malee.o $(FCEULIB)/boards/175.o $(FCEULIB)/boards/96.o $(FCEULIB)/boards/176.o $(FCEULIB)/boards/99.o $(FCEULIB)/boards/177.o $(FCEULIB)/boards/178.o $(FCEULIB)/boards/a9746.o $(FCEULIB)/boards/18.o $(FCEULIB)/boards/ac-08.o $(FCEULIB)/boards/n625092.o $(FCEULIB)/boards/183.o $(FCEULIB)/boards/addrlatch.o $(FCEULIB)/boards/novel.o $(FCEULIB)/boards/185.o $(FCEULIB)/boards/ax5705.o $(FCEULIB)/boards/onebus.o $(FCEULIB)/boards/186.o $(FCEULIB)/boards/pec-586.o $(FCEULIB)/boards/187.o $(FCEULIB)/boards/bb.o $(FCEULIB)/boards/sa-9602b.o $(FCEULIB)/boards/189.o $(FCEULIB)/boards/bmc13in1jy110.o $(FCEULIB)/boards/193.o $(FCEULIB)/boards/bmc42in1r.o $(FCEULIB)/boards/sc-127.o $(FCEULIB)/boards/199.o $(FCEULIB)/boards/bmc64in1nr.o $(FCEULIB)/boards/sheroes.o $(FCEULIB)/boards/208.o $(FCEULIB)/boards/bmc70in1.o $(FCEULIB)/boards/sl1632.o $(FCEULIB)/boards/222.o $(FCEULIB)/boards/bonza.o $(FCEULIB)/boards/smb2j.o $(FCEULIB)/boards/225.o $(FCEULIB)/boards/bs-5.o $(FCEULIB)/boards/228.o $(FCEULIB)/boards/cityfighter.o $(FCEULIB)/boards/super24.o $(FCEULIB)/boards/230.o $(FCEULIB)/boards/dance2000.o $(FCEULIB)/boards/n106.o $(FCEULIB)/boards/supervision.o $(FCEULIB)/boards/232.o $(FCEULIB)/boards/t-227-1.o $(FCEULIB)/boards/234.o $(FCEULIB)/boards/deirom.o $(FCEULIB)/boards/t-262.o $(FCEULIB)/boards/sachen.o $(FCEULIB)/boards/235.o $(FCEULIB)/boards/dream.o $(FCEULIB)/boards/244.o $(FCEULIB)/boards/edu2000.o $(FCEULIB)/boards/tf-1201.o $(FCEULIB)/boards/bandai.o $(FCEULIB)/boards/246.o $(FCEULIB)/boards/famicombox.o $(FCEULIB)/boards/transformer.o $(FCEULIB)/boards/252.o $(FCEULIB)/boards/fk23c.o $(FCEULIB)/boards/vrc2and4.o $(FCEULIB)/boards/253.o $(FCEULIB)/boards/ghostbusters63in1.o $(FCEULIB)/boards/vrc7.o $(FCEULIB)/boards/28.o $(FCEULIB)/boards/gs-2004.o $(FCEULIB)/boards/yoko.o $(FCEULIB)/input/arkanoid.o $(FCEULIB)/input/ftrainer.o $(FCEULIB)/input/oekakids.o $(FCEULIB)/input/suborkb.o $(FCEULIB)/input/bworld.o $(FCEULIB)/input/hypershot.o $(FCEULIB)/input/powerpad.o $(FCEULIB)/input/toprider.o $(FCEULIB)/input/cursor.o $(FCEULIB)/input/mahjong.o $(FCEULIB)/input/quiz.o $(FCEULIB)/input/zapper.o $(FCEULIB)/input/fkb.o $(FCEULIB)/input/shadow.o $(FCEULIB)/cart.o $(FCEULIB)/version.o $(FCEULIB)/emufile.o $(FCEULIB)/fceu.o $(FCEULIB)/fds.o $(FCEULIB)/file.o $(FCEULIB)/filter.o $(FCEULIB)/ines.o $(FCEULIB)/input.o $(FCEULIB)/palette.o $(FCEULIB)/ppu.o $(FCEULIB)/sound.o $(FCEULIB)/state.o $(FCEULIB)/unif.o $(FCEULIB)/vsuni.o $(FCEULIB)/x6502.o $(FCEULIB)/git.o $(FCEULIB)/fc.o $(FCEULIB)/emulator.o $(FCEULIB)/opcodes.o $(FCEULIB)/headless-driver.o $(FCEULIB)/simplefm2.o $(FCEULIB)/simplefm7.o

# to keep asm: -Wa,-aslh
%.o : %.cc *.h
	@$(CXX) $(CXXFLAGS) $(OPT) -c -o $@ $<
	@bash -c "echo -n '.'"

$(CC_LIB)/%.o : $(CC_LIB)/%.cc
	@$(CXX) $(CXXFLAGS) $(OPT) -c -o $@ $<
	@bash -c "echo -n '_'"

chess_test.exe : chess_test.o chess.o pgn.o pack.o packedgame.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

rungames.exe : rungames.o chess.o pgn.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

# fate-data.cc is generated by maketable, but that's not in the
# makefile because generating the stats is extremely expensive
# It can be easily excised.
chessreduce.exe : chessreduce.o chess.o pgn.o fate-data.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

makealmanac.exe : makealmanac.o chess.o pack.o packedgame.o pgn.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

validatepack.exe : validatepack.o chess.o pack.o packedgame.o pgn.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

repack.exe : repack.o chess.o pack.o packedgame.o pgn.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

packhisto.exe : packhisto.o chess.o pack.o packedgame.o pgn.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

makecommonbook.exe : makecommonbook.o common.o chess.o pack.o packedgame.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

maketable.exe : maketable.o chess.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

makeperm.exe : makeperm.o chess.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

tournament.exe : tournament-db.o tournament.o chess.o player.o almanac-player.o  packedgame.o pack.o common.o stockfish-player.o player-util.o uci-player.o stockfish.o chessmaster.o headless-graphics.o all-fate-data.o fate-player.o blind-player.o blind/unblinder.o blind/unblinder-mk0.o numeric-player.o letter-player.o eniac.o eniac-player.o nneval-player.o grad-player.o ../grad/network.o $(CCLIB_OBJECTS) $(FCEULIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

elo.exe : elo.o tournament-db.o chess.o headless-graphics.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

longest.exe : longest.o chess.o pgn.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

bench.exe : chess.o player.o bench.o player-util.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

chessmaster.exe : chess.o player.o player-util.o chessmaster.o chessmaster-main.o headless-graphics.o $(FCEULIB_OBJECTS) $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

bigrat_test.exe : bigrat_test.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

testplayer.exe : chess.o player.o player-util.o fate-player.o almanac-player.o pack.o packedgame.o common.o all-fate-data.o chessmaster.o testplayer.o headless-graphics.o blind-player.o stockfish.o blind/unblinder.o blind/unblinder-mk0.o numeric-player.o eniac-player.o eniac.o nneval-player.o grad-player.o ../grad/network.o $(FCEULIB_OBJECTS) $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

pairs.exe : chess.o pairs.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

genmoves.exe : chess.o genmoves.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

# XXX should not delete stockfish.exe
clean :
	rm -f *.o *.exe $(CCLIB_OBJECTS)
