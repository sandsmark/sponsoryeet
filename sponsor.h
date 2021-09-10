#pragma once
#include <iostream>
#include <regex>

struct Segment {
    double begin = 0.;
    double end = 0.;
};

static std::vector<double> splitToDouble(std::string string)
{
    for (char &c : string) {
        if (c == ',') {
            c = ' ';
        }
    }

    std::vector<double> ret;
    const char *begin = string.c_str();
    char* end = nullptr;
    for (double timestamp = std::strtod(begin, &end); begin != end; timestamp = std::strtod(begin, &end)) {
        begin = end;
        if (errno == ERANGE){
            std::cout << "range error, got " << timestamp << '\n';
            errno = 0;
            return {};
        }
        ret.push_back(timestamp);
    }
    return ret;
}

std::vector<Segment> downloadSegments(const std::string &videoId)
{
    if (videoId.empty()) {
        puts("Got empty videoId");
        return {};
    }
    std::string json = downloadFile("sponsor.ajay.app", 443, "/api/skipSegments?videoID=" + videoId);
    if (json.empty()) {
        puts("Failed to download segments to skip");
        return {};
    }

    std::regex segmentsRegex(R"--("segment"\s*:\[([^\]]+)\])--");
    std::vector<Segment> segments;
    std::sregex_iterator it = std::sregex_iterator(json.begin(), json.end(), segmentsRegex);
    for (; it != std::sregex_iterator(); it++) {
        const std::smatch match = *it;
        std::string segmentsArray = match[1].str();
        std::vector<double> numbers = splitToDouble(segmentsArray);
        if (numbers.size() != 2) {
            std::cout << "Invalid segment " << segmentsArray << std::endl;
            return {};
        }
        if (s_verbose) {
            std::cout << "Got segment " << numbers[0] << " -> " << numbers[1] << std::endl;
        }
        segments.push_back({numbers[0], numbers[1]});
    }
    std::cout << " - Got " << segments.size() << " skip segments for " << videoId << std::endl;

    return segments;
}

