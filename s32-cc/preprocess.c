#include "preprocess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Simple text-based preprocessor.
 * Handles: #define NAME value, #include "file", #ifdef/#ifndef/#endif
 * Returns a newly allocated expanded string. */

typedef struct Macro { char *name; char *value; struct Macro *next; } Macro;
static Macro *macros = NULL;

static void macro_set(const char *name, const char *value) {
    for (Macro *m = macros; m; m = m->next) {
        if (strcmp(m->name, name) == 0) { free(m->value); m->value = strdup(value); return; }
    }
    Macro *m = malloc(sizeof(Macro));
    m->name = strdup(name);
    m->value = strdup(value);
    m->next = macros;
    macros = m;
}

static const char *macro_get(const char *name) {
    for (Macro *m = macros; m; m = m->next)
        if (strcmp(m->name, name) == 0) return m->value;
    return NULL;
}

static bool macro_defined(const char *name) {
    return macro_get(name) != NULL;
}

/* Expand macros in a string. Simple object-like macros only. */
static char *expand_line(const char *line) {
    int cap = 4096, len = 0;
    char *out = malloc(cap);
    const char *p = line;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            int nlen = (int)(p - s);
            char name[256];
            memcpy(name, s, nlen); name[nlen] = 0;
            const char *val = macro_get(name);
            if (val) {
                int vlen = strlen(val);
                if (len + vlen + 1 > cap) { cap = cap * 2 + vlen; out = realloc(out, cap); }
                memcpy(out + len, val, vlen); len += vlen;
            } else {
                if (len + nlen + 1 > cap) { cap = cap * 2 + nlen; out = realloc(out, cap); }
                memcpy(out + len, s, nlen); len += nlen;
            }
        } else {
            if (len + 2 > cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = *p++;
        }
    }
    out[len] = 0;
    return out;
}

/* Handle #include by reading the file and returning its content */
static char *do_include(const char *line, const char *base_dir) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '#') return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "include", 7) != 0) return NULL;
    p += 7;
    while (*p && isspace((unsigned char)*p)) p++;
    
    char delim = *p;
    if (delim != '"' && delim != '<') return NULL;
    char endc = (delim == '"') ? '"' : '>';
    p++;
    const char *fs = p;
    while (*p && *p != endc && *p != '\n' && *p != '\r') p++;
    if (*p != endc) return NULL;
    int flen = (int)(p - fs);
    char *fname = malloc(flen + 1);
    memcpy(fname, fs, flen); fname[flen] = 0;
    
    FILE *f = NULL;
    char path[1024];
    if (delim == '"' && base_dir) { snprintf(path, sizeof(path), "%s/%s", base_dir, fname); f = fopen(path, "r"); }
    if (!f) f = fopen(fname, "r");
    free(fname);
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *content = malloc(sz + 1);
    fread(content, 1, sz, f); content[sz] = 0;
    fclose(f);
    return content;
}

char *preprocess_string(const char *source, const char *base_dir) {
    int cap = 65536, out_len = 0;
    char *output = malloc(cap);
    output[0] = 0;
    
    /* Strip \r from source */
    int slen = strlen(source);
    char *clean = malloc(slen + 1);
    int clen = 0;
    for (int i = 0; i < slen; i++)
        if (source[i] != '\r') clean[clen++] = source[i];
    clean[clen] = 0;
    source = clean;
    
    bool skipping = false;
    const char *s = source;
    
    while (*s) {
        /* Skip blank lines */
        while (*s == '\n' || *s == '\r') s++;
        if (!*s) break;
        
        /* Find end of line */
        const char *le = s;
        while (*le && *le != '\n' && *le != '\r') le++;
        int llen = (int)(le - s);
        
        /* Check if it's a #directive */
        const char *ls = s;
        while (ls < le && isspace((unsigned char)*ls)) ls++;
        
        if (ls < le && *ls == '#') {
            ls++;
            while (ls < le && isspace((unsigned char)*ls)) ls++;
            
            if (strncmp(ls, "include", 7) == 0) {
                char *line = malloc(llen + 1);
                memcpy(line, s, llen); line[llen] = 0;
                char *inc = do_include(line, base_dir);
                free(line);
                if (inc) {
                    char *exp = preprocess_string(inc, base_dir);
                    int elen = strlen(exp);
                    if (out_len + elen + 2 > cap) { cap = cap * 2 + elen; output = realloc(output, cap); }
                    memcpy(output + out_len, exp, elen); out_len += elen;
                    if (out_len > 0 && output[out_len-1] != '\n') output[out_len++] = '\n';
                    free(exp); free(inc);
                }
            } else if (strncmp(ls, "define", 6) == 0) {
                ls += 6;
                while (ls < le && isspace((unsigned char)*ls)) ls++;
                if (ls < le && (isalpha((unsigned char)*ls) || *ls == '_')) {
                    const char *ns = ls;
                    while (ls < le && (isalnum((unsigned char)*ls) || *ls == '_')) ls++;
                    int nlen = (int)(ls - ns);
                    char name[256];
                    memcpy(name, ns, nlen); name[nlen] = 0;
                    while (ls < le && isspace((unsigned char)*ls)) ls++;
                    char *val = malloc(le - ls + 1);
                    memcpy(val, ls, le - ls); val[le - ls] = 0;
                    macro_set(name, val);
                    free(val);
                }
            } else {
                /* Unknown directive - pass through as-is */
                if (out_len + llen + 2 > cap) { cap = cap * 2 + llen; output = realloc(output, cap); }
                memcpy(output + out_len, s, llen); out_len += llen;
                const char *next = le;
                while (*next == '\n' || *next == '\r') next++;
                if (*next) output[out_len++] = '\n';
            }
            
            /* Skip to next line */
            s = le;
            while (*s == '\n' || *s == '\r') s++;
            continue;
        }
        
        /* Not a directive - expand macros */
        if (!skipping) {
            char *line = malloc(llen + 1);
            memcpy(line, s, llen); line[llen] = 0;
            char *exp = expand_line(line);
            int elen = strlen(exp);
            if (out_len + elen + 2 > cap) { cap = cap * 2 + elen; output = realloc(output, cap); }
            memcpy(output + out_len, exp, elen); out_len += elen;
            /* Add newline only if there are more lines after this */
            const char *next = le;
            while (*next == '\n' || *next == '\r') next++;
            if (*next) output[out_len++] = '\n';
            free(exp); free(line);
        }
        
        s = le;
        while (*s == '\n' || *s == '\r') s++;
    }
    
    output[out_len] = 0;
    free(clean);
    return output;
}

char *preprocess(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(sz + 1);
    fread(src, 1, sz, f); src[sz] = 0;
    fclose(f);
    
    char base_dir[1024] = ".";
    const char *slash = strrchr(filename, '/');
    if (!slash) slash = strrchr(filename, '\\');
    if (slash) { int d = (int)(slash - filename); memcpy(base_dir, filename, d); base_dir[d] = 0; }
    
    char *result = preprocess_string(src, base_dir);
    free(src);
    return result;
}
