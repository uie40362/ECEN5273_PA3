#include <stdlib.h>
#include <stdio.h>
#include "string.h"

int main(){
    char mystring[] = "line1\r\nline2\r\n\r\n";
    char * line;
    line = strtok(mystring, "\r\n");
    printf("%s\n", line);
    while (line){
        line = strtok(NULL, "\r\n");
    }
    return 0;
};
