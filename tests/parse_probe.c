// Native harness for feeding real mock HTTP bodies through the firmware parser.
#include "box_protocol.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    box_response_t r;
    box_response_reset(&r);
    char chunk[7];
    size_t length;
    while ((length = fread(chunk, 1, sizeof(chunk), stdin)) > 0) box_response_append(&r, chunk, length);
    r.finished = atoi(argv[2]) != 0;
    box_light_t light;
    if (box_response_parse(&r, true, atoi(argv[1]), r.finished, &light)) puts(box_light_name(light));
    else puts("REJECT");
    return 0;
}
