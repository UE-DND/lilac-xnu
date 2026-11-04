#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../bootloader/debug-protocol.h"

static enum lilac_debug_action command(struct lilac_debug_session *session, const char *text, char *reply)
{
    return lilac_debug_command(session, (const uint8_t *)text, strlen(text), reply);
}

int main(void)
{
    const uint8_t log[] = "123456789";
    struct lilac_debug_session session = {.run_id = "0123456789abcdef0123456789abcdef", .log = log, .length = 9};
    char reply[64];
    session.crc = lilac_debug_crc(log, 9);
    assert(session.crc == 0xcbf43926);
    assert(command(&session, "reboot-bootloader", reply) == LILAC_DEBUG_REPLY && !strncmp(reply, "FAIL", 4));
    assert(command(&session, "oem log-ack:cbf43926", reply) == LILAC_DEBUG_REPLY && !session.acknowledged);
    assert(command(&session, "upload", reply) == LILAC_DEBUG_UPLOAD && !strcmp(reply, "DATA00000009"));
    assert(!session.uploaded);
    session.uploaded = 1;
    command(&session, "oem log-ack:cbf43927", reply); assert(!session.acknowledged);
    command(&session, "oem log-ack:cbf43926", reply); assert(session.acknowledged);
    assert(command(&session, "reboot-bootloader", reply) == LILAC_DEBUG_REBOOT);
    command(&session, "upload", reply); assert(!session.uploaded && !session.acknowledged);
    command(&session, "getvar:run-id", reply); assert(strlen(reply) == 36);
    command(&session, "getvar:log-crc32", reply); assert(!strcmp(reply, "OKAYcbf43926"));
    const char *forbidden[] = {"flash:boot", "erase:userdata", "download:00000001", "reboot", "oem unlock", "uploadx", "oem read"};
    for (unsigned index = 0; index < sizeof(forbidden)/sizeof(forbidden[0]); index++) {
        assert(command(&session, forbidden[index], reply) == LILAC_DEBUG_REPLY && !strncmp(reply, "FAIL", 4));
    }
    uint8_t oversized[65]; memset(oversized, 'x', sizeof(oversized));
    assert(lilac_debug_command(&session, oversized, sizeof(oversized), reply) == LILAC_DEBUG_REPLY);
    session.length = LILAC_DEBUG_LOG_LIMIT + 1;
    assert(command(&session, "upload", reply) == LILAC_DEBUG_REPLY && !strncmp(reply, "FAIL", 4));
    session.length = 0;
    assert(command(&session, "upload", reply) == LILAC_DEBUG_REPLY && !strncmp(reply, "FAIL", 4));
    puts("PASS: debug command whitelist, exact framing, CRC and save-before-reboot gate");
}
