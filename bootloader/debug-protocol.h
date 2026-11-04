#ifndef LILAC_DEBUG_PROTOCOL_H
#define LILAC_DEBUG_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define LILAC_DEBUG_LOG_LIMIT (2u * 1024u * 1024u)

struct lilac_debug_session {
    const char *run_id;
    const uint8_t *log;
    uint32_t length, crc;
    unsigned uploaded, acknowledged;
};

enum lilac_debug_action { LILAC_DEBUG_REPLY, LILAC_DEBUG_UPLOAD, LILAC_DEBUG_REBOOT };

static inline uint32_t lilac_debug_crc(const uint8_t *data, size_t length)
{
    uint32_t crc = ~0u;
    for (size_t offset = 0; offset < length; offset++) {
        crc ^= data[offset];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static inline void lilac_debug_hex(char *output, uint32_t value)
{
    for (unsigned digit = 0; digit < 8; digit++)
        output[digit] = "0123456789abcdef"[(value >> (28 - digit * 4)) & 15];
    output[8] = 0;
}

static inline int lilac_debug_equal(const uint8_t *data, size_t length, const char *text)
{
    size_t offset = 0;
    while (text[offset]) {
        if (offset >= length || data[offset] != (uint8_t)text[offset]) return 0;
        offset++;
    }
    return offset == length;
}

static inline void lilac_debug_reply(char output[64], const char *text)
{
    size_t offset = 0;
    while (offset < 63 && text[offset]) { output[offset] = text[offset]; offset++; }
    output[offset] = 0;
}

static inline enum lilac_debug_action lilac_debug_command(struct lilac_debug_session *session,
    const uint8_t *data, size_t length, char reply[64])
{
    lilac_debug_reply(reply, "FAILunsupported debug command");
    if (!length || length > 64 || session->length > LILAC_DEBUG_LOG_LIMIT) return LILAC_DEBUG_REPLY;
    if (lilac_debug_equal(data, length, "getvar:product"))
        lilac_debug_reply(reply, "OKAYlilac-ram-debug");
    else if (lilac_debug_equal(data, length, "getvar:version"))
        lilac_debug_reply(reply, "OKAY0.4");
    else if (lilac_debug_equal(data, length, "getvar:serialno"))
        lilac_debug_reply(reply, "OKAYlilac-debug");
    else if (lilac_debug_equal(data, length, "getvar:run-id")) {
        lilac_debug_reply(reply, "OKAY");
        unsigned offset = 0;
        while (offset < 32 && session->run_id[offset]) {
            reply[4 + offset] = session->run_id[offset]; offset++;
        }
        reply[4 + offset] = 0;
    } else if (lilac_debug_equal(data, length, "getvar:log-size")) {
        lilac_debug_reply(reply, "OKAY"); lilac_debug_hex(reply + 4, session->length);
    } else if (lilac_debug_equal(data, length, "getvar:log-crc32")) {
        lilac_debug_reply(reply, "OKAY"); lilac_debug_hex(reply + 4, session->crc);
    } else if (lilac_debug_equal(data, length, "upload")) {
        session->uploaded = session->acknowledged = 0;
        if (!session->length || !session->log) {
            lilac_debug_reply(reply, "FAILno captured log"); return LILAC_DEBUG_REPLY;
        }
        lilac_debug_reply(reply, "DATA"); lilac_debug_hex(reply + 4, session->length);
        return LILAC_DEBUG_UPLOAD;
    } else {
        char expected[32] = "oem log-ack:";
        lilac_debug_hex(expected + 12, session->crc);
        if (session->uploaded && lilac_debug_equal(data, length, expected)) {
            session->acknowledged = 1; lilac_debug_reply(reply, "OKAY");
        } else if (lilac_debug_equal(data, length, "reboot-bootloader")) {
            if (!session->acknowledged) lilac_debug_reply(reply, "FAILsave and acknowledge log first");
            else { lilac_debug_reply(reply, "OKAY"); return LILAC_DEBUG_REBOOT; }
        }
    }
    return LILAC_DEBUG_REPLY;
}
#endif
