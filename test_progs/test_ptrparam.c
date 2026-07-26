void set_val(int *p, int v) { *p = v; } int main(void) { int x = 0; set_val(&x, 42); return x; }
