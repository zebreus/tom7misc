//
// This file is part of Alpertron Calculators.
//
// Copyright 2019-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.

#ifndef _GLOBALS_H
#define _GLOBALS_H

#include "bignbr.h"

// extern char output[3000000];
extern char *output;
extern bool lang;

extern bool hexadecimal;

extern limb *Mult1;
extern limb *Mult2;
extern limb *Mult3;
extern limb *Mult4;
extern int *valueQ;

// void showText(const char* text);
// void shownbr(const BigInteger* value);
void beginLine(char** pptrOutput);
void finishLine(char** pptrOutput);

#endif
