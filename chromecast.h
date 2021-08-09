#pragma once

#include "connection.h"
#include "cast_channel.pb.h"

namespace cc
{

bool sendMessage(const Connection &conn, const std::string &dest, const std::string &ns, const std::string &message)
{
    cast_channel::CastMessage msg;
    msg.set_payload_type(msg.STRING);
    msg.set_protocol_version(msg.CASTV2_1_0);
    msg.set_namespace_(ns);
    msg.set_source_id("sender-0");
    msg.set_destination_id(dest);
    msg.set_payload_utf8(message);

    const uint32_t byteSize = htonl(uint32_t(msg.ByteSizeLong()));
    std::string buffer(sizeof byteSize, '\0');
    memcpy(buffer.data(), &byteSize, sizeof byteSize);

    std::string contents;
    msg.SerializeToString(&contents);

    return conn.write(buffer + contents);
}

} //namespace cc

