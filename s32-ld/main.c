#include "linker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-o output] [-e entry] file1.o [file2.o ...]\n", prog);
    fprintf(stderr, "  -o output  Output file (default: a.out)\n");
    fprintf(stderr, "  -e entry   Entry point symbol (default: _start)\n");
}

int main(int argc, char **argv) {
    LinkerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.entry_sym = "_start";
    cfg.output_path = "a.out";

    char **obj_paths = NULL;
    int obj_count = 0, obj_cap = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            cfg.output_path = argv[++i];
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
            cfg.entry_sym = argv[++i];
        else if (argv[i][0] != '-') {
            if (obj_count >= obj_cap) {
                obj_cap = obj_cap ? obj_cap * 2 : 16;
                obj_paths = realloc(obj_paths, sizeof(char*) * obj_cap);
            }
            obj_paths[obj_count++] = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (obj_count == 0) {
        usage(argv[0]);
        return 1;
    }

    cfg.obj_paths = obj_paths;
    cfg.obj_count = obj_count;

    bool ok = linker_link(&cfg);

    free(obj_paths);
    return ok ? 0 : 1;
}
