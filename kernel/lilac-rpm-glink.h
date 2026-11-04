/* SPDX-License-Identifier: GPL-2.0-only
 * Intentless RPM GLINK version/channel handshake over lilac-rpm-fifo.h.
 * The caller owns and serializes both queues, imposes a timeout and provides
 * device barriers and doorbell access. This code does not vote for power.
 */
#ifndef LILAC_RPM_GLINK_H
#define LILAC_RPM_GLINK_H
#include "lilac-rpm-fifo.h"

#define LILAC_GLINK_VERSION 0u
#define LILAC_GLINK_VERSION_ACK 1u
#define LILAC_GLINK_OPEN 2u
#define LILAC_GLINK_OPEN_ACK 4u
#define LILAC_GLINK_DATA 9u
#define LILAC_GLINK_READ_NOTIFY 13u
#define LILAC_GLINK_LOCAL_CHANNEL 1u

struct lilac_rpm_glink {
    struct lilac_rpm_queue tx, rx;
    const struct lilac_rpm_io *io;
    uint32_t pending_id;
    uint16_t remote_channel;
    uint8_t sent_version, peer_version, version_ack, sent_open, open_ack, broken;
};
static inline void lilac_glink_header(uint8_t *p, uint16_t command, uint16_t param1, uint32_t param2)
{
    lilac_rpm_put32(p, (uint32_t)command | (uint32_t)param1 << 16);
    lilac_rpm_put32(p + 4, param2);
}
static inline int lilac_glink_ready(const struct lilac_rpm_glink *link)
{
    return link && !link->broken && !link->tx.poisoned && !link->rx.poisoned && link->sent_open &&
        link->open_ack && link->remote_channel;
}
/* Returns 1 for progress, 0 if the TX FIFO is full, -1 for fatal I/O. */
static inline int lilac_glink_say(struct lilac_rpm_glink *link, const uint8_t *packet, uint32_t n)
{
    if (lilac_rpm_send(&link->tx, link->io, packet, n)) return 1;
    if (link->tx.poisoned) { link->broken = 1; return -1; }
    return 0;
}
static inline int lilac_glink_step(struct lilac_rpm_glink *link)
{
    static const uint8_t name[] = "rpm_requests";
    uint8_t packet[40] = {0}, reply[24] = {0};
    uint32_t read, write, available, word, param2, length = 8;
    uint16_t command, param1;
    int sent, request_channel = 0;
    if (!link || !link->io || link->broken || link->tx.poisoned || link->rx.poisoned) return -1;
    if (!link->sent_version) {
        /* Offer no optional features: RPM transport supports version 1. */
        lilac_glink_header(reply, LILAC_GLINK_VERSION, 1, 0);
        sent = lilac_glink_say(link, reply, 8);
        if (sent > 0) link->sent_version = 1;
        return sent;
    }
    if (link->peer_version && link->version_ack && !link->sent_open) {
        lilac_glink_header(reply, LILAC_GLINK_OPEN, LILAC_GLINK_LOCAL_CHANNEL, sizeof(name));
        for (size_t i = 0; i < sizeof(name); i++) reply[8 + i] = name[i];
        sent = lilac_glink_say(link, reply, sizeof(reply));
        if (sent > 0) link->sent_open = 1;
        return sent;
    }
    if (!lilac_rpm_indices(&link->rx, link->io, &read, &write)) { link->broken = 1; return -1; }
    available = write >= read ? write - read : link->rx.fifo.size - read + write;
    if (available < 8) return 0;
    if (!lilac_rpm_peek(&link->rx, link->io, packet, 8)) { link->broken = 1; return -1; }
    word = lilac_rpm_get32(packet); command = (uint16_t)word; param1 = (uint16_t)(word >> 16);
    param2 = lilac_rpm_get32(packet + 4);
    if (command == LILAC_GLINK_DATA) {
        /* The RPM client must process data and ACKs after the handshake. */
        if (lilac_glink_ready(link)) return 0;
        link->broken = 1; return -1;
    }
    if (command == LILAC_GLINK_OPEN) {
        if (!param1 || param2 < 2 || param2 > 32) { link->broken = 1; return -1; }
        length = 8 + ((param2 + 7) & ~7u);
        if (available < length) return 0;
        if (!lilac_rpm_peek(&link->rx, link->io, packet, length)) { link->broken = 1; return -1; }
        if (packet[8 + param2 - 1]) { link->broken = 1; return -1; }
        for (uint32_t i = 0; i + 1 < param2; i++)
            if (packet[8 + i] < 32 || packet[8 + i] > 126) { link->broken = 1; return -1; }
        request_channel = param2 == sizeof(name);
        for (size_t i = 0; request_channel && i < sizeof(name); i++)
            if (packet[8 + i] != name[i]) request_channel = 0;
        if (request_channel) {
            if (link->remote_channel && link->remote_channel != param1) { link->broken = 1; return -1; }
            lilac_glink_header(reply, LILAC_GLINK_OPEN_ACK, param1, 0);
            sent = lilac_glink_say(link, reply, 8);
            if (sent <= 0) return sent;
        } else if (param1 == link->remote_channel) { link->broken = 1; return -1; }
        /* RPM also advertises services with no local client. Consume OPEN
         * without acknowledging it; only rpm_requests completes its opening
         * handshake. This matches GLINK's deferred endpoint creation. */
    } else if (command == LILAC_GLINK_VERSION) {
        if (param1 != 1) { link->broken = 1; return -1; }
        lilac_glink_header(reply, LILAC_GLINK_VERSION_ACK, 1, 0);
        sent = lilac_glink_say(link, reply, 8);
        if (sent <= 0) return sent;
    } else if (command == LILAC_GLINK_VERSION_ACK) {
        if (param1 != 1 || param2 != 0) { link->broken = 1; return -1; }
    } else if (command == LILAC_GLINK_OPEN_ACK) {
        if (!link->sent_open || param1 != LILAC_GLINK_LOCAL_CHANNEL) { link->broken = 1; return -1; }
    } else if (command != LILAC_GLINK_READ_NOTIFY) {
        link->broken = 1; return -1;
    }
    if (!lilac_rpm_consume(&link->rx, link->io, length)) { link->broken = 1; return -1; }
    if (command == LILAC_GLINK_VERSION) link->peer_version = 1;
    if (command == LILAC_GLINK_VERSION_ACK) link->version_ack = 1;
    if (command == LILAC_GLINK_OPEN && request_channel) link->remote_channel = param1;
    if (command == LILAC_GLINK_OPEN_ACK) link->open_ack = 1;
    return 1;
}

/* Submit one version-0 RPM request. The caller supplies the DT-derived set and
 * voltage level and must impose a timeout; a timeout does not prove whether
 * RPM applied the vote. A new request is blocked until a matching ACK. */
static inline int lilac_glink_rwmx_send(struct lilac_rpm_glink *link,
    uint32_t id, uint32_t set, uint32_t level)
{
    uint8_t packet[56] = {0};
    if (!lilac_glink_ready(link) || link->pending_id ||
        lilac_rpm_rwmx_request(packet + 16, sizeof(packet) - 16, 0, id, set, level) != 40) return -1;
    lilac_glink_header(packet, LILAC_GLINK_DATA, LILAC_GLINK_LOCAL_CHANNEL, 0);
    lilac_rpm_put32(packet + 8, 40);
    int sent = lilac_glink_say(link, packet, sizeof(packet));
    if (sent > 0) link->pending_id = id;
    return sent;
}
/* Returns 1 for an ACK, 2 for an RPM error, 0 when incomplete, -1 for a
 * malformed or unrelated response. Nothing consumes an unverified message. */
static inline int lilac_glink_rwmx_ack(struct lilac_rpm_glink *link)
{
    uint8_t packet[160];
    uint32_t r, w, available, header, bytes, remaining, total, payload;
    if (!lilac_glink_ready(link) || !link->pending_id) return -1;
    if (!lilac_rpm_indices(&link->rx, link->io, &r, &w)) { link->broken = 1; return -1; }
    available = w >= r ? w - r : link->rx.fifo.size - r + w;
    if (available < 16) return 0;
    if (!lilac_rpm_peek(&link->rx, link->io, packet, 16)) { link->broken = 1; return -1; }
    header = lilac_rpm_get32(packet);
    bytes = lilac_rpm_get32(packet + 8); remaining = lilac_rpm_get32(packet + 12);
    if ((uint16_t)header != LILAC_GLINK_DATA || (header >> 16) != link->remote_channel ||
        remaining || bytes < 20 || bytes > sizeof(packet) - 16) { link->broken = 1; return -1; }
    total = 16 + ((bytes + 7) & ~7u);
    if (available < total) return 0;
    if (!lilac_rpm_peek(&link->rx, link->io, packet, total)) { link->broken = 1; return -1; }
    payload = lilac_rpm_get32(packet + 20);
    if (lilac_rpm_get32(packet + 16) != 0x00716572u || payload != bytes - 8 ||
        lilac_rpm_get32(packet + 24) != 0x2367736du || lilac_rpm_get32(packet + 28) != 4 ||
        lilac_rpm_get32(packet + 32) != link->pending_id) { link->broken = 1; return -1; }
    int result = 1;
    for (uint32_t at = 20; at < bytes;) {
        if (bytes - at < 8) { link->broken = 1; return -1; }
        uint32_t key = lilac_rpm_get32(packet + 16 + at), size = lilac_rpm_get32(packet + 20 + at);
        if (size > bytes - at - 8 || (((uint64_t)size + 3u) & ~3ull) > bytes - at - 8 ||
            key != 0x00727265u) { link->broken = 1; return -1; }
        result = 2;
        at += 8 + ((size + 3) & ~3u);
    }
    if (!lilac_rpm_consume(&link->rx, link->io, total)) { link->broken = 1; return -1; }
    link->pending_id = 0;
    return result;
}
#endif
