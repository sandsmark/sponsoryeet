static bool s_running = true;

#define PROGRESS_WIDTH 40

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
        //std::cerr << "Failed to get match for '" << regexstr << "' in:\n" << payload << std::endl;
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


static std::string currentVideo;
static double s_currentPosition = -1.;
static double currentDuration = -1.;
static double s_lastPositionFetched = -1;
static std::vector<Segment> currentSegments;
static double nextSegmentStart = -1.;
static bool currentlyPlaying = false;


static double currentPosition()
{
    if (!currentlyPlaying) {
        return s_currentPosition;
    }

    if (s_currentPosition < 0) {
        return -1;
    }
    double timeDelta = time(nullptr) - s_lastPositionFetched;
    return std::round(timeDelta + s_currentPosition);
}

static double secondsUntilNextSegment()
{
    const double current = currentPosition();
    if (current < 0) {
        puts("no current position");
        return -1;
    }
    if (currentSegments.empty()) {
        return -1;
    }

    double lowestBegin = currentSegments[0].begin;
    for (const Segment &segment : currentSegments) {
        if (segment.end < current) {
            continue;
        }
        if (segment.begin < lowestBegin) {
            lowestBegin = segment.begin;
        }
    }
    return lowestBegin - current;
}

static double currentSegmentEnd()
{
    const double current = currentPosition();
    if (current < 0) {
        return -1;
    }
    if (currentSegments.empty()) {
        return -1;
    }

    double lowestBegin = currentSegments[0].begin;
    for (const Segment &segment : currentSegments) {
        if (segment.end < current) {
            continue;
        }
        if (segment.begin < current) {
            return segment.end;
        }
    }
    //puts("Failed to find end of current segment");
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
    if (segmentEnd < 0) {
        return;
    }
    //printf("Current segment ends at: %f, position at %f\n", segmentEnd, currentPosition());

    if (!currentlyPlaying) {
        return;
    }
    if (segmentEnd > 0) {
        printf("\nSkipping sponsor...\n");
        s_currentPosition = -1.;
        nextSegmentStart = -1.;
        s_lastPositionFetched = -1;
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
    if (position < 0 || length < 0 || s_lastPositionFetched < 0) {
        //static const char spinner[] = { '-', '\\', '|', '/', '-', '\\', '|', '/', };
        //static uint8_t spinnerPos = 0;
        //spinnerPos = (spinnerPos + 1) % sizeof(spinner);
        //printf("\e[2K\r%c", spinner[spinnerPos]);
        //printf("\rInvalid position or length");
        //fflush(stdout);
        return;
    }
    //if (currentlyPlaying) {
    //    double timeDelta = time(nullptr) - lastPositionFetched;
    //    position = std::round(timeDelta + position);
    //}
    printf("\r[");
    int playedLength = position * PROGRESS_WIDTH / length;
    if (playedLength > PROGRESS_WIDTH) {
        playedLength = PROGRESS_WIDTH;
    }
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

    for (const Segment &segment : currentSegments) {
        int start = PROGRESS_WIDTH * segment.begin / length;
        if (start >= PROGRESS_WIDTH) {
            start = PROGRESS_WIDTH - 1;
        }
        // Seek to the start of the bar
        printf("\r\e[C");
        for (int i=0; i<start; i++) {
            printf("\e[C");
        }

        const int segmentLength = std::max<int>(PROGRESS_WIDTH * (segment.end - segment.begin) / length, 1);
        for (int i=0; i<segmentLength; i++) {
            printf("#");
        }
    }

    fflush(stdout);
}

bool handleMessage(Connection *connection, const std::string &inputBuffer)
{
    if (inputBuffer.empty()) {
        puts("Empty message");
        return false;
    }
    cast_channel::CastMessage message;
    if (!message.ParseFromString(inputBuffer)) {
        puts("PArsing failed");
        return false;
    }

    if (!message.has_payload_utf8()) {
        puts("No string payload");
        return true;
    } else if (message.has_payload_binary()) {
        puts("Got binary message, ignoring");
        return true;
    }
    const std::string payload = message.payload_utf8();
    std::string type = regexExtract(R"--("type"\s*:\s*"([^"]+)")--", payload);
    if (type != "PING") {
        //std::cout << message.source_id() << " > " << message.destination_id() << " (" << message.namespace_() << "): \n" << message.payload_utf8() << std::endl;
    }

    if (type == "CLOSE") {
        cc::sendSimple(*connection, cc::msg::Connect, cc::ns::Connection);
        return true;
    }
    if (type == "INVALID_REQUEST") {
        std::cout << message.source_id() << " > " << message.destination_id() << " (" << message.namespace_() << "): \n" << message.payload_utf8() << std::endl;
        return false;
    }

    if (type == "MEDIA_STATUS") {
        extractNumber(R"--("duration"\s*:\s*([0-9.]+))--", payload, &currentDuration);
        if (extractNumber(R"--("currentTime"\s*:\s*([0-9.]+))--", payload, &s_currentPosition)) {
            s_lastPositionFetched = time(nullptr);
        }
        std::string state = regexExtract(R"--("playerState"\s*:\s*"([A-Z]+)")--", payload);
        if (!state.empty()) {
            currentlyPlaying = state == "PLAYING";
        }
        std::string mediaSession = regexExtract(R"--("mediaSessionId"\s*:\s*([0-9]+))--", payload);
        if (!mediaSession.empty()) {
            //std::cout << "Got media session " << mediaSession << std::endl;
            cc::mediaSession = mediaSession;
        }

        // the ID is base64, but replaced / with - and + with _, and without padding
        std::string videoID = regexExtract(R"--("contentId"\s*:\s*"([A-Za-z0-9_-]+)")--", payload);
        //if (videoID.empty()) {
        //    std::cerr << "Failed to find video id" << std::endl;
        //    return true;
        //}
        if (!videoID.empty() && videoID != currentVideo) {
            currentSegments = downloadSegments(videoID);
            currentVideo = videoID;
            nextSegmentStart = -1;
        }

        if (!currentSegments.empty()) {
            double delta = secondsUntilNextSegment();
            std::cout << "time to next segment: " << delta << std::endl;
            if (delta >= 0) {
                nextSegmentStart = time(nullptr) + delta;
            }
            maybeSeek(connection);
        }
        return true;
    }

    if (message.namespace_() == cc::ns::strings[cc::ns::Heartbeat]) {
        if (type == "PING") {
            cc::sendSimple(*connection, cc::msg::Pong, cc::ns::Heartbeat);
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
        //double currentTime = time(nullptr);
        //if (nextSegmentStart > 0 && nextSegmentStart > currentTime) {
        //    std::cout << "Next segment at " << nextSegmentStart << ", current time " << currentTime << std::endl;
        //    timeout.tv_sec = nextSegmentStart - currentTime;
        //} else {
            timeout.tv_sec = 1;
        //}
        timeout.tv_usec = 0;
        const int events = select(connection.fd + 1, &fdset, 0, 0, &timeout);
        printProgress(currentPosition(), currentDuration);

        if (events < 0) {
            perror("select()");
            return errno;
        }
        if (errno == EINTR || events == 0) {
            continue;
        }

        // debugging
        if (errno) {
            perror("errno while selecting");
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
        if (msgLength > 64 * 1024) { // 64kb max according to the spec
            printf("Message length out of range: %d\n", msgLength);
            return EBADMSG;
        }

        std::string response = connection.read(msgLength);
        if (response.size() < msgLength) {
            std::cerr << "Short read, expected " << msgLength << " got " << response.size() << std::endl;
            return EBADMSG;
        }
        if (!handleMessage(&connection, response)) {
            puts("Failed to parse message");
            return ECONNRESET;
        }
        if (connection.eof) {
            puts("Disconnected");
            return ECONNRESET;
        }
        double currentTime = time(nullptr);
        if (nextSegmentStart > 0 && nextSegmentStart < currentTime) {
            // Check if we need to seek
            cc::sendSimple(connection, cc::msg::GetStatus, cc::ns::Media);
        }
    }
    return 0;
}

void signalHandler(int sig)
{
    signal(sig, SIG_DFL);
    s_running = false;
    printf("\e[?25h"); // re-enable cursor
    puts("Bye");
}

int main(int argc, char *argv[])
{
    signal(SIGINT, &signalHandler);
    signal(SIGTERM, &signalHandler);
    signal(SIGQUIT, &signalHandler);

    int ret = 0;
    while (s_running) {
        s_currentPosition = -1.;
        nextSegmentStart = -1.;
        s_lastPositionFetched = -1;
        currentVideo = "";
        cc::mediaSession = "";
        cc::dest = "";

        if (ret != 0) {
            puts("Disconnected, sleeping and re-connecting");
            sleep(10);
        }
        sockaddr_in address;
        if (!mdns::findChromecast(&address)) {
            return ENOENT;
        }

        // hide cursor
        printf("\e[?25l");
        ret = loop(address);
    }


    return ret;
}
