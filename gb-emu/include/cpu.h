#ifndef GB_CPU_H
#define GB_CPU_H

#include "types.h"

typedef struct {
    union {
        struct {
            u8 f;
            u8 a;
        };
        u16 af;
    };
    union {
        struct {
            u8 c;
            u8 b;
        };
        u16 bc;
    };
    union {
        struct {
            u8 e;
            u8 d;
        };
        u16 de;
    };
    union {
        struct {
            u8 l;
            u8 h;
        };
        u16 hl;
    };
    u16 sp;
    u16 pc;
} gb_registers_t;

struct gb;

typedef struct {
    gb_registers_t reg;
    bool halted;
    bool ime;
    bool ime_pending;
    int ime_delay;
    s32 cycles;
    int div_counter;
    int tima_counter;
    /* T-cycles the current instruction has already handed to the rest of the
     * hardware, so gb_cpu_step can tick whatever is left over at the end. */
    int tick_acc;
} gb_cpu_t;

void gb_cpu_init(gb_cpu_t *cpu);
int gb_cpu_step(gb_cpu_t *cpu, struct gb *gb);
void gb_cpu_trigger_interrupt(struct gb *gb, int flag);

#endif
