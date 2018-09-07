#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "utils.h"


void hvm_error(const char *fmt, int severity, ...) {
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    (severity == Fatal || severity == Error) ? exit(EXIT_FAILURE) : NULL;
}


FILE *hvm_fopen(const char *filename, const char *modes) {
    FILE *f;
    f = fopen(filename, modes);
    (!f) ? hvm_error("unable to open file", Error) : NULL;
    return f;
}



int hvm_fclose(FILE *fp) {
    if (fclose(fp) == EOF) {
        hvm_error("error closing file", Error);
        return EOF;
    }
    return 0;

}

int fd_isreg(const char *filename) {
    struct stat st;

    if (stat(filename, &st))
        return -1;

    return S_ISREG(st.st_mode);
}
