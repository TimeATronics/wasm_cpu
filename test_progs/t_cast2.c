struct s { double d; long l; };
int main() {
    struct s x;
    return (int)(double)x.l;
}
