#pragma once

std::string regexExtract(const std::string &regexstr, const std::string &payload)
{
    std::regex regex(regexstr);
    std::smatch match;
    if (!std::regex_search(payload, match, regex) || match.size() != 2) {
        //std::cerr << "Failed to get match for '" << regexstr << "' in:\n" << payload << std::endl;
        return "";
    }
    return match[1].str();
}

bool extractNumber(const std::string &regex, const std::string &payload, double *number)
{
    const std::string numberString = regexExtract(regex, payload);
    if (numberString.empty()) {
        //std::cerr << "Failed to extract number '" << regex << "'" << std::endl;
        return false;
    }
    char *endptr = nullptr;
    const char *startptr = numberString.c_str();
    const double converted = strtod(startptr, &endptr);
    if (endptr == startptr || errno == ERANGE || !std::isfinite(converted)) {
        return false;
    }
    *number = converted;

    return true;
}


