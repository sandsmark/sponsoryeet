#pragma once
#include "helpers.h"
#include <iostream>

struct Segment {
};

std::vector<double> downloadSegments(const std::string &videoId)
{
    const std::string json = downloadFile("sponsor.ajay.app", 443, "/api/skipSegments?videoID=" + videoId);
    if (json.empty()) {
        puts("Failed to download segments to skip");
        return false;
    }
    std::string segmentsArray = regexExtract(R"--("segment"\s*:\[([^\]]+)\])--", json);
    const char *begin = segmentsArray.c_str();
    const char* end = nullptr;
    std::vector<double> ret;
    for (double timestamp = std::strtod(begin, &end); begin != end; timestamp = std::strtod(begin, &end)) {
        begin = end;
        if (errno == ERANGE){
            std::cout << "range error, got ";
            std::cout << f << '\n';
            errno = 0;
            return {};
        }
        ret.push_back(timestamp);
    }

    return ret;
}

