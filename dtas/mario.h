
#ifndef _MARIO_H
#define _MARIO_H

inline constexpr int NUMBER_OF_LIVES = 0x075a;
inline constexpr int COIN_TALLY = 0x075e;
inline constexpr int HALFWAY_PAGE = 0x075b;
// cosmetic?
inline constexpr int LEVEL_NUMBER = 0x075c;
inline constexpr int AREA_NUMBER = 0x0760;
inline constexpr int WORLD_NUMBER = 0x075f;
inline constexpr int SWIMMING_FLAG = 0x0704;

inline constexpr int OPER_MODE = 0x0770;
inline constexpr int OPER_MODE_TASK = 0x0772;

// Pixels from top of screen; y increases downward.
// Coordinates are of mario's top-left pixel (when
// big, at least)
inline constexpr int PLAYER_Y = 0x00CE;
// 0 if above screen, 1 if on screen, 2+ if below
inline constexpr int PLAYER_Y_SCREEN = 0x00B5;

// Position within the level.
inline constexpr int PLAYER_X_HI = 0x006D;
inline constexpr int PLAYER_X_LO = 0x0086;

// Ranges from 0-31. Column of block to fill in the blockbuffer next.
inline constexpr int BLOCKBUFFER_COLUMN_POS = 0x6A0;
// I think this is the column within the page currently being decoded.
// It ranges from 0-15 and is BLOCKBUFFER_COLUMN_POS % 16 in the
// steady state.
inline constexpr int CURRENT_COLUMN_POS = 0x0726;

// Pixel scroll position of the screen.
inline constexpr int HORIZONTAL_SCROLL = 0x073F;
// Pixel scroll mod 32.
inline constexpr int HORIZONTAL_SCROLL_32 = 0x073D;

// Basically the same as the horizontal scroll. Gives the position of the
// screen within the global level coordinates. Also determines the starting
// position (of the left edge of the screen) within the ring buffer.
inline constexpr int SCREENLEFT_X_LO = 0x071C;
inline constexpr int SCREENLEFT_X_HI = 0x071A;

// 0x500-0x6A0
// Ring buffer containing level data ("block buffer")
// Two screens wide (16 blocks across, 13 high).

#endif
