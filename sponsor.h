#pragma once

struct Segment {
};

bool downloadSegments(const std::string &videoId)
{
    const std::string json = downloadFile("sponsor.ajay.app", 443, "/api/skipSegments?videoID=" + videoId);
    if (json.empty()) {
        puts("Failed to download segments to skip");
        return false;
    }
    return true;
}

