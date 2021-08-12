#pragma once

#include "connection.h"
#include "cast_channel.pb.h"

namespace cc
{

bool sendMessage(const Connection &conn, const std::string &ns, const std::string &message, const std::string &dest = "")
{
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
    GetStatus,
    SimpleMessageCount
};

}//namespace msgs

bool sendSimple(const Connection &conn, const msg::Type type, const ns::Namespace urn)
{
    static const char *msgs[msg::SimpleMessageCount] = {
        "{\"type\": \"CONNECT\"}",
        "{\"type\": \"PING\"}",
        "{\"type\": \"GET_STATUS\", \"requestId\": 1}"
    };

    if (urn >= ns::NamespacesCount) {
        return false;
    }
    if (type >= msg::SimpleMessageCount) {
        return false;
    }
    return sendMessage(conn, ns::strings[urn], msgs[type]);
}

bool seek(const Connection &conn, double position, const std::string sessionID)
{
    return sendMessage(conn,
            ns::strings[ns::Media],
            "{ "
            " \"type\": \"SEEK\", "
            " \"requestId\": 2, "
            " \"mediaSessionId\": " + sessionID + ", "
            " \"currentTime\": " + std::to_string(position) +
            "}"
        );
}

} //namespace cc

