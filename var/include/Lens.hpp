#pragma once
#include <string>

struct LensParams {
    bool  valid = false;
    int   method = 0;
    float intensity = 0, start = 0, end = 1;
    bool  invert = false;
    float k1 = 0, k2 = 0, k3 = 0, k4 = 0, p1 = 0, p2 = 0;
    float magnifier = 1;
};

void        Lens_Update();
const LensParams& Lens_Get();
void        Lens_Apply(float& x, float& y, float w, float h);
std::string Lens_Status();
