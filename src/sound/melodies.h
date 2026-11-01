#pragma once
#include "app/App.h"

/* Named melodies (arrays of BuzzerNote; freq 0 = rest).
 * Returns the array and its element count. */
const BuzzerNote *melodyOginski(int *count);
const BuzzerNote *melodyJingle(int *count);
const BuzzerNote *melodySvoi(int *count);

