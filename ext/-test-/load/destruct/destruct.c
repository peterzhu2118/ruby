#include <stdio.h>
#include "ruby/ruby.h"

void
Init_destruct(void)
{}

void
Destruct_destruct(void)
{
    printf("Calling Destruct_destruct\n");
}
