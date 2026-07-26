struct S { int x; int y; };
int main() {
  struct S s = (struct S){10, 20};
  return s.x + s.y;
}
