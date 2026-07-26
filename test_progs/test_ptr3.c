int main(void) { int arr[3]; int *p = arr; *p = 10; *(p+1) = 20; return *p + *(p+1); }
