#pragma once
#include <cstdlib>
#include "compiler/hardcoded_functions.h"
#include "compiler/datatypes.h"

struct Unsized_Array {void* data; i32 size;};

void _upp_main();
void _upp_main()
{
    i32 a;
    i32 _upp_int_expr1;
    bool _upp_int_expr2;
    a = 5;
    _upp_int_expr1 = 7;
    _upp_int_expr2 = (a) > (_upp_int_expr1);
    if (_upp_int_expr2) {
        a = 8;
    }
    print_i32(a);
    exit(0);

}

 int main(int argc, const char** argv) {
    _upp_main();
    return 0;
}