int *get_ptr(int *p) { return p; } int main(void) { int x = 42; int *q = get_ptr(&x); return *q; }
