/*
 * hvm.c
 */

#include <getopt.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "hopcodes.h"
#include "utils.h"

#define INVALID (-1)
#define P_OFF 0x8 /* program offset */

#define ROM_SIZE 32768 /* 32KB */
#define RAM_SIZE 16384 /* 16KB */

typedef struct _HVMData {
    uint16_t comp:10;
    uint8_t dest:3;
    uint8_t jmp:3;
    int16_t A_REG:16;
    int16_t D_REG:16;
    int state;
    int pc;
} HVMData;

enum hvm_state {
    hvm_fetch,
    hvm_decode,
    hvm_execute
};

/* Memory */
uint16_t ROM[ROM_SIZE];
int16_t RAM[RAM_SIZE];

/* Current state of machine */
int running = 1;


/* Initialize VM */
static void vm_init(char *);

/* VM State: Fetch, Decode, Execute */
static uint16_t fetch(HVMData *);
static void decode(uint16_t, HVMData *);
static void execute(HVMData *);

/* Memory snapshot */
static void snapshot(HVMData *);


int main(int argc, char *argv[]) {
    int opt;

    const char *usage = "Usage: ./hvm [file.hex]";
    while ((opt = getopt(argc, argv, "h:")) != -1) {
        switch (opt) {
            case 'h':
                printf("%s\n", usage);
                break;
            default: /* '?' */
                hvm_error("Usage: %s [file.hex]\n", Error, argv[0]);
        }
    }

    if (argv[optind] == NULL || strlen(argv[optind]) == 0) {
        hvm_error("error: %s: No such file or directory\n", Fatal, argv[optind]);
    }

    vm_init(argv[optind]);

    uint16_t instr;
    HVMData hdt = {
            .state=hvm_fetch,
            .pc=0};

    while (running) {
        // Fetch State
        instr = fetch(&hdt);

        if (instr == INVALID)
            break;
        // Decode State
        decode(instr, &hdt);
        if (hdt.state == hvm_execute) {
            hdt.state = hvm_fetch;
            // Execute State
            execute(&hdt);
        }
    }
    snapshot(&hdt);
}

static void snapshot(HVMData *hdt) {
    char *msg = " _   ___      ____  __   \n"
                "| | | |\\ \\   / |  \\/  |  \n"
                "| |_| | \\ \\ / /| |\\/| |  \n"
                "|  _  |  \\ V / | |  | |  \n"
                "|_| |_|   \\_/  |_|  |_|  \n"
                "                          \n"
                "Memory Snapshot \n"
                "****************************************\n"
                "*           *            *    CPU      *\n"
                "*           *            ***************\n"
                "*   ROM     *   RAM      |  A REG [%d]  \n"
                "*           *            |--------------\n"
                "*           *            |  D REG [%d]  \n"
                "*           *            |--------------\n"
                "*           *            |  PC [%d]     \n";

    char *memories = "_________________________\n"
                "|  %x             %d     \n";
    printf(msg, hdt->A_REG, hdt->D_REG, hdt->pc);
    for (int i = 0; ROM[i] ^ 0xffff; ++i) {
        printf(memories, ROM[i], RAM[i]);
    }

}


static void vm_init(char *arg) {
    uint16_t buff;
    int ind = 0;

    FILE *hexfp = NULL;

    if (fd_isreg(arg) > 0) {
        hexfp = hvm_fopen(arg, "rb");
    } else {
        hvm_error("error: %s: No such file or directory\n", Fatal, arg);
    }


    assert(hexfp != NULL);

    memset(RAM, 0, sizeof(RAM));
    memset(ROM, 0, sizeof(ROM));

    // jump program offset
    fseek(hexfp, P_OFF, SEEK_SET);
    while (fread(&buff, sizeof(buff), 1, hexfp) != 0) {
        ROM[ind++] = read_msb(buff);
    }
    // end-of-program signature
    ROM[ind] = (uint16_t) EOF;
    hvm_fclose(hexfp);
}

static uint16_t fetch(HVMData *hdt) {
    return ROM[hdt->pc++];
}

static void decode(uint16_t instr, HVMData *hdt) {
    // check instr first significant 3 bits.If 111 it is C instr,otherwise A instr
    if (((instr & 0xE000) >> 13) ^ 0x7) {
        hdt->state = hvm_decode;
        hdt->A_REG = instr;
        return;
    }
    // extract comp,dest,jmp parts of instr
    hdt->comp = (uint16_t) ((instr & 0xFFC0) >> 6);     // 1111111111000000
    hdt->dest = (uint8_t) ((instr & 0x38) >> 3);        // 0000000000111000
    hdt->jmp = (uint8_t) (instr & 0x07);                // 0000000000000111

    // turn machine state execute
    hdt->state = hvm_execute;
}


static void execute(HVMData *hdt) {
    if (!(hdt->jmp ^ 0x0)) {
        switch (hdt->comp) {
            case COMP_ZERO:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = 0x0;
                        break;
                    case DEST_D:
                        hdt->D_REG = 0x0;
                        break;
                    case DEST_MD:
                        hdt->D_REG = 0x0;
                        RAM[hdt->A_REG] = 0x0;
                        break;
                    case DEST_A:
                        hdt->A_REG = 0x0;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = 0x0;
                        hdt->A_REG = 0x0;
                        break;
                    case DEST_AD:
                        hdt->D_REG = 0x0;
                        hdt->A_REG = 0x0;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = 0x0;
                        hdt->A_REG = 0x0;
                        hdt->D_REG = 0x0;
                        break;
                    default:
                        break;
                }

                break;
            case COMP_ONE:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = 0x1;
                        break;
                    case DEST_D:
                        hdt->D_REG = 0x1;
                        break;
                    case DEST_MD:
                        hdt->D_REG = 0x1;
                        RAM[hdt->A_REG] = 0x1;
                        break;
                    case DEST_A:
                        hdt->A_REG = 0x1;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = 0x1;
                        hdt->A_REG = 0x1;
                        break;
                    case DEST_AD:
                        hdt->D_REG = 0x1;
                        hdt->A_REG = 0x1;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = 0x1;
                        hdt->A_REG = 0x1;
                        hdt->D_REG = 0x1;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_1:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = -1;
                        break;
                    case DEST_D:
                        hdt->D_REG = -1;
                        break;
                    case DEST_MD:
                        hdt->D_REG = -1;
                        RAM[hdt->A_REG] = -1;
                        break;
                    case DEST_A:
                        hdt->A_REG = -1;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = -1;
                        hdt->A_REG = -1;
                        break;
                    case DEST_AD:
                        hdt->D_REG = -1;
                        hdt->A_REG = -1;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = -1;
                        hdt->A_REG = -1;
                        hdt->D_REG = -1;
                        break;
                    default:
                        break;
                }

                break;
            case COMP_D:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG;
                        break;
                    case DEST_D:
                        break;
                    case DEST_MD:
                        RAM[hdt->A_REG] = hdt->D_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AD:
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->A_REG;
                        break;
                    case DEST_A:
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->A_REG;
                        hdt->D_REG = hdt->A_REG;
                        break;
                    default:
                        break;
                }

                break;
            case COMP_NOT_D:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = ~(hdt->D_REG);
                        break;
                    case DEST_D:
                        hdt->D_REG = ~(hdt->D_REG);
                        break;
                    case DEST_MD:
                        hdt->D_REG = ~(hdt->D_REG);
                        RAM[hdt->A_REG] = ~(hdt->D_REG);
                        break;
                    case DEST_A:
                        hdt->A_REG = ~(hdt->D_REG);
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = ~(hdt->D_REG);
                        hdt->A_REG = ~(hdt->D_REG);
                        break;
                    case DEST_AD:
                        hdt->D_REG = ~(hdt->D_REG);
                        hdt->A_REG = ~(hdt->D_REG);
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = ~(hdt->D_REG);
                        hdt->A_REG = ~(hdt->D_REG);
                        hdt->D_REG = ~(hdt->D_REG);
                        break;
                    default:
                        break;
                }
                break;
            case COMP_NOT_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = ~(hdt->A_REG);
                        break;
                    case DEST_D:
                        hdt->D_REG = ~(hdt->A_REG);
                        break;
                    case DEST_MD:
                        hdt->D_REG = ~(hdt->A_REG);
                        RAM[hdt->A_REG] = ~(hdt->A_REG);
                        break;
                    case DEST_A:
                        hdt->A_REG = ~(hdt->A_REG);
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = ~(hdt->A_REG);
                        hdt->A_REG = ~(hdt->A_REG);
                        break;
                    case DEST_AD:
                        hdt->D_REG = ~(hdt->A_REG);
                        hdt->A_REG = ~(hdt->A_REG);
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = ~(hdt->A_REG);
                        hdt->A_REG = ~(hdt->A_REG);
                        hdt->D_REG = ~(hdt->A_REG);
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_D:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = -hdt->D_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = -hdt->D_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = -hdt->D_REG;
                        RAM[hdt->A_REG] = -hdt->D_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = -hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = -hdt->D_REG;
                        hdt->A_REG = -hdt->D_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = -hdt->D_REG;
                        hdt->A_REG = -hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = -hdt->D_REG;
                        hdt->A_REG = -hdt->D_REG;
                        hdt->D_REG = -hdt->D_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = -hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = -hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = -hdt->A_REG;
                        RAM[hdt->A_REG] = -hdt->A_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = -hdt->A_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = -hdt->A_REG;
                        hdt->A_REG = -hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = -hdt->A_REG;
                        hdt->A_REG = -hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = -hdt->A_REG;
                        hdt->A_REG = -hdt->A_REG;
                        hdt->D_REG = -hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_PLUS_1:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = ++hdt->D_REG;
                        break;
                    case DEST_D:
                        ++hdt->D_REG;
                        break;
                    case DEST_MD:
                        ++hdt->D_REG;
                        RAM[hdt->A_REG] = hdt->D_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = ++hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = ++hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AD:
                        ++hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = ++hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A_PLUS_1:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = ++hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = ++hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = ++hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->A_REG;
                        break;
                    case DEST_A:
                        ++hdt->A_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = ++hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = ++hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = ++hdt->A_REG;
                        hdt->D_REG = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_MINUS_1:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = --hdt->D_REG;
                        break;
                    case DEST_D:
                        --hdt->D_REG;
                        break;
                    case DEST_MD:
                        --hdt->D_REG;
                        RAM[hdt->A_REG] = hdt->D_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = --hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = --hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AD:
                        --hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = --hdt->D_REG;
                        hdt->A_REG = hdt->D_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A_MINUS_1:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = --hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = --hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = --hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->A_REG;
                        break;
                    case DEST_A:
                        --hdt->A_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = --hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = --hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = --hdt->A_REG;
                        hdt->D_REG = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_PLUS_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG + hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG += hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG += hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->D_REG + hdt->A_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG += hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG + hdt->A_REG;
                        hdt->A_REG += hdt->D_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG += hdt->A_REG;
                        hdt->A_REG += hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG + hdt->A_REG;
                        hdt->A_REG += hdt->D_REG;
                        hdt->D_REG += hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_MINUS_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG - hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG -= hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG -= hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->D_REG - hdt->A_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG - hdt->A_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG - hdt->A_REG;
                        hdt->A_REG = hdt->D_REG - hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG -= hdt->A_REG;
                        hdt->A_REG = hdt->D_REG - hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG - hdt->A_REG;
                        hdt->A_REG = hdt->D_REG - hdt->A_REG;
                        hdt->D_REG -= hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A_MINUS_D:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->A_REG - hdt->D_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = hdt->A_REG - hdt->D_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = hdt->A_REG - hdt->D_REG;
                        RAM[hdt->A_REG] = hdt->A_REG - hdt->D_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG -= hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->A_REG - hdt->D_REG;
                        hdt->A_REG -= hdt->D_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = hdt->A_REG - hdt->D_REG;
                        hdt->A_REG -= hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->A_REG - hdt->D_REG;
                        hdt->A_REG -= hdt->D_REG;
                        hdt->D_REG = hdt->A_REG - hdt->D_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_AND_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG & hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG &= hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG &= hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->D_REG & hdt->A_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG & hdt->A_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG & hdt->A_REG;
                        hdt->A_REG = hdt->D_REG & hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG &= hdt->A_REG;
                        hdt->A_REG = hdt->D_REG & hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG & hdt->A_REG;
                        hdt->A_REG = hdt->D_REG & hdt->A_REG;
                        hdt->D_REG &= hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_OR_A:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG | hdt->A_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG |= hdt->A_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG |= hdt->A_REG;
                        RAM[hdt->A_REG] = hdt->D_REG | hdt->A_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG | hdt->A_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG | hdt->A_REG;
                        hdt->A_REG = hdt->D_REG | hdt->A_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG |= hdt->A_REG;
                        hdt->A_REG = hdt->D_REG | hdt->A_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG | hdt->A_REG;
                        hdt->A_REG = hdt->D_REG | hdt->A_REG;
                        hdt->D_REG |= hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_M:
                switch (hdt->dest) {
                    case DEST_M:
                        break;
                    case DEST_D:
                        hdt->D_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        hdt->A_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG = RAM[hdt->A_REG];
                        hdt->A_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        hdt->A_REG = RAM[hdt->A_REG];
                        hdt->D_REG = RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_NOT_M:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = ~RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG = ~RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG = ~RAM[hdt->A_REG];
                        RAM[hdt->A_REG] = ~RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = ~RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = ~RAM[hdt->A_REG];
                        hdt->A_REG = ~RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG = ~RAM[hdt->A_REG];
                        hdt->A_REG = ~RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = ~RAM[hdt->A_REG];
                        hdt->A_REG = ~RAM[hdt->A_REG];
                        hdt->D_REG = ~RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_M:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = -RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG = -RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG = -RAM[hdt->A_REG];
                        RAM[hdt->A_REG] = -RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = -RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = -RAM[hdt->A_REG];
                        hdt->A_REG = -RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG = -RAM[hdt->A_REG];
                        hdt->A_REG = -RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = -RAM[hdt->A_REG];
                        hdt->A_REG = -RAM[hdt->A_REG];
                        hdt->D_REG = -RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_M_PLUS_1:
                switch (hdt->dest) {
                    case DEST_M:
                        ++RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG = ++RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG = ++RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = ++RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        ++RAM[hdt->A_REG];
                        hdt->A_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG = ++RAM[hdt->A_REG];
                        hdt->A_REG = RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        ++RAM[hdt->A_REG];
                        hdt->A_REG = RAM[hdt->A_REG];
                        hdt->D_REG = RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_PLUS_M:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG + RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG += RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG += RAM[hdt->A_REG];
                        RAM[hdt->A_REG] = hdt->D_REG + RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG + RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG + RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG + RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG += RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG + RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG + RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG + RAM[hdt->A_REG];
                        hdt->D_REG += RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_MINUS_M:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG - RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG -= RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG -= RAM[hdt->A_REG];
                        RAM[hdt->A_REG] = hdt->D_REG - RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG - RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG - RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG - RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG -= RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG - RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG - RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG - RAM[hdt->A_REG];
                        hdt->D_REG -= RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_M_MINUS_D:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] -= hdt->D_REG;
                        break;
                    case DEST_D:
                        hdt->D_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        break;
                    case DEST_MD:
                        hdt->D_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        RAM[hdt->A_REG] -= hdt->D_REG;
                        break;
                    case DEST_A:
                        hdt->A_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] -= hdt->D_REG;
                        hdt->A_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        break;
                    case DEST_AD:
                        hdt->D_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        hdt->A_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] -= hdt->D_REG;
                        hdt->A_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        hdt->D_REG = RAM[hdt->A_REG] - hdt->D_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_AND_M:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG & RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG &= RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG &= RAM[hdt->A_REG];
                        RAM[hdt->A_REG] = hdt->D_REG & RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG & RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG & RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG & RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG &= RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG & RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG & RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG & RAM[hdt->A_REG];
                        hdt->D_REG &= RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_OR_M:
                switch (hdt->dest) {
                    case DEST_M:
                        RAM[hdt->A_REG] = hdt->D_REG | RAM[hdt->A_REG];
                        break;
                    case DEST_D:
                        hdt->D_REG |= RAM[hdt->A_REG];
                        break;
                    case DEST_MD:
                        hdt->D_REG |= RAM[hdt->A_REG];
                        RAM[hdt->A_REG] = hdt->D_REG | RAM[hdt->A_REG];
                        break;
                    case DEST_A:
                        hdt->A_REG = hdt->D_REG | RAM[hdt->A_REG];
                        break;
                    case DEST_AM:
                        RAM[hdt->A_REG] = hdt->D_REG | RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG | RAM[hdt->A_REG];
                        break;
                    case DEST_AD:
                        hdt->D_REG |= RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG | RAM[hdt->A_REG];
                        break;
                    case DEST_AMD:
                        RAM[hdt->A_REG] = hdt->D_REG | RAM[hdt->A_REG];
                        hdt->A_REG = hdt->D_REG | RAM[hdt->A_REG];
                        hdt->D_REG |= RAM[hdt->A_REG];
                        break;
                    default:
                        break;
                }
                break;
            default:
                running = 0;
                break;
        }
    } else {
        switch (hdt->comp) {
            case COMP_ZERO:
                switch (hdt->jmp) {
                    case JGT:
                    case JLT:
                    case JNE:
                        break;
                    case JEQ:
                    case JGE:
                    case JLE:
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_ONE:
                switch (hdt->jmp) {
                    case JEQ:
                    case JLT:
                        break;
                    case JGT:
                    case JGE:
                    case JNE:
                    case JLE:
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_1:
                switch (hdt->jmp) {
                    case JGT:
                    case JEQ:
                    case JGE:
                        break;
                    case JLT:
                    case JNE:
                    case JLE:
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }

                break;
            case COMP_D:
                switch (hdt->jmp) {
                    case JGT:
                        if (hdt->D_REG > 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JEQ:
                        if (hdt->D_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (hdt->D_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (hdt->D_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (hdt->D_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (hdt->D_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A:
                switch (hdt->jmp) {
                    case JGT:
                        if (hdt->A_REG > 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JEQ:
                        if (hdt->A_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (hdt->A_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (hdt->A_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (hdt->A_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (hdt->A_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }

                break;
            case COMP_NOT_D:
                switch (hdt->jmp) {
                    case JGT:
                        if (~hdt->D_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (~hdt->D_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (~hdt->D_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (~hdt->D_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (~hdt->D_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (~hdt->D_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_NOT_A:
                switch (hdt->jmp) {
                    case JGT:
                        if (~hdt->A_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (~hdt->A_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (~hdt->A_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (~hdt->A_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (~hdt->A_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (~hdt->A_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_D:
                switch (hdt->jmp) {
                    case JGT:
                        if (-hdt->D_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (-hdt->D_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (-hdt->D_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (-hdt->D_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (-hdt->D_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (-hdt->D_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_A:
                switch (hdt->jmp) {
                    case JGT:
                        if (-hdt->A_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (-hdt->A_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (-hdt->A_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (-hdt->A_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (-hdt->A_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (-hdt->A_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_PLUS_1:
                switch (hdt->jmp) {
                    case JGT:
                        if (++hdt->D_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (++hdt->D_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (++hdt->D_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (++hdt->D_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (++hdt->D_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (++hdt->D_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A_PLUS_1:
                switch (hdt->jmp) {
                    case JGT:
                        if (++hdt->A_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (++hdt->A_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (++hdt->A_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (++hdt->A_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (++hdt->A_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (++hdt->A_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_MINUS_1:
                switch (hdt->jmp) {
                    case JGT:
                        if (--hdt->D_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (--hdt->D_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (--hdt->D_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (--hdt->D_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (--hdt->D_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (--hdt->D_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A_MINUS_1:
                switch (hdt->jmp) {
                    case JGT:
                        if (--hdt->A_REG > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (--hdt->A_REG == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (--hdt->A_REG >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (--hdt->A_REG < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (--hdt->A_REG != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (--hdt->A_REG <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_PLUS_A:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG + hdt->A_REG) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG + hdt->A_REG) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG + hdt->A_REG) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG + hdt->A_REG) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG + hdt->A_REG) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG + hdt->A_REG) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_MINUS_A:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG - hdt->A_REG) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG - hdt->A_REG) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG - hdt->A_REG) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG - hdt->A_REG) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG - hdt->A_REG) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG - hdt->A_REG) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_A_MINUS_D:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->A_REG - hdt->D_REG) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->A_REG - hdt->D_REG) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->A_REG - hdt->D_REG) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->A_REG - hdt->D_REG) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->A_REG - hdt->D_REG) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->A_REG - hdt->D_REG) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_AND_A:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG & hdt->A_REG) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG & hdt->A_REG) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG & hdt->A_REG) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG & hdt->A_REG) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG & hdt->A_REG) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG & hdt->A_REG) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_OR_A:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG | hdt->A_REG) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG | hdt->A_REG) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG | hdt->A_REG) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG | hdt->A_REG) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG | hdt->A_REG) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG | hdt->A_REG) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_M:
                switch (hdt->jmp) {
                    case JGT:
                        if (RAM[hdt->A_REG] > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (RAM[hdt->A_REG] == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (RAM[hdt->A_REG] >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (RAM[hdt->A_REG] < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (RAM[hdt->A_REG] != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (RAM[hdt->A_REG] <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_NOT_M:
                switch (hdt->jmp) {
                    case JGT:
                        if (~RAM[hdt->A_REG] > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (~RAM[hdt->A_REG] == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (~RAM[hdt->A_REG] >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (~RAM[hdt->A_REG] < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (~RAM[hdt->A_REG] != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (~RAM[hdt->A_REG] <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_MINUS_M:
                switch (hdt->jmp) {
                    case JGT:
                        if (-RAM[hdt->A_REG] > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (-RAM[hdt->A_REG] == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (-RAM[hdt->A_REG] >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (-RAM[hdt->A_REG] < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (-RAM[hdt->A_REG] != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (-RAM[hdt->A_REG] <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_M_PLUS_1:
                switch (hdt->jmp) {
                    case JGT:
                        if (++RAM[hdt->A_REG] > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if (++RAM[hdt->A_REG] == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if (++RAM[hdt->A_REG] >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if (++RAM[hdt->A_REG] < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if (++RAM[hdt->A_REG] != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if (++RAM[hdt->A_REG] <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_PLUS_M:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG + RAM[hdt->A_REG]) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG + RAM[hdt->A_REG]) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG + RAM[hdt->A_REG]) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG + RAM[hdt->A_REG]) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG + RAM[hdt->A_REG]) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG + RAM[hdt->A_REG]) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_MINUS_M:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG - RAM[hdt->A_REG]) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG - RAM[hdt->A_REG]) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG - RAM[hdt->A_REG]) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG - RAM[hdt->A_REG]) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG - RAM[hdt->A_REG]) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG - RAM[hdt->A_REG]) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_M_MINUS_D:
                switch (hdt->jmp) {
                    case JGT:
                        if ((RAM[hdt->A_REG] - hdt->D_REG) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((RAM[hdt->A_REG] - hdt->D_REG) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((RAM[hdt->A_REG] - hdt->D_REG) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((RAM[hdt->A_REG] - hdt->D_REG) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((RAM[hdt->A_REG] - hdt->D_REG) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((RAM[hdt->A_REG] - hdt->D_REG) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_AND_M:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG & RAM[hdt->A_REG]) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG & RAM[hdt->A_REG]) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG & RAM[hdt->A_REG]) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG & RAM[hdt->A_REG]) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG & RAM[hdt->A_REG]) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG & RAM[hdt->A_REG]) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;
                }
                break;
            case COMP_D_OR_M:
                switch (hdt->jmp) {
                    case JGT:
                        if ((hdt->D_REG | RAM[hdt->A_REG]) > 0)
                            hdt->pc = hdt->A_REG;

                        break;
                    case JEQ:
                        if ((hdt->D_REG | RAM[hdt->A_REG]) == 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JGE:
                        if ((hdt->D_REG | RAM[hdt->A_REG]) >= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLT:
                        if ((hdt->D_REG | RAM[hdt->A_REG]) < 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JNE:
                        if ((hdt->D_REG | RAM[hdt->A_REG]) != 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JLE:
                        if ((hdt->D_REG | RAM[hdt->A_REG]) <= 0)
                            hdt->pc = hdt->A_REG;
                        break;
                    case JMP:
                        hdt->pc = hdt->A_REG;
                        break;
                    default:
                        break;

                }
                break;
            default:
                running = 0;
                break;
        }

    }

}

