/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*        Various macros for faster memory stuff
                (at least that's the idea)
*/

#ifndef _FCEULIB_MEMORY_H
#define _FCEULIB_MEMORY_H

#include <cstdlib>
#include <cstdint>

// Like memset, but filling with 32-bit quantities in host byte
// order. Looks like this assumes n_bytes is a multiple of 4.
inline void FCEU_dwmemset(void *dest, uint32_t c, int n_bytes) {
  const int nn = n_bytes >> 2;
  uint32_t *dest32 = (uint32_t *)dest;
  for (int i = nn - 1; i >= 0; i--) {
    dest32[i] = c;
  }
}

// Like malloc, but zeroes.
void *FCEU_malloc(size_t size);

#endif
