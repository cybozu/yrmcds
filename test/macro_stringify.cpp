#include <stdio.h>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)
int main(int argc, char** argv) {
    puts(EXPAND_AND_QUOTE(DEFAULT_CONFIG));
    return 0;
}
