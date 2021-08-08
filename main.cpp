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

static const std::array<uint8_t, 12> queryHeader = {
    0x00, 0x00, // ID
    0x00, 0x00, // Flags
    0x00, 0x01, // Query RRs
    0x00, 0x00, // Answer RRs
    0x00, 0x00, // Authority RRs
    0x00, 0x00  // Additional RRs
};
static const std::array<uint8_t, 4> queryFooter = {
    0x00, 0x0c, // QTYPE
    0x00, 0x01  // QCLASS
};

static const std::string queryName = "_googlecast._tcp.local.";
static const std::vector<std::string> queryNameComponents = {
    //pre-split for convenience
    "_googlecast", "_tcp", "local", ""
};

static bool s_running = true;

void signalHandler(int sig)
{
    signal(sig, SIG_DFL);
    s_running = false;
}

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
        perror("Failed to join multicast group");
        close(fd);
        return -1;
    }

    return fd;
}
bool sendData(const int fd, const std::vector<uint8_t> &data)
{
    sockaddr_in broadcastAddr = {0};
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
        perror("Failed to send request");
        return false;
    }

    return true;
}

bool sendRequest(const int fd)
{
    // Construct packet
    std::vector<uint8_t> data;
    data.insert(data.end(), queryHeader.begin(), queryHeader.end());

    for (const std::string &component : queryNameComponents) {
        data.push_back(uint8_t(component.size()));
        data.insert(data.end(), component.begin(), component.end());
    }

    data.insert(data.end(), queryFooter.begin(), queryFooter.end());

    return sendData(fd, data);
}

std::string parsePacket(const std::string &data)
{
    constexpr size_t minSize = queryHeader.size() + 4; // idk
    if (data.size() < minSize) {
        std::cerr << "Packet too small (" << data.size() << " bytes)" << std::endl;
        return "";
    }
    int pos = queryHeader.size();

    // No answers in packet
    if (data[6] == 0 && data[7] == 0) {
        return "";
    }

    std::string hostname;
    while (pos + 2 < data.size()) {
        const uint8_t length = data[pos];
        pos++;

        // Ends with a '.'
        if (length == 0) {
            return hostname;
        }

        const uint8_t nextPos = pos + length;
        if (nextPos >= data.size()) {
            std::cerr << "Invalid packet, next pos out of range (" << size_t(nextPos) << " max: " << data.size() << ")" << std::endl;
            return "";
        }
        hostname += data.substr(pos, length) + ".";

        pos = nextPos;
    }

    return hostname;
}

bool query(const int fd, sockaddr_in *address)
{
    // TODO: continously loop and update when new devices appear
    static constexpr std::chrono::seconds waitTime (10);

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    std::string packet(512, '\0');

    sockaddr_storage addressStorage;
    socklen_t addressSize = sizeof(addressStorage);

    do {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        timeval tv = {0};
        tv.tv_sec = waitTime.count();

        int st = select(fd+1, &fds, nullptr, nullptr, &tv);
        if (st < 0) {
            if (errno != EINTR) {
                perror("Error while waiting to read");
            }
            continue;
        }
        if (st == 0) {
            puts("Timeout waiting for chromecast, sending a new request");
            sendRequest(fd);
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
            perror("Failed to read packet");
            return false;
        }

        if (addressStorage.ss_family != AF_INET) {
            std::cerr << "Got wrong family, not IPv4 " << addressStorage.ss_family << std::endl;
            continue;
        }

        memcpy(address, &addressStorage, addressSize);

        packet.resize(size);

        std::string name = parsePacket(packet);
        if (name.empty()) {
            continue;
        }
        if (name != queryName) {
            std::cerr << "Got wrong name '" << name << "' from " << inet_ntoa(address->sin_addr) << std::endl;
            continue;
        }

        return true;
    } while (s_running);

    return false;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, &signalHandler);
    signal(SIGTERM, &signalHandler);
    signal(SIGQUIT, &signalHandler);

    const int fd = openSocket();
    if (fd < 0) {
        return 1;
    }
    if (!sendRequest(fd)) {
        return 2;
    }
    sockaddr_in address;
    if (!query(fd, &address)) {
        std::cout << "Failed to find a chromecast" << std::endl;
        return 3;
    }
    std::cout << "Found chromecast: " << inet_ntoa(address.sin_addr) << std::endl;

    return 0;
}
