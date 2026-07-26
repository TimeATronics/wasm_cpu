#ifndef S32CC_PREPROCESS_H
#define S32CC_PREPROCESS_H

#include <stdbool.h>

/* A simple C preprocessor supporting #include, #define, #ifdef/#ifndef/#endif */

/* Preprocess an input file, writing expanded output to a temp file.
 * Returns the path to the temp file (caller must free), or NULL on error. */
char *preprocess(const char *filename);

/* Preprocess from a string buffer, returns expanded buffer (caller must free) */
char *preprocess_string(const char *source, const char *base_dir);

#endif
