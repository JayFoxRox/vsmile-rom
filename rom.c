// (C)2020 Jannik Vogel
//
// Use like:
//
//    make -C .. && reset && clang rom.c && ./a.out && ../un-disas rom.bin && ../uuu-sdl rom.bin 

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  A_ADD=0,
  A_ADC=1,
  A_SUB=2,
  A_SBC=3,
  A_CMP=4,
  A_NEG=6,
  A_XOR=8,
  A_LOAD=9, // x = x1
  A_OR=10,
  A_AND=11,
  A_TEST=12,
  A_STORE=13 //FIXME: Difference to A_LOAD?
} AluOperation;

typedef struct {
  union {
    struct {
      uint16_t opB:3; // and the last three usually the second register (source register)
      uint16_t opN:3; // the next three can be anything
      uint16_t pad_hi0:10;
    };
    struct {
      uint16_t opimm:6; // the last six sometimes are a single immediate number
      uint16_t pad_hi1:10;
    };
    struct {
      uint16_t pad_lo:6;
      uint16_t op1:3; // and the next three the addressing mode
      uint16_t opA:3; // the next three are usually the destination register
      uint16_t op0:4; // the top four bits are the alu op or the branch condition, or E or F
    };
    uint16_t raw;
  };
} Instruction;

uint16_t mem[0x20000];
Instruction* code = mem;
Instruction* cursor;

typedef enum {
  R_SP,
  R_R1,
  R_R2,
  R_R3,
  R_R4,
  R_BP,
  R_SR,
  R_PC
} Register;

static void asm_set(Register r, uint16_t value) {
  cursor->op1 = 4;
  cursor->opN = 1;
  cursor->opA = r;
  cursor->opB = r;
  cursor->op0 = A_LOAD; // x = r[opB]
  cursor++;
  cursor->raw = value;
  cursor++;
}

static void asm_load(Register r, uint16_t address) {
  cursor->op1 = 4; // [imm16]
  cursor->opN = 2;
  cursor->opA = r;
  cursor->opB = r;
  cursor->op0 = A_LOAD; // x = r[opB]
  cursor++;
  cursor->raw = address;
  cursor++;
}

static void asm_store(uint16_t address, Register r) {
  cursor->op1 = 4;
  cursor->opN = 3;
  cursor->opA = r;
  cursor->opB = r;
  cursor->op0 = A_STORE; // x = r[opB]
  cursor++;
  cursor->raw = address;
  cursor++;
}

typedef enum {
  C_NEQUAL=4,
  C_EQUAL=5,
  C_ALWAYS=14
} Condition;

static void asm_alu(AluOperation op, Register ra, Register rb) {
  cursor->op1 = 4;
  cursor->opN = 0;
  cursor->op0 = op;
  cursor->opA = ra;
  cursor->opB = rb;
  cursor++;
}


static void asm_jump(Condition c, Instruction* target) {
  uint16_t pc = (cursor+1) - code;
  uint16_t address = target - code;

  cursor->op0 = c;
  cursor->opA = 7;
  cursor->op1 = (address < pc) ? 1 : 0; // 1 = backwards, 0 = forwards
  cursor->opimm = abs(address - pc);
  cursor++;
}

static void asm_goto(Instruction* target) {
  uint32_t address = target - code;
  cursor->op0 = 15;
  cursor->op1 = 2; // JMPF
  cursor->opA = 7;
  cursor->opimm = address >> 16;
  cursor++;
  cursor->raw = address & 0xFFFF;
  cursor++;
}

static void store_value(uint16_t address, uint16_t value) {
  asm_set(R_R1, value);
  asm_store(address, R_R1);
}

typedef enum {
  SS_8,
  SS_16,
  SS_32,
  SS_64
} SpriteSize;

typedef enum {
  SC_2,
  SC_4,
  SC_6,
  SC_8
} SpriteColors;

static void set_sprite(unsigned int i, uint16_t tile, uint16_t x, uint16_t y, unsigned int depth, SpriteSize w, SpriteSize h, SpriteColors nc) {
 
  uint16_t attr = (depth << 12) | (w << 6) | (h << 4) | nc;

/*
	u32 yflipmask = attr & 0x0008 ? h - 1 : 0;
	u32 xflipmask = attr & 0x0004 ? w - 1 : 0;
*/

/*
	u8 alpha = 255;
	if (attr & 0x4000)
		alpha = mem[0x282a] << 6;
*/

/*
	u32 pal_offset = (attr & 0x0f00) >> 4;
	pal_offset >>= nc;
	pal_offset <<= nc;
*/

  //FIXME: if ((u32)(attr & 0x3000) >> 12 != depth)
  store_value(0x2c00+i*4+0, tile);
  store_value(0x2c00+i*4+1, x);
  store_value(0x2c00+i*4+2, y);
  store_value(0x2c00+i*4+3, attr);
}

// Map [0x00,0xFF] to [0x00,0x1F] and pack
uint16_t color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return (((r >> 3) & 0x1F) << 10) |
         (((g >> 3) & 0x1F) << 5) |
         (((b >> 3) & 0x1F) << 0) |
         (~a & 0x80) << 8;
}

int main() {
  memset(mem, 0x00, sizeof(mem));

  // Be a batman board because it has a simple GPIO thingy
  mem[0x5ce1] = 0x42c2;
  mem[0x5ce2] = 0x5e42;

  cursor = &mem[0x6000];

  #define BATMAN_INPUT_GPIO 0x3d01
  #define BATMAN_INPUT_GPIO_UP 0x8000

  // Mainloop
  uint16_t* start = cursor;
  asm_load(R_R1, BATMAN_INPUT_GPIO);
  asm_set(R_R2, BATMAN_INPUT_GPIO_UP);
  asm_alu(A_TEST, R_R1, R_R2);

  // Decide which color to use for the palette
  uint16_t* jump = cursor;
  asm_jump(0, 0); // set_blue?
  asm_goto(0); // fill_red
  uint16_t* set_blue = cursor;
  asm_goto(0); // fill_blue

  // Fill palette
  uint16_t* fill_red = cursor;
  for(int i = 0; i < 0x100; i++) {
    store_value(0x2b00 + i, color(i,0,0,0xFF));
  }
  uint16_t* fill_red_done = cursor;
  asm_goto(0);
  uint16_t* fill_blue = cursor;
  for(int i = 0; i < 0x100; i++) {
    store_value(0x2b00 + i, color(0,0,i,0xFF));
  }
  uint16_t* fill_blue_done = cursor;
  uint16_t* fill_done = cursor;

  // Fixup jumps
  {
    uint16_t* cursor_backup = cursor;

    cursor = jump;
    asm_jump(C_NEQUAL, set_blue);
    asm_goto(fill_red);
    //set_blue
    asm_goto(fill_blue);

    cursor = fill_red_done;
    asm_goto(fill_done);

    cursor = cursor_backup;
  }

  // Prepare some sprite table
  uint16_t sprite_base = 0x0000;
  store_value(0x2822, sprite_base / 0x40);

  // Create an image
  uint8_t image[128*128];
  unsigned int sprite_size = 32*32*8/16;
  for(int y = 0; y < 128; y++) {
    for(int x = 0; x < 128; x++) {
      float l = x / 128.0f * y / 128.0f;
      image[y * 128 + x] = (int)(0xFF * l);
    }
  }

  // Create sprites
  int cx = 4;
  int cy = 4;
  for(int y = 0; y < cy; y++) {
    for(int x = 0; x < cx; x++) {
      int sprite_i = y * cx + x;
      unsigned int tile = 1 + sprite_i;

      for(int dy = 0; dy < 32; dy++) {
        for(int dx = 0; dx < 32; dx+=2) {
          int image_xy = (y*32+dy) * 128 + (x*32+dx);
          int sprite_xy = (dy*32+dx)/2;
          store_value(sprite_base + tile * sprite_size + sprite_xy, (image[image_xy+0] << 8) | image[image_xy+1]);
        }
      }

    }
  }

  // Place sprites
  for(int y = 0; y < cy; y++) {
    for(int x = 0; x < cx; x++) {
      int sprite_i = y * cx + x;
      unsigned int tile = 1 + sprite_i;
      set_sprite(sprite_i, tile, x*32, 50-y*32, 1, SS_32, SS_32, SC_8);
    }
  }

   // Enable sprites
  store_value(0x2842, 0x0001);

  //asm_jump(C_ALWAYS, start);
  asm_goto(start);


  // Entry point
  mem[0xfff7] = 0x6000;

  FILE* f = fopen("rom.bin", "wb");
  fwrite(mem, sizeof(mem), 1, f);
  fclose(f);
}
