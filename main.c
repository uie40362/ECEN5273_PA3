#include <stdlib.h>
#include <stdio.h>
#include "string.h"

struct mystruct{
    int myint;
};
int main(){
    struct mystruct * test = NULL;
    printf("%d", test);
    return 0;
};
