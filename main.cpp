static bool s_running = true;

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

static bool findChromecast(sockaddr_in *address)
{
    const int fd = mdns::openSocket();
    if (fd < 0) {
        return 1;
    }
    if (!mdns::sendRequest(fd)) {
        return 2;
    }

    const bool found = mdns::query(fd, address);
    close(fd);

    if (!found) {
        perror("Failed to find chromecast");
        return false;
    }
    address->sin_port = htons(8009);
    std::cout << "Found chromecast: " << inet_ntoa(address->sin_addr) << std::endl;
    return true;
}

std::string regexExtract(const std::string &regexstr, const std::string &payload)
{
    std::regex regex(regexstr);
    std::smatch match;
    if (!std::regex_search(payload, match, regex) || match.size() != 2) {
        std::cerr << "Failed to get match!" << std::endl;
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
static std::string currentVideo;
static std::vector<Segment> currentSegments;

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
        if (message.IsInitializedWithErrors()) {
            puts("has errors");
        }
        puts("PArsing failed");
        return false;
    }

    std::cout << message.source_id() << " > " << message.destination_id() << " (" << message.namespace_() << "): " << message.payload_utf8() << std::endl;
    if (!message.has_payload_utf8()) {
        puts("No string payload");
        return true;
    } else if (message.has_payload_binary()) {
        puts("Got binary message, ignoring");
        return true;
    }
    std::string type = regexExtract(R"--("type"\s*:\s*"([^"]+)")--", message.payload_utf8());
    std::cout << "Got type: " << type << std::endl;
    if (type == "CLOSE") {
        cc::sendSimple(*connection, cc::msg::Connect, cc::ns::Connection);
        return true;
    }
    if (type == "MEDIA_STATUS") {
        std::string videoID = regexExtract(R"--("contentId"\s*:\s*"([^"]+)")--", message.payload_utf8());
        std::string currentPosition = regexExtract(R"--("currentTime"\s*:\s*"([^"]+)")--", message.payload_utf8());
        if (videoID != currentVideo) {
            currentSegments = downloadSegments(videoID);
            currentVideo = videoID;
        }
    }

    if (message.namespace_() == cc::ns::strings[cc::ns::Heartbeat]) {
        if (type == "PING") {
            puts("Got ping, sending pong");
            cc::sendSimple(*connection, cc::msg::Pong, cc::ns::Heartbeat);
            //cc::sendSimple(*connection, cc::msg::Ping, cc::ns::Heartbeat);
        }
        return true;
    }
    if (message.namespace_() == cc::ns::strings[cc::ns::Receiver]) {
        if (type == "RECEIVER_STATUS") {
            const std::string displayName = regexExtract(R"--("displayName"\s*:\s*"([^"]+)")--", message.payload_utf8());
            const std::string sessionId = regexExtract(R"--("sessionId"\s*:\s*"([^"]+)")--", message.payload_utf8());
            std::cout << "got display name " << displayName << std::endl;
            std::cout << "session: " <<  sessionId << std::endl;
            cc::dest = sessionId;
            if (displayName == "YouTube") {
                puts("Correct");
                cc::sendSimple(*connection, cc::msg::GetStatus, cc::ns::Media);
            }
        }
    }

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
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        const int events = select(connection.fd + 1, &fdset, 0, 0, &timeout);

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
        printf("Message length: %d\n", msgLength);

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
        if (!findChromecast(&address)) {
            return ENOENT;
        }
        ret = loop(address);
    }


    return ret;
}
