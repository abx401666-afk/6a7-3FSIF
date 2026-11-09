#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct MediaInfo {
    bool        available = false;
    bool        playing = false;
    std::string title, artist, album, source;
    double      position = 0.0;
    double      duration = 0.0;
    uint32_t    artVersion = 0;
};

enum MediaCommand { Media_None = 0, Media_PlayPause, Media_Next, Media_Previous };

bool Media_Get(MediaInfo& out);
uint32_t Media_GetArt(std::vector<uint8_t>& rgba);
void Media_Send(MediaCommand cmd);
