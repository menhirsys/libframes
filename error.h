#ifndef __ERROR_H__
#define __ERROR_H__

#include <stdio.h>
#include <stdlib.h>

#define str(s) #s
#define xstr(s) str(s)

#define MSG(s) puts(__FILE__ ", L" xstr(__LINE__) ": " s "\n");

#define EXPECT(e1, e2) \
    do { \
        int v1 = (e1); \
        if (v1 != e2) { \
            printf(__FILE__ ", L" xstr(__LINE__) ": expected " xstr(e2) ", got: %i\n", v1); \
            exit(1); \
        } \
    } while(0);

#endif
