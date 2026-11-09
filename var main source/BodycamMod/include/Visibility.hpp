#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "UETypes.hpp"

void Visibility_Update();

bool Visibility_IsPawnVisible(uintptr_t pawn);

bool Visibility_LineOfSightActive();

uint32_t Visibility_ProbeStart(const FVector& start, const FVector& dir, double maxDist);
int      Visibility_ProbeResult(uint32_t id, double& dist);
