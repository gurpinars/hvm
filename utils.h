#ifndef HVM_UTILS_H
#define HVM_UTILS_H
#include <stdio.h>

#define read_msb(_val_) (((_val_) << 8)|((_val_) >> 8)) /* read byte MSB */

enum error_severity {
    Fatal,
    Error,
    Warning,
    Info
};

void hvm_error(const char *, int, ...);
int hvm_fclose(FILE *);
FILE* hvm_fopen(const char *, const char *);
int fd_isreg(const char *);

#endif // HVM_UTILS_H
