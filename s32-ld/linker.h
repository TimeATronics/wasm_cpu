#ifndef S32LD_LINKER_H
#define S32LD_LINKER_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char **obj_paths;
    int obj_count;
    const char *output_path;
    const char *entry_sym;
    bool verbose;
} LinkerConfig;

bool linker_link(LinkerConfig *cfg);

#endif
