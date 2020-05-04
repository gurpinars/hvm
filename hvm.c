/*
 * hvm.c
 */

#include <getopt.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include "hopcodes.h"

#define errprint(format, ...) fprintf (stderr, format, __VA_ARGS__);

/* read Most Significant Bit */
#define read_msb(n) ( ((n) << 8u) | ((n) >> 8u) )

/* End of signature */
#define EOS 0xFFFF 

/* payload offset */
#define P_OFF 0x8 

/* 32KB */
#define ROM_SIZE 32768 

/* 16KB */
#define RAM_SIZE 16384

#define EmitComp(n) ((n & 0xFFC0u) >> 6u)
#define EmitDest(n) ((n & 0x38u) >> 3u)
#define EmitJmp(n) (n & 0x07u)

typedef uint16_t u16;
typedef uint8_t u8;

typedef struct {
    u16 comp:10;
    u8 dest:3;
    u8 jmp:3;
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
static u16 ROM[ROM_SIZE];
static int16_t RAM[RAM_SIZE];

/* Current state of machine */
static int running = 1;

/* Initialize VM */
static void vm_init(char *);

/* VM State: Fetch, Decode, Execute */
static u16 fetch(HVMData *);
static void decode(u16, HVMData *);
static void execute(HVMData *);

/* Memory snapshot */
static void snapshot(HVMData *);

static int util_fd_isreg(const char *filename);


int main(int argc, char *argv[]) {
    int opt;

    const char *usage = "Usage: ./hvm [file.hex]";
    while ((opt = getopt(argc, argv, "h:")) != -1) {
        switch (opt) {
            case 'h':
                printf("%s\n", usage);
                break;
            default: /* '?' */
                errprint("Usage: %s [file.hex]\n", argv[0])
        }
    }

    if (argv[optind] == NULL || strlen(argv[optind]) == 0) {
        errprint("error: [%s] No such file or directory\n", argv[optind])
        exit(EXIT_FAILURE);
    }

    vm_init(argv[optind]);

    u16 instr;
    HVMData hdt = {
            .state=hvm_fetch,
            .pc=0};

    while (running) {
        // Fetch State
        instr = fetch(&hdt);

        if (instr == EOS)
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
    u16 buff;
    int ind = 0;

    FILE *hexfp = NULL;

    if (util_fd_isreg(arg) > 0) {
        hexfp = fopen(arg, "rb");
        if (!hexfp) {
            errprint("error: [%s] unable to open file\n", arg)
            exit(EXIT_FAILURE);
        }
    } else {
        errprint("error: [%s] No such file or directory\n", arg)
        exit(EXIT_FAILURE);
    }

    memset(RAM, 0, sizeof(RAM));
    memset(ROM, 0, sizeof(ROM));

    // jump program offset
    fseek(hexfp, P_OFF, SEEK_SET);
    while (fread(&buff, sizeof(buff), 1, hexfp) != 0) {
        ROM[ind++] = read_msb(buff);
    }
    // end-of-program signature
    ROM[ind] = EOS;
    fclose(hexfp);
}

static u16 fetch(HVMData *hdt) {
    return ROM[hdt->pc++];
}

static void decode(u16 instr, HVMData *hdt) {
    // check instr first significant 3 bits.If 111 it is C instr,otherwise A instr
    if (((instr & 0xE000u) >> 13u) ^ 0x7u) {
        hdt->state = hvm_decode;
        hdt->A_REG = instr;
        return;
    }
    // extract comp,dest,jmp parts of instr
    hdt->comp = EmitComp(instr);     // 1111111111000000
    hdt->dest = EmitDest(instr);     // 0000000000111000
    hdt->jmp =  EmitJmp(instr);      // 0000000000000111

    // turn machine state execute
    hdt->state = hvm_execute;
}

static int util_fd_isreg(const char *filename) {
    struct stat st;

    if (stat(filename, &st))
        return -1;

    return S_ISREG(st.st_mode);
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

