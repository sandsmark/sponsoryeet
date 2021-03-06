#pragma once

#define WAIT_TIMEOUT

#include "globals.h"

#include <cstdint>
#include <cstring>
#include <array>
#include <string>
#include <vector>
#include <iostream>

extern "C" {
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
}

namespace mdns {
static constexpr std::array<uint8_t, 12> queryHeader = {
    0x00, 0x00, // ID
    0x00, 0x00, // Flags
    0x00, 0x01, // Query RRs
    0x00, 0x00, // Answer RRs
    0x00, 0x00, // Authority RRs
    0x00, 0x00  // Additional RRs
};
static constexpr std::array<uint8_t, 4> queryFooter = {
    0x00, 0x0c, // QTYPE
    0x00, 0x01  // QCLASS
};

static const std::string queryName = "_googlecast._tcp.local.";

int openSocket()
{
    static const uint32_t MdnsTTL = 255;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("Creating socket failed");
        return -1;
    }

    uint32_t val = 1;

    int st = setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
    if (st < 0) {
        perror("Setting SO_BROADCAST failed");
        close(fd);
        return -1;
    }
    st = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (st < 0) {
        perror("Setting SO_REUSEADDR failed");
        close(fd);
        return -1;
    }

    st = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &MdnsTTL, sizeof(MdnsTTL));
    if (st < 0) {
        perror("Setting ttl failed");
        close(fd);
        return -1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(5353);
    sin.sin_addr.s_addr = INADDR_ANY;
    st = bind(fd, (const struct sockaddr *) &sin, sizeof(sin));
    if (st < 0) {
        perror("Falied to bind");
        close(fd);
        return -1;
    }

    struct group_req mgroup;
    bzero(&mgroup, sizeof mgroup);
    inet_aton("224.0.0.251", &sin.sin_addr);
    memcpy(&mgroup.gr_group, &sin, sizeof sin);

    st = setsockopt(fd, IPPROTO_IP, MCAST_JOIN_GROUP, reinterpret_cast<const char*>(&mgroup), sizeof(mgroup));
    if (st < 0) {
        perror(" ! Failed to join multicast group");
        close(fd);
        return -1;
    }

    return fd;
}
bool sendData(const int fd, const std::vector<uint8_t> &data)
{
    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(5353);
    inet_aton("224.0.0.251", &broadcastAddr.sin_addr);

    long st = sendto(
            fd,
            data.data(),
            data.size(),
            0,
            reinterpret_cast<const sockaddr*>(&broadcastAddr),
            sizeof(broadcastAddr));

    // todo check length
    if (st < 0) {
        perror(" ! Failed to send request");
        return false;
    }

    return true;
}

bool sendRequest(const int fd)
{
    // Construct packet
    std::vector<uint8_t> data;
    data.insert(data.end(), queryHeader.begin(), queryHeader.end());

    for (const std::string &component : stringSplit(queryName, '.')) {
        data.push_back(uint8_t(component.size()));
        data.insert(data.end(), component.begin(), component.end());
    }

    data.insert(data.end(), queryFooter.begin(), queryFooter.end());
    if (s_verbose) puts(" > Sending request....");

    return sendData(fd, data);
}

std::string parsePacket(const std::string &data)
{
    constexpr size_t minSize = queryHeader.size() + 4; // idk
    if (data.size() < minSize) {
        if (s_verbose) std::cerr << "Packet too small (" << data.size() << " bytes)" << std::endl;
        return "";
    }

    const bool hasResponse = !(data[6] == 0 && data[7] == 0);
    if (!s_verbose && !hasResponse) { // if verbose mode is on, parse the name anyways
        return "";
    }

    std::string hostname;
    size_t pos = queryHeader.size();
    while (pos + 2 < data.size()) {
        const uint8_t length = data[pos];
        pos++;

        // Ends with a '.'
        if (length == 0) {
            break;
        }

        const uint8_t nextPos = pos + length;
        if (nextPos >= data.size()) {
            if (s_verbose) std::cerr << " ! Invalid packet, next pos out of range (" << size_t(nextPos) << " max: " << data.size() << ")" << std::endl;
            return "";
        }
        hostname += data.substr(pos, length) + ".";

        pos = nextPos;
    }

    if (s_verbose && pos + queryFooter.size() != data.size()) {
        printf("Failed to parse entire packet (%lu/%lu), hostname: %s\n", pos, data.size(), hostname.c_str());
    }
    // No answers in packet
    if (!hasResponse) {
        if (s_verbose) puts((" - Packet with no query response (probably another request) for " + hostname).c_str());
        return "";
    }

    return hostname;
}

bool query(const int fd, sockaddr_in *address)
{
    // TODO: continously loop and update when new devices appear

    std::string packet(512, '\0');

    sockaddr_storage addressStorage;
    socklen_t addressSize = sizeof(addressStorage);

    time_t endTime = time(nullptr) + 10;
    int pingTries = 0;

    do {
        if (time(nullptr) > endTime) {
            if (s_verbose) printf("\033[2K\r - Timeout waiting for mdns response, sending a new\n");
            sendRequest(fd);
            endTime = time(nullptr) + 10;
            pingTries++;
            continue;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        FD_SET(STDIN_FILENO, &fds);

        timeval tv = {0, 0};
        //tv.tv_usec = 100000; // 100ms, need dat nice spinner
        tv.tv_sec = 1;

        int st = select(fd+1, &fds, nullptr, nullptr, &tv);
        if (errno == EINTR) {
            continue;
        }
        if (st == 0) {
            static const char spinner[] = { '-', '\\', '|', '/', '-', '\\', '|', '/', };
            static uint8_t spinnerPos = 0;
            spinnerPos = (spinnerPos + 1) % sizeof(spinner);
            printf("%c Waiting for response", spinner[spinnerPos]);
            for (int i=0; i<pingTries % 10; i++) printf(".");
            fflush(stdout);
            printf("\033[2K\r"); // Erase the line, which won't be visible until the next flush
            continue;
        }
        if (st < 0) {
            if (errno != EINTR) {
                perror(" ! Error while waiting to read");
            }
            continue;
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            const int key = getchar();
            switch(key) {
            case 'q':
            case '\x1b':
                puts("Aborted");
                return false;
            default:
                if (s_verbose) printf("Unhandled key 0x%x\n", key);
                break;
            }
        }
        if (!FD_ISSET(fd, &fds)) {
            continue;
        }

        long size = recvfrom(
                fd,
                packet.data(),
                packet.size(),
                0,
                reinterpret_cast<sockaddr*>(&addressStorage),
                &addressSize
            );

        if (size < 0) {
            perror(" ! Failed to read packet");
            return false;
        }

        if (addressStorage.ss_family != AF_INET) {
            std::cerr << " ! Got wrong family, not IPv4 " << addressStorage.ss_family << std::endl;
            continue;
        }

        memcpy(address, &addressStorage, addressSize);
        if (s_verbose) {
            puts(" < Got response");
        }

        packet.resize(size);

        std::string name = parsePacket(packet);
        if (name.empty()) {
            continue;
        }
        if (name != queryName) {
            if (s_verbose) {
                std::cerr << " - Got wrong name '" << name << "' from " << inet_ntoa(address->sin_addr) << std::endl;
            }
            continue;
        }

        return true;
    } while (s_running);

    return false;
}

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
        if (errno) {
            perror("Failed to find chromecast");
        }
        return false;
    }
    address->sin_port = htons(8009);
    std::cout << "Found chromecast: " << inet_ntoa(address->sin_addr) << std::endl;
    return true;
}

} // namespace mdns
