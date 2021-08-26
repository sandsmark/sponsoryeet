#pragma once

#include "connection.h"
#include "cast_channel.pb.h"

namespace cc
{
    static std::string dest;

bool sendMessage(const Connection &conn, const std::string &ns, const std::string &message ) //, const std::string &dest = "")
{
    std::cout << "Sending '" << ns << ": '" << message << "'" << std::endl;
    cast_channel::CastMessage msg;
    msg.set_payload_type(msg.STRING);
    msg.set_protocol_version(msg.CASTV2_1_0);
    msg.set_namespace_(ns);
    msg.set_source_id("sender-0");
    msg.set_destination_id(dest.empty() ? "receiver-0" : dest);
    msg.set_payload_utf8(message);

    const uint32_t byteSize = htonl(uint32_t(msg.ByteSizeLong()));
    std::string buffer(sizeof byteSize, '\0');
    memcpy(buffer.data(), &byteSize, sizeof byteSize);

    std::string contents;
    msg.SerializeToString(&contents);

    return conn.write(buffer + contents);
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
    //if (type == msg::GetStatus) {
    //    return sendMessage(conn, ns::strings[urn], 
    //            "{\"type\": \"GET_STATUS\", \"requestId\": " + std::to_string(s_requestId++) + "}"
    //            );
    //}
    return sendMessage(conn, ns::strings[urn], msgs[type]);
}

bool seek(const Connection &conn, double position)
{
    return sendMessage(conn,
            ns::strings[ns::Media],
            "{ "
            " \"type\": \"SEEK\", "
            " \"requestId\": " + std::to_string(s_requestId++) + ", "
            " \"mediaSessionId\": " + dest + ", "
            " \"currentTime\": " + std::to_string(position) +
            "}"
        );
}

size_t decodeHeader(const std::string &buffer, uint8_t *tag, uint8_t *wire, uint32_t *lengthOrValue)
{
    size_t processedBytes = 0;
    if (buffer.empty()) {
        puts("Empty buffer received");
        return 0;
    }

    *wire = buffer[0] & 0b111;
    *tag = buffer[0] >> 3;
    processedBytes++;

    *lengthOrValue = 0;
    for (int decoded = 0; processedBytes < buffer.size(); decoded++) {
        const uint8_t current = buffer[processedBytes];
        processedBytes++;

        *lengthOrValue |= current & 0x7F << ((decoded) * 7);
        decoded++;

        if ((current & 0x80) == 0) {
            break;
        }
    }

    return processedBytes;
}

} //namespace cc

