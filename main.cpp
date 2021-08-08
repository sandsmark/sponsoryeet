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

#include "mdns.h"


void signalHandler(int sig)
{
    signal(sig, SIG_DFL);
    s_running = false;
}


int main(int argc, char *argv[])
{
    signal(SIGINT, &signalHandler);
    signal(SIGTERM, &signalHandler);
    signal(SIGQUIT, &signalHandler);

    const int fd = mdns::openSocket();
    if (fd < 0) {
        return 1;
    }
    if (!mdns::sendRequest(fd)) {
        return 2;
    }
    sockaddr_in address;
    const bool found = mdns::query(fd, &address);
    close(fd);

    if (!found) {
        std::cout << "Failed to find a chromecast" << std::endl;
        return 3;
    }
    std::cout << "Found chromecast: " << inet_ntoa(address.sin_addr) << std::endl;

    return 0;
}
