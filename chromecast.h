#pragma once

#include "globals.h"
#include "connection.h"
#include "castchannel.h"
#include <fstream>

namespace cc
{
    static std::string dest;
    static std::string mediaSession;

bool sendMessage(const Connection &conn, const std::string &ns, const std::string &message)
{
    if (s_verbose) {
        std::cout << "Sending to '" << dest << "': '" << ns << ": '" << message << "'" << std::endl;
    }

    CastMessage msg;
    msg._payload_type = CastMessage::STRING;
    msg._protocol_version = CastMessage::CASTV2_1_0;
    msg._namespace = ns;
    msg._source_id = "sender-0";
    msg._destination_id = dest.empty() ? "receiver-0" : dest;
    msg._payload_utf8 = message;

    const uint32_t byteSize = htonl(uint32_t(msg.size()));
    std::basic_string<uint8_t> buffer(sizeof byteSize, '\0');
    memcpy(buffer.data(), &byteSize, sizeof byteSize);

    std::basic_string<uint8_t> contents;
    contents.reserve(1024);
    if (!msg.serialize(&buffer)) {
        puts("Failed to serialize");
        return false;
    }
    return conn.write(buffer);
}
namespace ns {
enum Namespace {
    Connection = 0,
    Receiver,
    Heartbeat,
    Media,
    NamespacesCount
};
static const char *strings[ns::NamespacesCount] = {
    "urn:x-cast:com.google.cast.tp.connection",
    "urn:x-cast:com.google.cast.receiver",
    "urn:x-cast:com.google.cast.tp.heartbeat",
    "urn:x-cast:com.google.cast.media"
};
}// namespace ns

namespace msg
{
enum Type {
    Connect = 0,
    Ping,
    Pong,
    GetStatus,
    MediaStatus,
    SimpleMessageCount
};

}//namespace msgs

static int s_requestId = 1;
bool sendSimple(const Connection &conn, const msg::Type type, const ns::Namespace urn)
{
    static const char *msgs[msg::SimpleMessageCount] = {
        "{\"type\": \"CONNECT\"}",
        "{\"type\": \"PING\"}",
        "{\"type\": \"PONG\"}",
        "{\"type\": \"GET_STATUS\", \"requestId\": 1}",
        "{\"type\": \"MEDIA_STATUS\"}"
    };

    if (urn >= ns::NamespacesCount) {
        return false;
    }
    if (type >= msg::SimpleMessageCount) {
        return false;
    }
    if (type == msg::GetStatus && mediaSession != "") {
        return sendMessage(conn,
                ns::strings[ns::Media],
                "{ "
                " \"type\": \"GET_STATUS\", "
                " \"requestId\": " + std::to_string(s_requestId++) + ", "
                " \"mediaSessionId\": \"" + mediaSession + "\""
                "}"
            );
    }

    const bool wasVerbose = s_verbose;
    if (type == msg::Ping || type == msg::Pong) {
        // too much spam
        s_verbose = false;
    }
    const bool ret = sendMessage(conn, ns::strings[urn], msgs[type]);
    s_verbose = wasVerbose;
    return ret;
}

bool seek(const Connection &conn, double position)
{
    if (mediaSession.empty()) {
        std::cerr << "Can't seek without media session" << std::endl;
        return false;
    }
    return sendMessage(conn,
            ns::strings[ns::Media],
            "{ "
            " \"type\": \"SEEK\", "
            " \"requestId\": " + std::to_string(s_requestId++) + ", "
            " \"mediaSessionId\": \"" + mediaSession + "\", "
            " \"currentTime\": " + std::to_string(position) +
            "}"
        );
}

bool sendSimpleMedia(const Connection &conn, const std::string &command)
{
    if (mediaSession.empty()) {
        std::cerr << "Can't seek without media session" << std::endl;
        return false;
    }
    return sendMessage(conn,
            ns::strings[ns::Media],
            "{ "
            " \"type\": \"" + command + "\", "
            " \"requestId\": " + std::to_string(s_requestId++) + ", "
            " \"mediaSessionId\": \"" + mediaSession + "\" "
            "}"
        );
}


bool loadMedia(const Connection &conn, const std::string &video, double position)
{
    if (video.empty()) {
        puts("Can't load empty video");
        return false;
    }
    return sendMessage(conn,
            ns::strings[ns::Media],
            "{ "
            " \"type\": \"LOAD\", "
            " \"requestId\": " + std::to_string(s_requestId++) + ", "
            " \"media\": {"
            "   \"contentId\": \"" + video + "\", "
            "   \"streamType\": \"BUFFERED\", "
            "   \"contentType\": \"x-youtube/video\" "
            " }, "
            " \"currentTime\": " + std::to_string(position) +
            "}"
        );
}

} //namespace cc

