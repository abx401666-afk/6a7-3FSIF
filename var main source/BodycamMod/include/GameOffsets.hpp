#pragma once
#include <cstddef>
#include <cstdint>

enum OffsetId : int {
#define OFFSET(name, value, spec) Off_##name,
#include "Offsets.inc"
#undef OFFSET
    Off_Count
};

struct OffsetEntry {
    char    spec[112];
    int64_t value;
};
static_assert(sizeof(OffsetEntry) == 120);

struct OffsetTable {
    char     magic[32];
    uint32_t version;
    uint32_t count;
    uint32_t entrySize;
    uint32_t reserved;
    char     sdkStamp[64];
    OffsetEntry e[Off_Count];
};

extern OffsetTable g_offsets;

#define OFF(name) (g_offsets.e[Off_##name].value)
