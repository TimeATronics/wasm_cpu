struct P { int x; int y; }; int main(void) { struct P p; struct P *q = &p; q->x = 10; q->y = 20; return q->x + q->y; }
