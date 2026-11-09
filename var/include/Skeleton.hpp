#pragma once
#include "UETypes.hpp"
#include <cstdint>
#include <utility>
#include <vector>

struct SkeletonPose {
    std::vector<std::pair<FVector, FVector>> lines;
};

bool Skeleton_GetPose(uintptr_t pawn, SkeletonPose& out);

struct SkeletonAimBones {
    FVector head{}, neck{}, chest{}, pelvis{}, legs{};
    bool hasHead = false, hasNeck = false, hasChest = false, hasPelvis = false, hasLegs = false;
};
bool Skeleton_GetAimBones(uintptr_t pawn, SkeletonAimBones& out);


std::vector<std::string> Skeleton_ClassChain(uintptr_t obj);

std::string Skeleton_FName(uint32_t comparisonIndex);
