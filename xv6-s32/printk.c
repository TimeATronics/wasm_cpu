#include "types.h"
#include "defs.h"
#include "s32.h"

static char digits[] = "0123456789abcdef";

static void printint(int xx, int base, int sign) {
  char buf[16];
  int i;
  unsigned int x;

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    consputc(buf[i]);
}

static void printptr(unsigned int x) {
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < 8; i++) {
    consputc(digits[(x >> 28) & 0xf]);
    x <<= 4;
  }
}

void printk(const char *fmt, int a1, int a2, int a3, int a4) {
  int i, c;
  int *argp = &a1;
  int argidx = 0;

  for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
    if (c != '%') {
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if (c == 0) break;

    int val = 0;
    int is_ptr = 0;
    switch (c) {
    case 'd':
      if (argidx < 4) val = *(&a1 + argidx++);
      printint(val, 10, 1);
      break;
    case 'x':
    case 'p':
      if (argidx < 4) val = *(&a1 + argidx++);
      printptr(val);
      break;
    case 's':
      if (argidx < 4) {
        char *s = (char*)*(&a1 + argidx++);
        if (s == 0) s = "(null)";
        while (*s) consputc(*s++);
      }
      break;
    case 'c':
      if (argidx < 4) { val = *(&a1 + argidx++); consputc(val); }
      break;
    case '%':
      consputc('%');
      break;
    default:
      consputc('%');
      consputc(c);
      break;
    }
  }
}

void panic(char *s) {
  intr_off();
  printk("panic: %s\n", (int)s, 0, 0, 0);
  for (;;);
}
