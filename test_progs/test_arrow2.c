struct S { int x; int y; }; int main(void) { struct S s; struct S *p = &s; p->x = 10; p->y = 20; return p->x + p->y; }
