#include <stdio.h>

int main() {
    printf("Now 'printf' & 'getchar' addresses are resolved. Press ENTER key after running 'restore_got'...");
    getchar();
    printf("Now they are resolved again. Press ENTER key to exit...");
    getchar();
    return 0;
}
