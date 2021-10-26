extern "C" {
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
#include "ssl.h"


void signalHandler(int sig)
{
    signal(sig, SIG_DFL);
    s_running = false;
    puts("Bye");
}

int main(int argc, char *argv[])
{
    if (!ssl::initialize()) {
        return 1;
    }
    static std::unordered_map<std::string, std::string> categories = {
        { "--sponsor", "Paid promotion, paid referrals and direct advertisements." },
        { "--selfpromo", "Unpaid or self promotion. Includes sections about merchandise, donations, or information about who they collaborated with." },
        { "--interaction", "Reminders to like, subscribe or follow them in the middle of content." },
        { "--intro", "An interval without actual content. Pauses, static frames, repeating animations. Not transitions containing information." },
        { "--outro", "Credits or when the YouTube endcards appear. Not conclusions with information." },
        { "--preview", "Quick recap of previous episodes, or a preview of what's coming up later in the current video. Edited together clips, not spoken summaries." },
        { "--music_offtopic", "Only in music videos. Non-music sections of music videos that aren't already covered by another category." },
    };
    for (int i=1; i<argc; i++) {
        const std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            s_verbose = true;
        } else if (arg == "--all-categories") {
            for (const std::pair<std::string, std::string> &category : categories) {
                s_categories.insert(category.first.substr(2));
            }
        } else if (arg == "-a" || arg == "--adblock") {
            s_adblock = true;
        } else if (categories.count(arg)) {
            s_categories.insert(arg.substr(2));
        } else {
            printf("Usage: %s [-a|--adblock] [-v|--verbose] [--all-categories]\n", argv[0]);
            puts("You may also specify which categories you want to skip, defaults to just sponsors:");
            for (const std::pair<std::string, std::string> &category : categories) {
                printf("  %s: %s\n", category.first.c_str(), category.second.c_str());
            }
            puts("\n--adblock is basically untested and might not work, hence not on by default");
            exit(EINVAL);
        }
    }
    if (!s_categories.empty()) {
        printf("  Skipping these categories: ");
        for (const std::string &category : s_categories) {
            printf("%s ", category.c_str());
        }
        puts("");
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
        sockaddr_in address{};


        if (!mdns::findChromecast(&address)) {
            ret = ENOENT;
            break;
        }

        // hide cursor
        printf("\033[?25l");
        s_currentStatus = "Connecting...";
        ret = loop(address);
    }

    printf("\033[?25h"); // re-enable cursor
    tcsetattr(STDIN_FILENO, TCSANOW, &origTermios);

    return ret;
}
