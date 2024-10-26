
#ifndef _MARIO_H
#define _MARIO_H

inline constexpr int FRAME_COUNTER = 0x0009;

inline constexpr int NUMBER_OF_LIVES = 0x075a;
inline constexpr int COIN_TALLY = 0x075e;


// e.g. 8-2 is major 8 (0x07) and minor 2 (0x01).
// Setting these on the title will begin the game at that level.
inline constexpr int WORLD_MAJOR = 0x075f;
inline constexpr int WORLD_MINOR = 0x0760;
// On-screen display of the minor world. Might be purely cosmetic.
// Note that in the game there are some transition levels where
// Mario automatically goes down a pipe, like at the beginning
// of 1-2. These are actually different minor worlds: The transition
// is the real 1-2 (maj=0, min=1) and then the underground world
// is actually 1-3 (maj=0, min=2). That's what this byte seems to
// be for: It doesn't get incremented during the transition, so
// that both display as 1-2 (and the real 1-4 shows as 1-3, etc.).
inline constexpr int WORLD_MINOR_DISPLAY = 0x075c;
// This sets the screen (within the level) we continue from after dying.
inline constexpr int HALFWAY_PAGE = 0x075b;

inline constexpr int SWIMMING_FLAG = 0x0704;

inline constexpr int OPER_MODE = 0x0770;
// mode:task is 1:3 when playing (or dying)
//              1:1 on the pre-level loading screen
//              2:0 when the bridge is collapsing
//        then  2:1 briefly
//        then  2:2 when walking to princess
//        then  2:3 as message reveals
//              2:4 when win screen is fully displayed
inline constexpr int OPER_MODE_TASK = 0x0772;

// 0 blank between screen loads?
// 2 when entering horiz pipe
// 3 when going down pipe
// 4 sliding down flagpole
// 5 walking to exit, time countdown
// 7 when coming out of vertical pipe, or interstitial
// 8 during normal play, or dying
// 9 when getting big
// 10 when getting small
// 11 when dying from damage (but not from falling into pit)
// 12 when becoming fire mario
inline constexpr int GAME_ENGINE_SUBROUTINE = 0x000e;

// Determines whether player entrance (subroutine 7) is using
// pipe/vine (0x02) or "normal".
inline constexpr int ALT_ENTRANCE_CONTROL = 0x0752;

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

// At least the first two bytes are LFSR-based PRNG, but the five
// bytes after that also change every frame.
inline constexpr int LFSR_PRNG = 0x07a7;

// Timer displayed on screen. Each byte is a decimal
// digit 0-9, where TIMER1 is the most significant digit.
//
// The player dies when this reaches 000.
inline constexpr int TIMER1 = 0x07f8;
inline constexpr int TIMER2 = 0x07f9;
inline constexpr int TIMER3 = 0x07fa;

// Warm boot detection requires this to be 0xA5.
inline constexpr int WARM_BOOT_VALIDATION = 0x07ff;
// Five digits of the top score. Warm boot detection
// checks for these all being less than 0x0A.
inline constexpr int TOP_SCORE_DISPLAY = 0x07d7;

// The controller bits processed by the player movement
// routine. If the player is autocontrolled, then this
// is the input from that (not the joystick).
inline constexpr int LAST_JOYPAD = 0x06fc;

#endif
