#pragma once

#include <string>

static bool s_running = true;
static bool s_verbose = false;
static bool s_adblock = false;
static bool s_youtube = false;

struct Segment {
    double begin = 0.;
    double end = 0.;
};

static std::vector<Segment> currentSegments;
static double nextSegmentStart = -1.;

static std::string currentVideo;
static double s_currentPosition = -1.;
static double currentDuration = -1.;
static double s_lastPositionFetched = -1;
static bool currentlyPlaying = false;
static time_t s_lastSeek = 0;
static time_t s_lastPing = 0;
static std::string s_currentStatus;
