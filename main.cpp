static bool s_running = true;

#define PROGRESS_WIDTH 20

extern "C" {
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
}

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

#include <vector>
#include <string>
#include <array>
#include <iostream>
#include <chrono>
#include <sstream>

#include "mdns.h"
#include "connection.h"
#include "chromecast.h"
#include "sponsor.h"

std::string regexExtract(const std::string &regexstr, const std::string &payload)
{
    std::regex regex(regexstr);
    std::smatch match;
    if (!std::regex_search(payload, match, regex) || match.size() != 2) {
        std::cerr << "Failed to get match for '" << regexstr << "' in:\n" << payload << std::endl;
        return "";
    }
    return match[1].str();
}

std::string getType(const std::string &payload)
{
    const std::regex typeRegex(R"--("type"\s*:\s*"([^"]+)")--");
    std::smatch typeMatch;
    if (!std::regex_search(payload, typeMatch, typeRegex) || typeMatch.size() != 2) {
        std::cerr << "Failed to get type!" << std::endl;
        return "";
    }
    std::string type = typeMatch[1].str();
    std::cout << "type: " << type << std::endl;

    return type;
}

bool extractNumber(const std::string &regex, const std::string &payload, double *number)
{
    const std::string numberString = regexExtract(regex, payload);
    if (numberString.empty()) {
        std::cerr << "Failed to extract number '" << regex << "'" << std::endl;
        return false;
    }
    char *endptr = nullptr;
    const char *startptr = numberString.c_str();
    const double converted = strtod(startptr, &endptr);
    if (endptr == startptr || errno == ERANGE) {
        return false;
    }
    *number = converted;

    return true;
}


static std::string currentVideo;
static double currentPosition = -1.;
static double currentDuration = -1.;
static double lastPositionFetched = -1;
static std::vector<Segment> currentSegments;
static double nextSegmentStart = -1.;

static double secondsUntilNextSegment()
{
    if (currentPosition < 0) {
        return -1;
    }
    if (currentSegments.empty()) {
        return -1;
    }

    double lowestBegin = -1;//currentSegments[0].begin;
    for (const Segment &segment : currentSegments) {
        if (segment.end < currentPosition) {
            continue;
        }
        if (segment.begin < lowestBegin) {
            lowestBegin = segment.begin;
        }
    }
    return lowestBegin;
}

static double currentSegmentEnd()
{
    if (currentPosition < 0) {
        return -1;
    }
    if (currentSegments.empty()) {
        return -1;
    }

    double lowestBegin = currentSegments[0].begin;
    for (const Segment &segment : currentSegments) {
        if (segment.end < currentPosition) {
            continue;
        }
        if (segment.begin < currentPosition) {
            return segment.end;
        }
    }
    return -1;
}

static void maybeSeek(Connection *connection)
{
    if (currentVideo.empty()) {
        return;
    }
    if (currentSegments.empty()) {
        return;
    }
    const double segmentEnd = currentSegmentEnd();
    if (segmentEnd > 0) {
        currentPosition = -1.;
        nextSegmentStart = -1.;
        cc::seek(*connection, segmentEnd);
        cc::sendSimple(*connection, cc::msg::GetStatus, cc::ns::Media);
    }
}

void printTimestamp(int timestamp)
{
    const int seconds = timestamp % 60;
    timestamp /= 60;
    const int minutes = timestamp % 60;
    timestamp /= 60;
    const int hours = timestamp % 60;
    printf("%.2d:%.2d:%.2d", hours, minutes, seconds);
}

void printProgress(double position, double length)
{
    if (position < 0 || length < 0 || lastPositionFetched < 0) {
        //printf("\rInvalid position or length");
        //fflush(stdout);
        return;
    }
    double timeDelta = time(nullptr) - lastPositionFetched;
    position = std::round(timeDelta + position);
    printf("\r[");
    int playedLength = position * PROGRESS_WIDTH / length;
    for (int i=0; i<playedLength; i++) {
        printf("=");
    }
    for (int i=playedLength; i<PROGRESS_WIDTH; i++) {
        printf("-");
    }
    printf("] ");
    printTimestamp(position);
    printf("/");
    printTimestamp(length);
    fflush(stdout);
}

bool handleMessage(Connection *connection, std::istringstream *istr)
{
    if (istr->eof()) {
        return false;
    }
    if (!istr->good()) {
        puts("No good buffer");
        return false;
    }
    cast_channel::CastMessage message;
    if (!message.ParseFromIstream(istr)) {
        puts("PArsing failed");
        return false;
    }

    //std::cout << message.source_id() << " > " << message.destination_id() << " (" << message.namespace_() << "): \n" << message.payload_utf8() << std::endl;
    if (!message.has_payload_utf8()) {
        puts("No string payload");
        return true;
    } else if (message.has_payload_binary()) {
        puts("Got binary message, ignoring");
        return true;
    }
    const std::string payload = message.payload_utf8();
    std::string type = regexExtract(R"--("type"\s*:\s*"([^"]+)")--", payload);
    if (type == "CLOSE") {
        cc::sendSimple(*connection, cc::msg::Connect, cc::ns::Connection);
        return true;
    }
    if (type == "MEDIA_STATUS") {
        std::string videoID = regexExtract(R"--("contentId"\s*:\s*"([^"]+)")--", payload);
        if (videoID.empty()) {
            std::cerr << "Failed to find video id" << std::endl;
            return true;
        }
        if (videoID != currentVideo) {
            currentSegments = downloadSegments(videoID);
            currentVideo = videoID;
            nextSegmentStart = -1;
        }

        std::string currentPositionStr = regexExtract(R"--("currentTime"\s*:\s*([0-9.]+))--", payload);
        if (currentPositionStr.empty()) {
            std::cerr << "Failed to find current time" << std::endl;
            currentPosition = -1.;
            return true;
        }
        currentPosition = strtod(currentPositionStr.c_str(), nullptr);

        std::string currentDurationStr = regexExtract(R"--("duration"\s*:\s*([0-9.]+))--", payload);
        if (currentDurationStr.empty()) {
            std::cerr << "Failed to find current duration" << std::endl;
            currentDuration = -1.;
            return true;
        }
        currentDuration = strtod(currentDurationStr.c_str(), nullptr);

        if (errno == ERANGE) {
            std::cerr << "Invalid current position " << currentPositionStr << std::endl;
            currentPosition = -1.;
            return true;
        }
        lastPositionFetched = time(nullptr);
        if (!currentSegments.empty()) {
            double delta = secondsUntilNextSegment();
            if (delta >= 0) {
                nextSegmentStart = time(nullptr) + delta;
                maybeSeek(connection);
            }
        }
        return true;
    }

    if (message.namespace_() == cc::ns::strings[cc::ns::Heartbeat]) {
        if (type == "PING") {
            //puts("Got ping, sending pong");
            cc::sendSimple(*connection, cc::msg::Pong, cc::ns::Heartbeat);
            //cc::sendSimple(*connection, cc::msg::Ping, cc::ns::Heartbeat);
        }
        return true;
    }
    if (message.namespace_() == cc::ns::strings[cc::ns::Receiver]) {
        if (type == "RECEIVER_STATUS") {
            const std::string displayName = regexExtract(R"--("displayName"\s*:\s*"([^"]+)")--", payload);
            const std::string sessionId = regexExtract(R"--("sessionId"\s*:\s*"([^"]+)")--", payload);
            std::cout << "got display name " << displayName << std::endl;
            std::cout << "session: " <<  sessionId << std::endl;
            cc::dest = sessionId;
            if (displayName == "YouTube") {
                puts("Correct");
                cc::sendSimple(*connection, cc::msg::GetStatus, cc::ns::Media);
            }
        }
        return true;
    }
    std::cout << "Got type: " << type << std::endl;

    return true;
}

// TODO: automatically reconnect
int loop(const sockaddr_in &address)
{
    cc::dest = "";

    Connection connection;
    if (!connection.connect(address)) {
        std::cerr << "Failed to connect to " << inet_ntoa(address.sin_addr) << std::endl;
        return errno;
    }
    if (!cc::sendSimple(connection, cc::msg::Connect, cc::ns::Connection)) {
        puts("Failed to send connect message");
        return errno;
    }
    puts("Connected");
    if (!cc::sendSimple(connection, cc::msg::GetStatus, cc::ns::Receiver)) {
        puts("Failed to send getstatus message");
        return errno;
    }

    while (s_running && !connection.eof) {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(connection.fd, &fdset);

        // idk, 1 second debug mostly for debugging
        timeval timeout;
        double currentTime = time(nullptr);
        if (nextSegmentStart > 0 && nextSegmentStart > currentTime) {
            std::cout << "Next segment at " << nextSegmentStart << ", current time " << currentTime << std::endl;
            timeout.tv_sec = nextSegmentStart - currentTime;
        } else {
            timeout.tv_sec = 1;
        }
        timeout.tv_usec = 0;
        const int events = select(connection.fd + 1, &fdset, 0, 0, &timeout);
        printProgress(currentPosition, currentDuration);

        // debugging
        if (errno) {
            perror("errno while selecting");
        }
        if (events < 0) {
            perror("Error when waiting for read");
            return errno;
        }
        if (errno == EINTR || events == 0) {
            continue;
        }
        std::string msgLengthBuffer = connection.read(sizeof(uint32_t));
        if (msgLengthBuffer.size() < 4) {
            std::cerr << "Failed to read message size: '" << msgLengthBuffer << "'" << std::endl;
            return EBADMSG;
        }
        if (connection.eof) {
            puts("Connection closed");
            return ECONNRESET;
        }
        uint32_t msgLength = 0;
        memcpy(&msgLength, msgLengthBuffer.data(), sizeof msgLength);
        msgLength = ntohl(msgLength);
        //printf("Message length: %d\n", msgLength);

        std::string response = connection.read(msgLength);
        if (response.empty()) {
            puts("No message received");
            return EINVAL;
        }
        if (response.size() < msgLength) {
            std::cerr << "Short read, expected " << msgLength << " got " << response.size() << std::endl;
            return EBADMSG;
        }
        std::istringstream istr(response);
        while (handleMessage(&connection, &istr)) {
            if (connection.eof) {
                return ECONNRESET;
            }
        }
        currentTime = time(nullptr);
        if (nextSegmentStart > 0 && nextSegmentStart < currentTime) {
            // Check if we need to seek
            cc::sendSimple(connection, cc::msg::GetStatus, cc::ns::Media);
        }
        //try {
        //    while (handleMessage(&response) && !response.empty()) { }
        //} catch (const std::exception &e) {
        //    puts(e.what());
        //    return EBADMSG;
        //}
    }
    return 0;
}

void signalHandler(int sig)
{
    signal(sig, SIG_DFL);
    s_running = false;
    puts("Bye");
}

int main(int argc, char *argv[])
{
    signal(SIGINT, &signalHandler);
    signal(SIGTERM, &signalHandler);
    signal(SIGQUIT, &signalHandler);

    int ret = 0;
    while (s_running) {
        if (ret != 0) {
            puts("Disconnected, sleeping and re-connecting");
            sleep(10);
        }
        sockaddr_in address;
        if (!mdns::findChromecast(&address)) {
            return ENOENT;
        }
        ret = loop(address);
    }


    return ret;
}
