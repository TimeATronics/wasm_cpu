struct S { int x; int y; }; int main(void) { struct S s; struct S *p = &s; p->x = 42; return p->x; }
