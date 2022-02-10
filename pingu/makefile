
all : pingu.so tetru.so

CC_LIB=../cc-lib
FCEULIB=../fceulib

CXXFLAGS=-Wall -Werror --std=c++17 -O -I $(CC_LIB) -I ../../nbdkit/include -I ../../nbdkit/common/include -I ../../nbdkit/common/allocators -I ../../nbdkit/common/utils -I $(FCEULIB) -I tetris/ -fPIC -DPSS_STYLE=2

CC_LIB_OBJECTS=$(CC_LIB)/arcfour.o $(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o

FCEULIB_OBJECTS=$(FCEULIB)/mappers/6.o $(FCEULIB)/mappers/61.o $(FCEULIB)/mappers/24and26.o $(FCEULIB)/mappers/51.o $(FCEULIB)/mappers/69.o $(FCEULIB)/mappers/77.o $(FCEULIB)/mappers/40.o $(FCEULIB)/mappers/mmc2and4.o $(FCEULIB)/mappers/71.o $(FCEULIB)/mappers/79.o $(FCEULIB)/mappers/41.o $(FCEULIB)/mappers/72.o $(FCEULIB)/mappers/80.o $(FCEULIB)/mappers/42.o $(FCEULIB)/mappers/62.o $(FCEULIB)/mappers/73.o $(FCEULIB)/mappers/85.o $(FCEULIB)/mappers/emu2413.o $(FCEULIB)/mappers/46.o $(FCEULIB)/mappers/65.o $(FCEULIB)/mappers/75.o $(FCEULIB)/mappers/50.o $(FCEULIB)/mappers/67.o $(FCEULIB)/mappers/76.o $(FCEULIB)/mappers/tengen.o $(FCEULIB)/utils/memory.o $(FCEULIB)/utils/crc32.o $(FCEULIB)/utils/endian.o $(FCEULIB)/utils/md5.o $(FCEULIB)/utils/xstring.o $(FCEULIB)/boards/mmc1.o $(FCEULIB)/boards/mmc5.o $(FCEULIB)/boards/datalatch.o $(FCEULIB)/boards/mmc3.o $(FCEULIB)/boards/01-222.o $(FCEULIB)/boards/32.o $(FCEULIB)/boards/gs-2013.o $(FCEULIB)/boards/103.o $(FCEULIB)/boards/33.o $(FCEULIB)/boards/h2288.o $(FCEULIB)/boards/106.o $(FCEULIB)/boards/34.o $(FCEULIB)/boards/karaoke.o $(FCEULIB)/boards/108.o $(FCEULIB)/boards/3d-block.o $(FCEULIB)/boards/kof97.o $(FCEULIB)/boards/112.o $(FCEULIB)/boards/411120-c.o $(FCEULIB)/boards/konami-qtai.o $(FCEULIB)/boards/116.o $(FCEULIB)/boards/43.o $(FCEULIB)/boards/ks7012.o $(FCEULIB)/boards/117.o $(FCEULIB)/boards/57.o $(FCEULIB)/boards/ks7013.o $(FCEULIB)/boards/120.o $(FCEULIB)/boards/603-5052.o $(FCEULIB)/boards/ks7017.o $(FCEULIB)/boards/121.o $(FCEULIB)/boards/68.o $(FCEULIB)/boards/ks7030.o $(FCEULIB)/boards/12in1.o $(FCEULIB)/boards/8157.o $(FCEULIB)/boards/ks7031.o $(FCEULIB)/boards/15.o $(FCEULIB)/boards/82.o $(FCEULIB)/boards/ks7032.o $(FCEULIB)/boards/151.o $(FCEULIB)/boards/8237.o $(FCEULIB)/boards/ks7037.o $(FCEULIB)/boards/156.o $(FCEULIB)/boards/830118c.o $(FCEULIB)/boards/ks7057.o $(FCEULIB)/boards/164.o $(FCEULIB)/boards/88.o $(FCEULIB)/boards/le05.o $(FCEULIB)/boards/168.o $(FCEULIB)/boards/90.o $(FCEULIB)/boards/lh32.o $(FCEULIB)/boards/17.o $(FCEULIB)/boards/91.o $(FCEULIB)/boards/lh53.o $(FCEULIB)/boards/170.o $(FCEULIB)/boards/95.o $(FCEULIB)/boards/malee.o $(FCEULIB)/boards/175.o $(FCEULIB)/boards/96.o $(FCEULIB)/boards/176.o $(FCEULIB)/boards/99.o $(FCEULIB)/boards/177.o $(FCEULIB)/boards/178.o $(FCEULIB)/boards/a9746.o $(FCEULIB)/boards/18.o $(FCEULIB)/boards/ac-08.o $(FCEULIB)/boards/n625092.o $(FCEULIB)/boards/183.o $(FCEULIB)/boards/addrlatch.o $(FCEULIB)/boards/novel.o $(FCEULIB)/boards/185.o $(FCEULIB)/boards/ax5705.o $(FCEULIB)/boards/onebus.o $(FCEULIB)/boards/186.o $(FCEULIB)/boards/pec-586.o $(FCEULIB)/boards/187.o $(FCEULIB)/boards/bb.o $(FCEULIB)/boards/sa-9602b.o $(FCEULIB)/boards/189.o $(FCEULIB)/boards/bmc13in1jy110.o $(FCEULIB)/boards/193.o $(FCEULIB)/boards/bmc42in1r.o $(FCEULIB)/boards/sc-127.o $(FCEULIB)/boards/199.o $(FCEULIB)/boards/bmc64in1nr.o $(FCEULIB)/boards/sheroes.o $(FCEULIB)/boards/208.o $(FCEULIB)/boards/bmc70in1.o $(FCEULIB)/boards/sl1632.o $(FCEULIB)/boards/222.o $(FCEULIB)/boards/bonza.o $(FCEULIB)/boards/smb2j.o $(FCEULIB)/boards/225.o $(FCEULIB)/boards/bs-5.o $(FCEULIB)/boards/228.o $(FCEULIB)/boards/cityfighter.o $(FCEULIB)/boards/super24.o $(FCEULIB)/boards/230.o $(FCEULIB)/boards/dance2000.o $(FCEULIB)/boards/n106.o $(FCEULIB)/boards/supervision.o $(FCEULIB)/boards/232.o $(FCEULIB)/boards/t-227-1.o $(FCEULIB)/boards/234.o $(FCEULIB)/boards/deirom.o $(FCEULIB)/boards/t-262.o $(FCEULIB)/boards/sachen.o $(FCEULIB)/boards/235.o $(FCEULIB)/boards/dream.o $(FCEULIB)/boards/244.o $(FCEULIB)/boards/edu2000.o $(FCEULIB)/boards/tf-1201.o $(FCEULIB)/boards/bandai.o $(FCEULIB)/boards/246.o $(FCEULIB)/boards/famicombox.o $(FCEULIB)/boards/transformer.o $(FCEULIB)/boards/252.o $(FCEULIB)/boards/fk23c.o $(FCEULIB)/boards/vrc2and4.o $(FCEULIB)/boards/253.o $(FCEULIB)/boards/ghostbusters63in1.o $(FCEULIB)/boards/vrc7.o $(FCEULIB)/boards/28.o $(FCEULIB)/boards/gs-2004.o $(FCEULIB)/boards/yoko.o $(FCEULIB)/input/arkanoid.o $(FCEULIB)/input/ftrainer.o $(FCEULIB)/input/oekakids.o $(FCEULIB)/input/suborkb.o $(FCEULIB)/input/bworld.o $(FCEULIB)/input/hypershot.o $(FCEULIB)/input/powerpad.o $(FCEULIB)/input/toprider.o $(FCEULIB)/input/cursor.o $(FCEULIB)/input/mahjong.o $(FCEULIB)/input/quiz.o $(FCEULIB)/input/zapper.o $(FCEULIB)/input/fkb.o $(FCEULIB)/input/shadow.o $(FCEULIB)/cart.o $(FCEULIB)/version.o $(FCEULIB)/emufile.o $(FCEULIB)/fceu.o $(FCEULIB)/fds.o $(FCEULIB)/file.o $(FCEULIB)/filter.o $(FCEULIB)/ines.o $(FCEULIB)/input.o $(FCEULIB)/palette.o $(FCEULIB)/ppu.o $(FCEULIB)/sound.o $(FCEULIB)/state.o $(FCEULIB)/unif.o $(FCEULIB)/vsuni.o $(FCEULIB)/x6502.o $(FCEULIB)/git.o $(FCEULIB)/fc.o $(FCEULIB)/emulator.o $(FCEULIB)/headless-driver.o $(FCEULIB)/simplefm2.o $(FCEULIB)/simplefm7.o $(FCEULIB)/stringprintf.o

TETRIS_OBJECTS=tetris/encoding.o tetris/movie-maker.o

%.o : %.cc
	g++ $(CXXFLAGS) -c $^ -o $@

pingu.so : pingu.o $(CC_LIB_OBJECTS)
	g++ $(CXXFLAGS) -shared -lpthread $^ -o $@

tetru.so : tetru.o $(CC_LIB_OBJECTS) $(TETRIS_OBJECTS) $(FCEULIB_OBJECTS)

netutil_test.exe : netutil.o netutil_test.o $(CC_LIB)/base/logging.o
	g++ -lpthread $^ -o $@

clean:
	rm -f *.so *.o

veryclean: clean
	rm -f *~ $(CC_LIB_OBJECTS) $(TETRIS_OBJECTS) $(FCEULIB_OBJECTS)

