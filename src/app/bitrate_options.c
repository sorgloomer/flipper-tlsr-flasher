
#include <stdint.h>

#include "src/app/bitrate_options.h"

/*
import math
def round_to_2(x):
    rounder = 10 ** max(int(math.floor(math.log10(abs(x)))) - 1, 0)
    return round(x / rounder) * rounder
[round_to_2(960000 * 10 ** (-i/4)) for i in range(0, 21)][::-1]
*/
const uint32_t BitrateOptions__values[] = {10,    17,    30,    54,     96,     170,    300,
                                           540,   960,   1700,  3000,   5400,   9600,   17000,
                                           30000, 54000, 96000, 170000, 300000, 540000, 960000};
const uint32_t BitrateOptions__count =
    sizeof(BitrateOptions__values) / sizeof(BitrateOptions__values[0]);
const uint32_t BitrateOptions__default = 170000;
