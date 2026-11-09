#include "../include/GameOffsets.hpp"

OffsetTable g_offsets = {
    "VAR-OFFSET-TABLE-1\x00\x7f\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa",
    1, Off_Count, sizeof(OffsetEntry), 0,
    "",
    {
#define OFFSET(name, value, spec) { spec, static_cast<int64_t>(value) },
#include "../include/Offsets.inc"
#undef OFFSET
    }
};
