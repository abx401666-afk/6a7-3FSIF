#pragma once
#include <cstdint>

#define BCVIS_MAP_NAME L"Local\\BodycamModVisTrace_v3"

namespace VisShared {
    constexpr uint32_t kMagic   = 0x53495642;
    constexpr uint32_t kVersion = 3;
    constexpr int      kMaxRays = 256;

    struct Ray {
        double   sx, sy, sz;
        double   ex, ey, ez;
        uint64_t target;
    };

    struct Offsets {
        int64_t gobjects, gworld, appendString, processEvent, processEventIdx;
        int64_t objClass, objName, objOuter, fieldNext, structSuper, structChildren, classDefaultObject;
        int64_t objArrayNum, worldPersistentLevel, worldGameInstance, giLocalPlayers, lpController, pcAckPawn, actorOwner;
        int64_t hitLocation, hitComponent;
    };

    struct Block {
        uint32_t magic;
        uint32_t version;
        volatile uint32_t reqSeq;
        uint32_t          count;
        uint32_t          enabled;
        volatile uint32_t offsetsReady;
        Offsets           off;
        Ray               rays[kMaxRays];
        volatile uint32_t resSeq;
        uint32_t          resCount;
        uint64_t          resTarget[kMaxRays];
        uint8_t           resVisible[kMaxRays];
        volatile uint64_t heartbeat;
        volatile uint32_t dllState;
        uint32_t          tracesDone;
        uint32_t          pid;
        uint32_t          pad1;
        char              status[128];
    };
}
