#include "types.h"
#include "defs.h"
#include "s32.h"

static char digits[] = "0123456789abcdef";

static void printint(int xx, int base, int sign) {
  /* TODO: codegen bugs with shl/div/mul - skip for now */
}

static void printptr(unsigned int x) {
  /* TODO: codegen bugs with shift ops - skip for now */
  consputc('0');
  consputc('x');
}

void printk(char *fmt, int a1, int a2, int a3, int a4) {
  int i;
  int c;
  int val;
  int fmt_idx;

  fmt_idx = 0;
  while (1) {
    c = fmt[fmt_idx];
    c = c & 0xff;
    if (c == 0) break;

    if (c != '%') {
      consputc(c);
      fmt_idx = fmt_idx + 1;
      continue;
    }

    fmt_idx = fmt_idx + 1;
    c = fmt[fmt_idx];
    c = c & 0xff;
    if (c == 0) break;

    if (c == 'd') {
      val = a1;
      a1 = a2;
      a2 = a3;
      a3 = a4;
      a4 = 0;
      printint(val, 10, 1);
    } else if (c == 'x' || c == 'p') {
      val = a1;
      a1 = a2;
      a2 = a3;
      a3 = a4;
      a4 = 0;
      printptr(val);
    } else if (c == 's') {
      char *s = (char*)a1;
      a1 = a2;
      a2 = a3;
      a3 = a4;
      a4 = 0;
      if (s == 0) {
        consputc('(');
        consputc('n');
        consputc('u');
        consputc('l');
        consputc('l');
        consputc(')');
      } else {
        while (*s) {
          consputc(*s);
          s = s + 1;
        }
      }
    } else if (c == 'c') {
      val = a1;
      a1 = a2;
      a2 = a3;
      a3 = a4;
      a4 = 0;
      consputc(val);
    } else if (c == '%') {
      consputc('%');
    } else {
      consputc('%');
      consputc(c);
    }
    fmt_idx = fmt_idx + 1;
  }
}

void panic(char *s) {
  printk("panic: ", (int)s, 0, 0, 0);
  printk("\n", 0, 0, 0, 0);
  for (;;);
}
