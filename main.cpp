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

bool handleMessage(std::string *buffer)
{
    if (buffer->empty()) {
        return false;
    }
    uint8_t tag = 0;
    uint8_t wire = 0;
    uint32_t lengthOrValue = 0;
    size_t processedBytes = cc::decodeHeader(*buffer, &tag, &wire, &lengthOrValue);
    if (processedBytes == 0) {
        puts("Error while parsing header");
        return false;
    }
    printf("tag: %hhx, wire: %hhx, length or value: %u, processed: %lld\n", tag, wire, lengthOrValue, processedBytes);

    *buffer = buffer->substr(processedBytes);
    return true;
}

// TODO: automatically reconnect
int loop(const sockaddr_in &address)
{
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
    if (!cc::sendSimple(connection, cc::msg::Ping, cc::ns::Heartbeat)) {
        puts("Failed to send ping message");
        return errno;
    }
    puts("pinged");

    while (s_running) {
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
        std::string response = connection.read(1024 * 1024); // idk, 1MB should be enough
        if (response.empty()) {
            puts("No message received");
            return EINVAL;
        }
        try {
            while (handleMessage(&response) && !response.empty()) { }
        } catch (const std::exception &e) {
            puts(e.what());
            return EBADMSG;
        }
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
