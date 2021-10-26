#pragma once

#include "ec_protobuf.h"

// message CastMessage {
//   enum ProtocolVersion { CASTV2_1_0 = 0; }
//   required ProtocolVersion protocol_version = 1;
//   required string source_id = 2;
//   required string destination_id = 3;
//   required string namespace = 4;
//   enum PayloadType {
//     STRING = 0;
//     BINARY = 1;
//   }
//   required PayloadType payload_type = 5;
//   optional string payload_utf8 = 6;
//   optional bytes payload_binary = 7;
// }

class CastMessage : public ec::cls_protoc3<std::basic_string<uint8_t>>
{
    enum  {
        id_protocol_version = 1,
        id_source_id = 2,
        id_destination_id = 3,
        id_namespace = 4,
        id_payload_type = 5,
        id_payload_utf8 = 6,
        id_payload_binary = 7
    };

public:
    enum ProtocolVersion : uint32_t {
        CASTV2_1_0 = 0
    };
    enum PayloadType : uint32_t {
        STRING = 0,
        BINARY = 1
    };

    uint32_t _protocol_version = CASTV2_1_0;
    std::string _source_id;
    std::string _destination_id;
    std::string _namespace;
    uint32_t _payload_type = STRING;
    std::string _payload_utf8;
    std::basic_string<uint8_t> _payload_binary;

    void reset() override {
        _protocol_version = CASTV2_1_0;
        _source_id.clear();
        _destination_id.clear();
        _namespace.clear();
        _payload_type = STRING;
        _payload_utf8.clear();
        _payload_binary.clear();
    }
protected:
    size_t size_content() override {
        return
            size_varint(_protocol_version) +
            size_str(id_source_id, _source_id.c_str()) +
            size_str(id_destination_id, _destination_id.c_str()) +
            size_str(id_namespace, _namespace.c_str()) +
            size_varint(_payload_type) +
            size_str(id_payload_utf8, _payload_utf8.c_str()) +
            size_length_delimited(_payload_binary.size());
    }
    bool out_content(std::basic_string<uint8_t> *pout) override {
        return
            out_var(pout, id_protocol_version, _protocol_version) &&
            out_str(pout, id_source_id, _source_id.c_str()) &&
            out_str(pout, id_destination_id, _destination_id.c_str()) +
            out_str(pout, id_namespace, _namespace.c_str()) &&
            out_var(pout, id_payload_type, _payload_type) &&
            out_str(pout, id_payload_utf8, _payload_utf8.c_str()) &&
            out_cls(pout, id_payload_binary, _payload_binary.data(), _payload_binary.size());
    }
    bool on_cls(uint32_t field_number, const void* pdata, size_t sizedata) override {
        switch(field_number) {
        case id_source_id:
            _source_id.assign((const char*)pdata, sizedata);
            break;
        case id_destination_id:
            _destination_id.assign((const char*)pdata, sizedata);
            break;
        case id_namespace:
            _namespace.assign((const char*)pdata, sizedata);
            break;
        case id_payload_utf8:
            _payload_utf8.assign((const char*)pdata, sizedata);
            break;
        case id_payload_binary:
            _payload_binary.assign((const unsigned char*)pdata, sizedata);
            break;
        default:
            printf("Unknown cls field %u\n", field_number);
            return false;
        }
        return true;
    }
    bool on_var(uint32_t field_number, uint64_t val) override {
        switch (field_number) {
        case id_protocol_version:
            _protocol_version = val;
            break;
        case id_payload_type:
            _payload_type = val;
            break;
        default:
            printf("Unknown var field %u\n", field_number);
            return false;
        }
        return true;
    }
    bool on_fix32(uint32_t field_number, const void* pval) override {
        (void)pval;
        printf("Unknown fix32 %u\n", field_number);
        return false;
    }
    bool on_fix64(uint32_t field_number, const void* pval) override {
        (void)pval;
        printf("Unknown fix64 %u\n", field_number);
        return true;
    }
};
