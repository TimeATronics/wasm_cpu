#include "types.h"
#include "defs.h"

void *memset(void *dst, int c, uint n) {
  char *cdst = (char*)dst;
  for (uint i = 0; i < n; i++)
    cdst[i] = (char)c;
  return dst;
}

int memcmp(const void *v1, const void *v2, uint n) {
  unsigned char *s1 = (unsigned char*)v1;
  unsigned char *s2 = (unsigned char*)v2;
  while (n-- > 0) {
    if (*s1 != *s2) return *s1 - *s2;
    s1++; s2++;
  }
  return 0;
}

void *memmove(void *dst, const void *src, uint n) {
  char *s = (char*)src;
  char *d = (char*)dst;
  if (s < d && s + n > d) {
    s += n; d += n;
    while (n-- > 0) *--d = *--s;
  } else {
    while (n-- > 0) *d++ = *s++;
  }
  return dst;
}

void *memcpy(void *dst, const void *src, uint n) {
  return memmove(dst, src, n);
}

int strncmp(char *p, char *q, uint n) {
  while (n > 0 && *p && *p == *q)
    { n--; p++; q++; }
  if (n == 0) return 0;
  return (unsigned char)*p - (unsigned char)*q;
}

char *strncpy(char *s, const char *t, int n) {
  char *os = s;
  while (n-- > 0 && (*s++ = *t++) != 0);
  while (n-- > 0) *s++ = 0;
  return os;
}

char *safestrcpy(char *s, const char *t, int n) {
  char *os = s;
  if (n <= 0) return os;
  while (--n > 0 && (*s++ = *t++) != 0);
  *s = 0;
  return os;
}

int strlen(const char *s) {
  int n;
  for (n = 0; s[n]; n++);
  return n;
}
