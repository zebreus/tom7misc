
#define PPU_CTRL                ((unsigned char*)0x2000U)
#define PPU_MASK                ((unsigned char*)0x2001U)
#define PPU_STATUS              ((unsigned char*)0x2002U)
#define SCROLL                  ((unsigned char*)0x2005U)
#define PPU_ADDRESS             ((unsigned char*)0x2006U)
#define PPU_DATA                ((unsigned char*)0x2007U)

#define OAM_ADDRESS             ((unsigned char*)0x2003U)
#define OAM_DMA                 ((unsigned char*)0x4014U)

#define JOY1                    ((unsigned char*)0x4016U)
#define JOY2                    ((unsigned char*)0x4017U)

#define APU_STATUS              ((unsigned char*)0x4015U)
#define APU_PULSE1_ENV          ((unsigned char*)0x4000U)
#define APU_PULSE1_SWEEP        ((unsigned char*)0x4001U)
#define APU_PULSE1_TIMER        ((unsigned char*)0x4002U)
#define APU_PULSE1_LEN          ((unsigned char*)0x4003U)
#define APU_PULSE2_ENV          ((unsigned char*)0x4004U)
#define APU_PULSE2_SWEEP        ((unsigned char*)0x4005U)
#define APU_PULSE2_TIMER        ((unsigned char*)0x4006U)
#define APU_PULSE2_LEN          ((unsigned char*)0x4007U)
// TODO TRIANGLE/NOISE/DMC

#define MEM ((unsigned char*)0x0000)
#define MEM_SIZE 2048

// Note, this is backwards from simplefm2.
#define RIGHT    0x01
#define LEFT     0x02
#define DOWN     0x04
#define UP       0x08
#define START    0x10
#define SELECT   0x20
#define B_BUTTON 0x40
#define A_BUTTON 0x80

// Two writes to this memory-mapped register.
#define SET_PPU_ADDRESS(addr) \
  do { \
    *PPU_ADDRESS = ((addr) >> 8) & 0xff;        \
    *PPU_ADDRESS = (addr) & 0xff;               \
  } while (0)

#pragma bss-name(push, "ZEROPAGE")
unsigned char ignore;
// Incremented whenever NMI happens.
unsigned char client_nmi;

unsigned char joy1;
unsigned char joy2;
// XXX Would be nice for these two implementation details to just be
// defined in input.s...
unsigned char joy1test;
unsigned char joy2test;

unsigned char scroll_x;

// Must be in zeropage for inline assembly
unsigned char *MEM_LINE;

#pragma bss-name(pop)

#pragma bss-name(push, "OAM")
unsigned char SPRITES[256];
// OAM equals ram addresses 200-2ff
#pragma bss-name(pop)

// Globals
unsigned char index;
unsigned char sindex = 0;

unsigned char chunk_num = 0;

// Counter for demo
unsigned char counter;

unsigned char old_joy1, old_joy2;

unsigned char switch_bank = 0;
unsigned char music_on = 1;

// input.s
void GetInput();
// dma.s
void DoDMA();

const unsigned char PALETTE[] = {
  // purple
  // 0x1f, 0x03, 0x13, 0x23,
  0x1f, 0x04, 0x14, 0x24,
  // red
  0x1f, 0x06, 0x16, 0x26,
  // green
  0x1f, 0x0a, 0x1a, 0x2a,
  // blue
  0x1f, 0x01, 0x11, 0x21,
  // sprite palette is just for debugging; these should
  // not be visible in normal operation
  0x1f, 0x21, 0x22, 0x23,
  0x1f, 0x24, 0x25, 0x26,
  0x1f, 0x27, 0x28, 0x29,
  0x1f, 0x2a, 0x2b, 0x2c,
};

unsigned char jiggleframe;

void main() {
  // turn off the screen
  *PPU_CTRL = 0;
  *PPU_MASK = 0;

  client_nmi = 0;
  counter = 0;
  old_joy1 = 0;
  old_joy2 = 0;
  joy1 = 0;
  joy2 = 0;
  jiggleframe = 0;

  *APU_STATUS = 0x0f;
  *APU_PULSE1_ENV = 0x0f;
  *APU_PULSE1_LEN = 0x01;

  // load the palette
  // set an address in the PPU of 0x3f00
  SET_PPU_ADDRESS(0x3f00U);
  for (index = 0; index < sizeof(PALETTE); ++index) {
    *PPU_DATA = PALETTE[index];
  }

  // Stick some arbitrary colors in the attribute table.
  // SET_PPU_ADDRESS(0x23d3);
  SET_PPU_ADDRESS(0x23c0);
  for (index = 0; index < 64; ++index) {
    counter = counter * 5 + 0x37;
    counter = (counter << 3) | (counter >> 5);
    *PPU_DATA = counter;
  }

  // Reset the scroll position.
  SET_PPU_ADDRESS(0x0000U);
  *SCROLL = 0;
  *SCROLL = 0;

  // turn on screen
  *PPU_CTRL = 0x90;      // screen is on, NMI on
  *PPU_MASK = 0x1e;

  // Main loop.
  for (;;) {
    // Sync to NMI flag (set in interrupt handler in reset.s)
    while (!client_nmi) {}
    // DoDMA();

    // ok to touch PPU here.

    // Too slow to do in one frame, so we update one line.
    // Note that the whole ram doesn't fit on the screen, so we're
    // also blinking back and forth!
    #define CHUNK_SIZE 64
    // alternate screens
    // #define TOTAL_CHUNKS (2048 / CHUNK_SIZE)
    // one screen, switched with joystick
    #define TOTAL_CHUNKS (1024 / CHUNK_SIZE)
    {
      #if CHUNK_SIZE <= 32
      // Number of chunks in one line
      #define CHUNKS_ACROSS (32 / CHUNK_SIZE)
      #define CHUNKS_SCREEN (32 * CHUNKS_ACROSS)
      int sx = (chunk_num % CHUNKS_ACROSS) * CHUNK_SIZE;
      int sy = (chunk_num / CHUNKS_ACROSS) & 31;
      #else
      #define LINES_PER_CHUNK (CHUNK_SIZE / 32)
      int sx = 0;
      int sy = (chunk_num * LINES_PER_CHUNK) & 31;
      #endif
      // Note, we only show bank zero, and this actually only
      // displays up to 0x3BF because the screen is 240 pixels
      // high. That's enough for the purposes of this test cart.
      if (1 /* bank == 0 && sy < 30 */) {
        unsigned short screen_start = 0x2000U + (unsigned short)sy * 32 + sx;
        MEM_LINE = MEM + (unsigned short)chunk_num * CHUNK_SIZE;
        if (switch_bank) MEM_LINE += (unsigned short)1024;

        SET_PPU_ADDRESS(screen_start);
        // PERF: Can get a better frame rate by optimizing
        // this loop! Maybe just unrolling it!
        // #define ONE_CHUNK *PPU_DATA = chunk_num
        // #define BEGIN_CHUNKS {}
        // #define ONE_CHUNK *PPU_DATA = *MEM_LINE; ++MEM_LINE
        // We only need to increment the low byte of MEM_LINE,
        // since a single line will never overflow.
        #if 1
        #define BEGIN_CHUNKS \
          __asm__("ldy $0")
        #define ONE_CHUNK \
          __asm__("lda (%v),y", MEM_LINE);        \
          __asm__("sta $2007");                 \
          __asm__("iny")
#endif

        // #define ONE_CHUNK *PPU_DATA = bb
        #if 1
        BEGIN_CHUNKS;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; // 16
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; // 16
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; // 16
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK;
        ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; ONE_CHUNK; // 16
        #else
        { // unsigned char bb = bank >> 4;
        for (index = 0; index < CHUNK_SIZE; ++index) {
          ONE_CHUNK;
        }
        }
        #endif
      }
    }

    chunk_num++;
    chunk_num %= TOTAL_CHUNKS;

    // Need to reset scroll (last) since writes to VRAM can overwrite this.
    SET_PPU_ADDRESS(0x0000U);
    *SCROLL = 0;
    *SCROLL = 0;

    client_nmi = 0;

    // assuming now out of vblank. do what we gotta do.

    // Play sounds to assist in diagnosing problems if the
    // video isn't working.
    if (music_on && (jiggleframe & 15) == 0) {
      *APU_STATUS = 0x0f;
      *APU_PULSE1_ENV = 0x0f;
      *APU_PULSE1_TIMER = (jiggleframe * 0x51) ^ 0x5A;
      *APU_PULSE1_LEN = 0x01;

      *APU_PULSE2_ENV = 0x0f;
      *APU_PULSE2_TIMER = ((jiggleframe >> 4) * 3) ^ 0x17;
      *APU_PULSE2_LEN = 0x01;
    }

    jiggleframe++;
    // jiggleframe &= 63;

    GetInput();

    {
      unsigned char joy_new = joy1 & ~old_joy1;
      if (joy_new & SELECT) switch_bank = !switch_bank;
      if (joy_new & START) music_on = !music_on;
      old_joy1 = joy1;
    }
  }
};

