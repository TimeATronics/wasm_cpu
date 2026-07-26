struct S { int x; int y; }; int main(void) { struct S s; int *p = (int*)&s; *p = 10; return s.x; }
