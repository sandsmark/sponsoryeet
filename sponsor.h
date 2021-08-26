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
        std::cout << "Got timestamp " << timestamp << std::endl;
        ret.push_back(timestamp);
    }
    return ret;
}

std::vector<Segment> downloadSegments(const std::string &videoId)
{
    const std::string json = downloadFile("sponsor.ajay.app", 443, "/api/skipSegments?videoID=" + videoId);
    if (json.empty()) {
        puts("Failed to download segments to skip");
        return {};
    }

    std::regex segmentsRegex(R"--("segment"\s*:\[([^\]]+)\])--");
    std::smatch match;
    std::vector<Segment> segments;
    while(std::regex_search(json, match, segmentsRegex)) {
        if (match.size() != 2) {
            std::cout << "Invalid segments array '" << json << "'" << std::endl;
            return {};
        }
        std::string segmentsArray = match[1].str();
        std::vector<double> numbers = splitToDouble(segmentsArray);
        if (numbers.size() != 2) {
            std::cout << "Invalid segment " << segmentsArray << std::endl;
            return {};
        }
        std::cout << "Got segment " << numbers[0] << " -> " << numbers[1] << std::endl;
        segments.push_back({numbers[0], numbers[1]});
    }

    return segments;
}

