#define PROGRESS_WIDTH 40
#define PING_INTERVAL 30

extern "C" {
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
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

#include "globals.h"

#include "connection.h"
#include "sponsor.h"
#include "util.h"
#include "mdns.h"
#include "chromecast.h"
#include "loop.h"


void signalHandler(int sig)
{
    signal(sig, SIG_DFL);
    s_running = false;
    puts("Bye");
}

int main(int argc, char *argv[])
{
    for (int i=1; i<argc; i++) {
        const std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            s_verbose = true;
        } else if (arg == "-a" || arg == "--adblock") {
            s_adblock = true;
        } else {
            printf("Usage: %s [-a|--adblock] [-v|--verbose]\n", argv[0]);
            puts("\t--adblock is basically untested and might not work, hence not on by default");
            exit(EINVAL);
        }
    }
    signal(SIGINT, &signalHandler);
    signal(SIGTERM, &signalHandler);
    signal(SIGQUIT, &signalHandler);

    termios origTermios;
    tcgetattr(STDIN_FILENO, &origTermios);

    termios newTermios = origTermios;
    newTermios.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);

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
        s_currentStatus = "Connecting...";
        ret = loop(address);
    }

    printf("\e[?25h"); // re-enable cursor
    tcsetattr(STDIN_FILENO, TCSANOW, &origTermios);

    return ret;
}
