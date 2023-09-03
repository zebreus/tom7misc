#
#    This file is part of Alpertron Calculators.
#
#    Copyright 2018 Dario Alejandro Alpern
#
#    Alpertron Calculators is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    Alpertron Calculators is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.
#
#    Use flags_coverage=--coverage in make command line to compile
#    with coverage support.

CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++

CXXFLAGS=-g -O -march=native
LFLAGS=-static

flags_factorization_1=-D_USING64BITS_ -DFACTORIZATION_APP=1 -DFACTORIZATION_FUNCTIONS=1 -c
flags_squares_1=-D_USING64BITS_ -c
flags_other_1=-D_USING64BITS_ -c
ifeq ($(flags_coverage), )
flags_factorization=$(flags_factorization_1) $(CXXFLAGS)
flags_squares=$(flags_squares_1) $(CXXFLAGS)
flags_other=$(flags_other_1) $(CXXFLAGS)
else
flags_factorization=$(flags_factorization_1) $(flags_coverage) -g $(CXXFLAGS)
flags_squares=$(flags_squares_1) $(flags_coverage) -g $(CXXFLAGS)
flags_other=$(flags_other_1) $(flags_coverage) -g $(CXXFLAGS)
endif
h_files=batch.h bignbr.h commonstruc.h expression.h factor.h highlevel.h polynomial.h showtime.h skiptest.h
targets = ecm.exe quad.exe quadmod.exe fsquares.exe fcubes.exe polfact.exe dilog.exe contfrac.exe blockly.exe tsqcubes.exe sumquad.exe divisors.exe isprime.exe
.PHONY : all
all: $(targets)

ecm.exe: expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco ecmfront.fco sumSquares.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco batch.fco fft.fco gcdrings.fco fromBlockly.fco linkedbignbr.fco test_ecm.fco
	$(CXX) expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco ecmfront.fco sumSquares.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco batch.fco fft.fco gcdrings.fco fromBlockly.fco linkedbignbr.fco test_ecm.fco $(flags_coverage) $(LFLAGS) -lm -o $@

blockly.exe: expression.fbo parseexpr.fbo partition.fbo errors.fbo copyStr.fbo bigint.fbo division.fbo baseconv.fbo karatsuba.fbo modmult.fbo sqroot.fbo \
factor.fbo ecm.fbo siqs.fbo siqsLA.fbo ecmfront.fbo sumSquares.fco bignbr.fbo showtime.fbo from_musl.fbo inputstr.fbo batch.fbo fft.fbo gcdrings.fbo fromBlockly.fbo linkedbignbr.fbo test_blockly.fbo
	$(CXX) expression.fbo parseexpr.fbo partition.fbo errors.fbo copyStr.fbo bigint.fbo division.fbo baseconv.fbo karatsuba.fbo modmult.fbo sqroot.fbo \
factor.fbo ecm.fbo siqs.fbo siqsLA.fbo ecmfront.fbo sumSquares.fco bignbr.fbo showtime.fbo from_musl.fbo inputstr.fbo batch.fbo fft.fbo gcdrings.fbo fromBlockly.fbo linkedbignbr.fbo test_blockly.fbo $(flags_coverage) $(LFLAGS) -lm -o $@

sumquad.exe: expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco ecmfront.fco sumSquares.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco batch.fco fft.fco gcdrings.fco fromBlockly.fco linkedbignbr.fco test_sumquad.fco
	$(CXX) expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco ecmfront.fco sumSquares.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco batch.fco fft.fco gcdrings.fco fromBlockly.fco linkedbignbr.fco test_sumquad.fco $(flags_coverage) $(LFLAGS) -lm -o $@

divisors.exe: expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco ecmfront.fco sumSquares.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco batch.fco fft.fco gcdrings.fco fromBlockly.fco linkedbignbr.fco test_divisors.fco
	$(CXX) expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco ecmfront.fco sumSquares.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco batch.fco fft.fco gcdrings.fco fromBlockly.fco linkedbignbr.fco test_divisors.fco $(flags_coverage) $(LFLAGS) -lm -o $@

gaussian.exe: parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco gaussian.fco GaussExpr.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco gcdrings.fco test_gaussian.fco
	$(CXX) parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco gaussian.fco GaussExpr.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco gcdrings.fco test_gaussian.fco $(flags_coverage) $(LFLAGS) -lm -o $@

quad.exe: expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco quad.fco quadmodLL.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco test_quad.fco
	$(CXX) expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco quad.fco quadmodLL.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco test_quad.fco $(flags_coverage) $(LFLAGS) -lm -o $@

dilog.exe: expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco dilog.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco test_dilog.fco
	$(CXX) expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco dilog.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco test_dilog.fco $(flags_coverage) $(LFLAGS) -lm -o $@

quadmod.exe: expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco quadmod.fco quadmodLL.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco test_quadmod.fco
	$(CXX) expression.fco parseexpr.fco partition.fco errors.fco copyStr.fco bigint.fco division.fco baseconv.fco karatsuba.fco modmult.fco sqroot.fco \
factor.fco ecm.fco siqs.fco siqsLA.fco quadmod.fco quadmodLL.fco bignbr.fco showtime.fco from_musl.fco inputstr.fco fft.fco test_quadmod.fco $(flags_coverage) $(LFLAGS) -lm -o $@

fsquares.exe: expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
fsquares.sqo tsquares.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_fsquares.sqo
	$(CXX) expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
fsquares.sqo tsquares.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_fsquares.sqo $(flags_coverage) $(LFLAGS) -lm -o $@

tsqcubes.exe: expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
tsqcubes.sqo tsquares.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_tsqcubes.sqo
	$(CXX) expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
tsqcubes.sqo tsquares.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_tsqcubes.sqo $(flags_coverage) $(LFLAGS) -lm -o $@

fcubes.exe: expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
fcubes.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_fcubes.sqo
	$(CXX) expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
fcubes.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_fcubes.sqo $(flags_coverage) $(LFLAGS) -lm -o $@

isprime.exe: isprime.sqo test_isprime.sqo
	$(CXX) isprime.sqo test_isprime.sqo $(flags_coverage) $(LFLAGS) -lm -o $@

contfrac.exe: expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
contfrac.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_contfrac.sqo
	$(CXX) expression.sqo parseexpr.sqo partition.sqo errors.sqo copyStr.sqo bigint.sqo division.sqo baseconv.sqo karatsuba.sqo modmult.sqo sqroot.sqo \
contfrac.sqo bignbr.sqo showtime.sqo from_musl.sqo inputstr.sqo batch.sqo fft.sqo gcdrings.sqo test_contfrac.sqo $(flags_coverage) $(LFLAGS) -lm -o $@

polfact.exe: expression.oto parseexpr.plo partition.oto errors.oto bigint.oto linkedbignbr.oto division.oto baseconv.oto karatsuba.oto modmult.oto sqroot.oto \
rootseq.oto quartics.oto quintics.oto quinticsData.oto bigrational.oto output.oto copyStr.oto polynomial.oto polyexpr.oto multpoly.oto divpoly.oto fftpoly.oto polfact.oto bignbr.oto showtime.oto from_musl.oto inputstr.oto fft.oto test_polfact.oto intpolfact.oto
	$(CXX) expression.oto parseexpr.plo partition.oto errors.oto bigint.oto linkedbignbr.oto division.oto baseconv.oto karatsuba.oto modmult.oto sqroot.oto \
rootseq.oto quartics.oto quintics.oto quinticsData.oto bigrational.oto output.oto copyStr.oto polynomial.oto polyexpr.oto multpoly.oto divpoly.oto fftpoly.oto polfact.oto bignbr.oto showtime.oto from_musl.oto inputstr.oto fft.oto test_polfact.oto intpolfact.oto $(flags_coverage) $(LFLAGS) -lm -o $@

%.fco : %.c $(h_files)
	$(CXX) $(flags_factorization) -o $@ $<

%.fbo : %.c $(h_files)
	$(CXX) $(flags_factorization) -DUSING_BLOCKLY=1 -o $@ $<

%.oto : %.c $(h_files)
	$(CXX) $(flags_other) -o $@ $<

%.plo : %.c $(h_files)
	$(CXX) -DPOLYEXPR=1 $(flags_other) -o $@ $<

%.sqo : %.c $(h_files)
	$(CXX) $(flags_squares) -o $@ $<

test_ecm.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=13 -o $@ $<

test_blockly.fbo: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=22 -DUSING_BLOCKLY=1 -o $@ $<

test_sumquad.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=25 -o $@ $<

test_divisors.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=27 -o $@ $<

test_gaussian.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=12 -o $@ $<

test_quadmod.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=16 -o $@ $<

test_quad.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=17 -o $@ $<

test_dilog.fco: test.c $(h_files)
	$(CXX) $(flags_factorization) -DDEBUG_CODE=11 -o $@ $<

test_fsquares.sqo: test.c $(h_files)
	$(CXX) $(flags_squares) -DDEBUG_CODE=1 -o $@ $<

test_tsqcubes.sqo: test.c $(h_files)
	$(CXX) $(flags_squares) -DDEBUG_CODE=23 -o $@ $<

test_fcubes.sqo: test.c $(h_files)
	$(CXX) $(flags_squares) -DDEBUG_CODE=2 -o $@ $<

test_contfrac.sqo: test.c $(h_files)
	$(CXX) $(flags_squares) -DDEBUG_CODE=6 -o $@ $<

test_polfact.oto: test.c $(h_files)
	$(CXX) $(flags_other) -DDEBUG_CODE=9 -o $@ $<

test_isprime.sqo: test.c $(h_files)
	$(CXX) $(flags_other) -DDEBUG_CODE=28 -o $@ $<

clean:
	rm -f *.oto *.fco *.fbo *.sqo *.plo *.gcda *.gcno *.gcov $(targets)

