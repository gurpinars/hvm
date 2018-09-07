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
    // our end-of-program signature
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
    hdt->comp = (uint16_t) ((instr & 0xFFC0) >> 6); // 1111111111000000
    hdt->dest = (uint8_t) ((instr & 0x38) >> 3); //  0000000000111000
    hdt->jmp = (uint8_t) (instr & 0x07); // 0000000000000111

    // turn machine state execute
    hdt->state = hvm_execute;
}

#define SWITCH_CASE_DEST(OPCODE, ptrVmData, COMP)       \
    case OPCODE:                                        \
        switch ((ptrVmData)->dest) {                    \
            case DEST_M:                                \
                RAM[(ptrVmData)->A_REG] = COMP;         \
                break;                                  \
            case DEST_D:                                \
                (ptrVmData)->D_REG = COMP;              \
                break;                                  \
            case DEST_MD:                               \
                (ptrVmData)->D_REG = COMP;              \
                RAM[(ptrVmData)->A_REG] = COMP;         \
                break;                                  \
            case DEST_A:                                \
                (ptrVmData)->A_REG = COMP;              \
                break;                                  \
            case DEST_AM:                               \
                RAM[(ptrVmData)->A_REG] = COMP;         \
                (ptrVmData)->A_REG = COMP;              \
                break;                                  \
            case DEST_AD:                               \
                (ptrVmData)->D_REG = COMP;              \
                (ptrVmData)->A_REG = COMP;              \
                break;                                  \
            case DEST_AMD:                              \
                RAM[(ptrVmData)->A_REG] = COMP;         \
                (ptrVmData)->A_REG = COMP;              \
                (ptrVmData)->D_REG = COMP;              \
                break;                                  \
            default:                                    \
                break;                                  \
        }                                               \
        break;                                          \

#define SWITCH_CASE_JMP(OPCODE, ptrVmData, COMP)                \
    case OPCODE:                                                \
        switch ((ptrVmData)->jmp) {                             \
            case JGT:                                           \
                if ((COMP) > 0)                                 \
                    (ptrVmData)->pc = (ptrVmData)->A_REG;       \
                break;                                          \
            case JEQ:                                           \
                if ((COMP) == 0)                                \
                    (ptrVmData)->pc = (ptrVmData)->A_REG;       \
                break;                                          \
            case JGE:                                           \
                if ((COMP) >= 0)                                \
                    (ptrVmData)->pc = (ptrVmData)->A_REG;       \
                break;                                          \
            case JLT:                                           \
                if ((COMP) < 0)                                 \
                    (ptrVmData)->pc = (ptrVmData)->A_REG;       \
                break;                                          \
            case JNE:                                           \
                if ((COMP) != 0)                                \
                    (ptrVmData)->pc = (ptrVmData)->A_REG;       \
                break;                                          \
            case JLE:                                           \
                if ((COMP) <= 0)                                \
                    (ptrVmData)->pc = (ptrVmData)->A_REG;       \
                    break;                                      \
            case JMP:                                           \
                (ptrVmData)->pc = (ptrVmData)->A_REG;           \
            default:                                            \
                break;                                          \
        }                                                       \
        break;                                                  \


static void execute(HVMData *hdt) {
    if (!(hdt->jmp ^ 0x0)) {
        switch (hdt->comp) {
            SWITCH_CASE_DEST(COMP_ZERO, hdt, 0)
            SWITCH_CASE_DEST(COMP_ONE, hdt, 0x1)
            SWITCH_CASE_DEST(COMP_MINUS_1, hdt, -1)
            SWITCH_CASE_DEST(COMP_D, hdt, hdt->D_REG)
            SWITCH_CASE_DEST(COMP_A, hdt, hdt->A_REG)
            SWITCH_CASE_DEST(COMP_NOT_D, hdt, ~(hdt->D_REG))
            SWITCH_CASE_DEST(COMP_NOT_A, hdt, ~(hdt->A_REG))
            SWITCH_CASE_DEST(COMP_MINUS_D, hdt, -hdt->D_REG)
            SWITCH_CASE_DEST(COMP_MINUS_A, hdt, -hdt->A_REG)
            SWITCH_CASE_DEST(COMP_D_PLUS_1, hdt, ++hdt->D_REG)
            SWITCH_CASE_DEST(COMP_A_PLUS_1, hdt, ++hdt->A_REG)
            SWITCH_CASE_DEST(COMP_D_MINUS_1, hdt, --hdt->D_REG)
            SWITCH_CASE_DEST(COMP_A_MINUS_1, hdt, --hdt->A_REG)
            SWITCH_CASE_DEST(COMP_D_PLUS_A, hdt, (hdt->D_REG + hdt->A_REG))
            SWITCH_CASE_DEST(COMP_D_MINUS_A, hdt, (hdt->D_REG - hdt->A_REG))
            SWITCH_CASE_DEST(COMP_A_MINUS_D, hdt, (hdt->A_REG - hdt->D_REG))
            SWITCH_CASE_DEST(COMP_D_AND_A, hdt, (hdt->D_REG & hdt->A_REG))
            SWITCH_CASE_DEST(COMP_D_OR_A, hdt, (hdt->D_REG | hdt->A_REG))
            SWITCH_CASE_DEST(COMP_M, hdt, RAM[hdt->A_REG])
            SWITCH_CASE_DEST(COMP_NOT_M, hdt, ~RAM[hdt->A_REG])
            SWITCH_CASE_DEST(COMP_MINUS_M, hdt, -RAM[hdt->A_REG])
            SWITCH_CASE_DEST(COMP_M_PLUS_1, hdt, ++RAM[hdt->A_REG])
            SWITCH_CASE_DEST(COMP_M_MINUS_1, hdt, --RAM[hdt->A_REG])
            SWITCH_CASE_DEST(COMP_D_PLUS_M, hdt, (hdt->D_REG + RAM[hdt->A_REG]))
            SWITCH_CASE_DEST(COMP_D_MINUS_M, hdt, (hdt->D_REG - RAM[hdt->A_REG]))
            SWITCH_CASE_DEST(COMP_M_MINUS_D, hdt, (RAM[hdt->A_REG] - hdt->D_REG))
            SWITCH_CASE_DEST(COMP_D_AND_M, hdt, (hdt->D_REG & RAM[hdt->A_REG]))
            SWITCH_CASE_DEST(COMP_D_OR_M, hdt, (hdt->D_REG | RAM[hdt->A_REG]))

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
            SWITCH_CASE_JMP(COMP_D, hdt, hdt->D_REG)
            SWITCH_CASE_JMP(COMP_A, hdt, hdt->A_REG)
            SWITCH_CASE_JMP(COMP_NOT_D, hdt, ~hdt->D_REG)
            SWITCH_CASE_JMP(COMP_NOT_A, hdt, ~hdt->A_REG)
            SWITCH_CASE_JMP(COMP_MINUS_D, hdt, -hdt->D_REG)
            SWITCH_CASE_JMP(COMP_MINUS_A, hdt, -hdt->A_REG)
            SWITCH_CASE_JMP(COMP_D_PLUS_1, hdt, ++hdt->D_REG)
            SWITCH_CASE_JMP(COMP_A_PLUS_1, hdt, ++hdt->A_REG)
            SWITCH_CASE_JMP(COMP_D_MINUS_1, hdt, --hdt->D_REG)
            SWITCH_CASE_JMP(COMP_A_MINUS_1, hdt, --hdt->A_REG)
            SWITCH_CASE_JMP(COMP_D_PLUS_A, hdt, (hdt->D_REG + hdt->A_REG))
            SWITCH_CASE_JMP(COMP_D_MINUS_A, hdt, (hdt->D_REG - hdt->A_REG))
            SWITCH_CASE_JMP(COMP_A_MINUS_D, hdt, (hdt->A_REG - hdt->D_REG))
            SWITCH_CASE_JMP(COMP_D_AND_A, hdt, (hdt->D_REG & hdt->A_REG))
            SWITCH_CASE_JMP(COMP_D_OR_A, hdt, (hdt->D_REG | hdt->A_REG))
            SWITCH_CASE_JMP(COMP_M, hdt, RAM[hdt->A_REG])
            SWITCH_CASE_JMP(COMP_NOT_M, hdt, ~RAM[hdt->A_REG])
            SWITCH_CASE_JMP(COMP_MINUS_M, hdt, -RAM[hdt->A_REG])
            SWITCH_CASE_JMP(COMP_M_PLUS_1, hdt, ++RAM[hdt->A_REG])
            SWITCH_CASE_JMP(COMP_M_MINUS_1, hdt, --RAM[hdt->A_REG])
            SWITCH_CASE_JMP(COMP_D_PLUS_M, hdt, (hdt->D_REG + RAM[hdt->A_REG]))
            SWITCH_CASE_JMP(COMP_D_MINUS_M, hdt, (hdt->D_REG - RAM[hdt->A_REG]))
            SWITCH_CASE_JMP(COMP_M_MINUS_D, hdt, (RAM[hdt->A_REG] - hdt->D_REG))
            SWITCH_CASE_JMP(COMP_D_AND_M, hdt, (hdt->D_REG & RAM[hdt->A_REG]))
            SWITCH_CASE_JMP(COMP_D_OR_M, hdt, (hdt->D_REG | RAM[hdt->A_REG]))

            default:
                running = 0;
                break;
        }

    }
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

    char *rom = "_________________________\n"
                "|  %x             %d     \n";
    printf(msg, hdt->A_REG, hdt->D_REG, hdt->pc);
    for (int i = 0; ROM[i] ^ 0xffff; ++i) {
        printf(rom, ROM[i], RAM[i]);
    }

}
