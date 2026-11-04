#include "debug-usb.h"
#include "debug-protocol.h"
#include "debug-reboot.h"

#define USB_BASE 0x0a800000u
#define GCC_BASE 0x00100000u
#define DCFG 0xc700
#define DCTL 0xc704
#define DSTS 0xc70c
#define BIT(number) (1u << (number))

struct debug_trb { uint32_t low, high, size, control; };
static volatile struct debug_trb transfers[4] __attribute__((aligned(64)));
static volatile uint32_t events[1024] __attribute__((aligned(4096)));
static uint8_t setup[64] __attribute__((aligned(64)));
static uint8_t control_data[256] __attribute__((aligned(64)));
static uint8_t receive_data[512] __attribute__((aligned(64)));
static uint8_t transmit_data[16384] __attribute__((aligned(64)));
static struct lilac_debug_session session;
static uint32_t event_read, requested[4], pending[4], finished[4], resource[4];
static unsigned configured, control_phase, control_length, control_direction, packet_size = 512;
static unsigned active, failed, transport_generation;
static uint32_t last_register, last_value;
static uint32_t debug_stage, debug_events, debug_last_event;
uint64_t shim_debug_fault_esr, shim_debug_fault_far;

static void memory_barrier(void)
{
#ifndef LILAC_DEBUG_HOST_TEST
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}

static void delay_us(unsigned microseconds)
{
#ifdef LILAC_DEBUG_HOST_TEST
    (void)microseconds;
#else
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    uint64_t start, now, interval = frequency * microseconds / 1000000;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    do {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < interval);
#endif
}

static uint32_t read32(uint64_t address)
{
    uint32_t value = 0;
    if (!failed) {
        last_register = address;
        if (!shim_debug_read32(address, &value)) failed = 1;
        last_value = value;
    }
    return value;
}

static void write32(uint64_t address, uint32_t value)
{
    if (!failed) {
        last_register = address; last_value = value;
        if (!shim_debug_write32(address, value)) failed = 1;
    }
    memory_barrier();
}

static uint32_t usb_read(uint32_t offset) { return read32(USB_BASE + offset); }
static void usb_write(uint32_t offset, uint32_t value) { write32(USB_BASE + offset, value); }

static int wait_bits(uint64_t address, uint32_t mask, uint32_t wanted)
{
    for (unsigned attempt = 0; attempt < 10000 && !failed; attempt++) {
        if ((read32(address) & mask) == wanted) return !failed;
        delay_us(10);
    }
    failed = 1;
    return 0;
}

static void copy_bytes(uint8_t *target, const uint8_t *source, size_t length)
{
    for (size_t index = 0; index < length; index++) target[index] = source[index];
}

static unsigned text_length(const char *text)
{
    unsigned length = 0;
    while (text[length]) length++;
    return length;
}

static int endpoint_command(unsigned endpoint, uint32_t command, uint32_t param0,
    uint32_t param1, uint32_t param2)
{
    uint32_t offset = 0xc800 + endpoint * 16;
    usb_write(offset, param2); usb_write(offset + 4, param1); usb_write(offset + 8, param0);
    usb_write(offset + 12, command | BIT(10));
    if (!wait_bits(USB_BASE + offset + 12, BIT(10), 0)) return 0;
    uint32_t result = usb_read(offset + 12);
    if (result & 0xf000) { failed = 1; return 0; }
    if ((command & 15) == 6) resource[endpoint] = (result >> 16) & 0x7f;
    return 1;
}

static void configure_endpoint(unsigned endpoint, unsigned modify)
{
    uint32_t param0 = ((endpoint < 2 ? 64 : packet_size) << 3) |
        (endpoint < 2 ? 0 : 4) | ((endpoint & 1) ? (endpoint >> 1) << 17 : 0) |
        (modify ? 2u << 30 : 0);
    endpoint_command(endpoint, 1, param0, (endpoint << 25) | BIT(8) |
        (endpoint < 2 ? BIT(10) : 0), 0);
    if (!modify) endpoint_command(endpoint, 2, 1, 0, 0);
}

static void start_transfer(unsigned endpoint, void *buffer, uint32_t length, unsigned type)
{
    if (failed || pending[endpoint]) { failed = 1; return; }
    uint64_t address = (uintptr_t)buffer;
    transfers[endpoint].low = (uint32_t)address;
    transfers[endpoint].high = (uint32_t)(address >> 32);
    transfers[endpoint].size = length;
    transfers[endpoint].control = (type << 4) | BIT(11) | BIT(10) | BIT(1) | BIT(0);
    memory_barrier();
    requested[endpoint] = length; pending[endpoint] = 1; finished[endpoint] = 0;
    address = (uintptr_t)&transfers[endpoint];
    endpoint_command(endpoint, 6, (uint32_t)(address >> 32), (uint32_t)address, 0);
}

static void stop_transfer(unsigned endpoint)
{
    if (pending[endpoint]) {
        uint32_t command = 8 | BIT(8) | (resource[endpoint] << 16);
        if (endpoint >= 2) command |= BIT(11);
        if (!endpoint_command(endpoint, command, 0, 0, 0)) return;
        delay_us(1);
    }
    pending[endpoint] = finished[endpoint] = resource[endpoint] = 0;
}

static void receive_setup(void)
{
    control_phase = 0;
    start_transfer(0, setup, 8, 2);
}

static void stall_control(void)
{
    stop_transfer(0); stop_transfer(1);
    endpoint_command(0, 4, 0, 0, 0);
    if (!pending[0]) receive_setup();
}

static void reset_connection(void)
{
    transport_generation++;
    configured = 0; session.uploaded = session.acknowledged = 0;
    for (unsigned endpoint = 2; endpoint < 4; endpoint++) stop_transfer(endpoint);
    if (control_phase != 0 || !pending[0]) stall_control();
    usb_write(0xc720, 3); usb_write(DCFG, usb_read(DCFG) & ~0x3f8u);
}

static void control_request(void)
{
    unsigned request = setup[1], value = setup[2] | setup[3] << 8;
    unsigned index = setup[4] | setup[5] << 8, length = setup[6] | setup[7] << 8;
    unsigned response = 0;
    static const uint8_t device[] = {18,1,0,2,0,0,0,64,0xd1,0x18,0x0d,0xd0,0,1,1,2,3,1};
    static const uint8_t configuration[] = {9,2,32,0,1,1,0,0x80,250,
        9,4,0,0,2,0xff,0x42,3,0,7,5,0x01,2,0,2,0,7,5,0x81,2,0,2,0};
    static const uint8_t qualifier[] = {10,6,0,2,0,0,0,64,1,0};
    control_length = length; control_direction = setup[0] >> 7;
    if (setup[0] == 0x80 && request == 6) {
        unsigned type = value >> 8, descriptor = value & 255;
        if (type == 1 && !descriptor && !index) {
            response = sizeof(device); copy_bytes(control_data, device, response);
        } else if ((type == 2 || type == 7) && !descriptor && !index) {
            response = sizeof(configuration); copy_bytes(control_data, configuration, response);
            control_data[1] = type;
            unsigned size = type == 7 ? (packet_size == 512 ? 64 : 512) : packet_size;
            control_data[22] = control_data[29] = size & 255;
            control_data[23] = control_data[30] = size >> 8;
        } else if (type == 6 && !descriptor && !index) {
            response = sizeof(qualifier); copy_bytes(control_data, qualifier, response);
        } else if (type == 3 && descriptor <= 3) {
            if (!descriptor) { control_data[0]=4; control_data[1]=3; control_data[2]=9; control_data[3]=4; response=4; }
            else {
                const char *text = descriptor == 1 ? "Lilac" : descriptor == 2 ? "RAM debug" : "lilac-debug";
                response = 2 + 2 * text_length(text); control_data[0]=response; control_data[1]=3;
                for (unsigned offset=0; text[offset]; offset++) { control_data[2+offset*2]=text[offset]; control_data[3+offset*2]=0; }
            }
        } else { stall_control(); return; }
    } else if (setup[0] == 0 && request == 5 && value <= 127 && !length && !index) {
        usb_write(DCFG, (usb_read(DCFG) & ~0x3f8u) | (value << 3));
    } else if (setup[0] == 0 && request == 9 && value <= 1 && !length && !index) {
        for (unsigned endpoint = 2; endpoint < 4; endpoint++) stop_transfer(endpoint);
        configured = value; session.uploaded = session.acknowledged = 0;
        if (configured) {
            endpoint_command(0, 9 | (2u << 16), 0, 0, 0);
            configure_endpoint(2, 0); configure_endpoint(3, 0); usb_write(0xc720, 15);
            start_transfer(2, receive_data, packet_size, 1);
        } else usb_write(0xc720, 3);
    } else if (setup[0] == 0x80 && request == 8 && !value && !index && length == 1) {
        control_data[0] = configured; response = 1;
    } else if ((setup[0] & 0xfc) == 0x80 && request == 0 && !value && length == 2) {
        control_data[0] = control_data[1] = 0; response = 2;
    } else if (setup[0] == 2 && request == 1 && value == 0 && !length && (index == 1 || index == 0x81)) {
        endpoint_command(index == 1 ? 2 : 3, 5, 0, 0, 0);
    } else { stall_control(); return; }
    if (length) {
        if (!control_direction || !response) { stall_control(); return; }
        control_phase = 1;
        start_transfer(1, control_data, response < length ? response : length, 5);
    } else control_phase = 2;
}

static void poll_events(void)
{
    uint32_t count = usb_read(0xc40c) & 0xfffc;
    if (count > sizeof(events)) { failed = 1; return; }
    while (count >= 4 && !failed) {
        memory_barrier();
        uint32_t event = events[event_read++]; event_read %= 1024;
        debug_events++; debug_last_event = event;
        usb_write(0xc40c, 4); count -= 4;
        if (event & 1) {
            unsigned type = (event >> 8) & 15;
            if (type == 0 || type == 1) {
                reset_connection();
            } else if (type == 2) {
                unsigned speed = usb_read(DSTS) & 7;
                if (speed != 0 && speed != 1) { failed = 1; return; }
                packet_size = speed == 0 ? 512 : 64;
                configure_endpoint(0, 1); configure_endpoint(1, 1);
            } else if (type == 9 || type == 11) failed = 1;
            continue;
        }
        unsigned endpoint = (event >> 1) & 31, type = (event >> 6) & 15, status = (event >> 12) & 15;
        if (endpoint >= 4) continue;
        if (type == 1) {
            if (status & 1) { failed = 1; return; }
            if (!pending[endpoint] || (transfers[endpoint].control & BIT(0))) continue;
            pending[endpoint] = resource[endpoint] = 0; finished[endpoint] = 1;
            if (endpoint < 2) {
                if (control_phase == 0 && endpoint == 0) control_request();
                else if (control_phase == 1) control_phase = 2;
                else if (control_phase == 3) receive_setup();
            }
        } else if (type == 3 && endpoint < 2 && status == 2 && control_phase == 2) {
            unsigned direction = control_length ? !control_direction : 1;
            if (endpoint != direction) { stall_control(); continue; }
            control_phase = 3;
            start_transfer(endpoint, control_data, 0, control_length ? 4 : 3);
        }
    }
}

static int bulk_send(const uint8_t *data, uint32_t length)
{
    unsigned generation = transport_generation;
    while (length && !failed && configured) {
        uint32_t chunk = length > sizeof(transmit_data) ? sizeof(transmit_data) : length;
        copy_bytes(transmit_data, data, chunk);
        start_transfer(3, transmit_data, chunk, 1);
        for (unsigned attempt = 0; attempt < 100000 && pending[3] && !failed; attempt++) {
            poll_events(); delay_us(100);
            if (generation != transport_generation || !configured) return 0;
        }
        if (pending[3] || failed || (transfers[3].size & 0xffffff)) { failed = 1; return 0; }
        length -= chunk; data += chunk;
    }
    return !length && !failed && configured && generation == transport_generation;
}

static int reboot_read(uint64_t address, uint32_t *value)
{
    last_register = address;
    int result = shim_debug_read32(address, value);
    if (result) last_value = *value;
    return result;
}

static int reboot_write(uint64_t address, uint32_t value)
{
    last_register = address; last_value = value;
    int result = shim_debug_write32(address, value);
    memory_barrier();
    return result;
}

static void reboot_bootloader(void)
{
    const struct lilac_reboot_io io = {reboot_read, reboot_write, delay_us};
    debug_stage = 7;
    if (!lilac_prepare_fastboot(&io)) { failed = 1; return; }
    lilac_debug_display("Log saved. Rebooting to Fastboot", 0);
    usb_write(DCTL, usb_read(DCTL) & ~BIT(31));
    debug_stage = 8;
    write32(LILAC_PS_HOLD, 0);
    delay_us(1000000);
    failed = 1;
}

static int initialize_usb(void)
{
    debug_stage = 1;
    uint32_t power = read32(GCC_BASE + 0xf004);
    if (power & 1) write32(GCC_BASE + 0xf004, power & ~1u);
    if (!wait_bits(GCC_BASE + 0xf004, BIT(31), BIT(31))) return 0;
    const uint32_t branches[] = {0xf008, 0xf00c, 0xf010, 0x6a004, 0x82024, 0x5018};
    for (unsigned index = 0; index < sizeof(branches)/sizeof(branches[0]); index++) {
        debug_stage = 0x10 + index;
        uint64_t address = GCC_BASE + branches[index];
        write32(address, read32(address) | 1);
        if (!wait_bits(address, BIT(31), 0)) return 0;
    }
    debug_stage = 2;
    write32(GCC_BASE + 0xf000, read32(GCC_BASE + 0xf000) & ~1u);
    uint32_t phy_power = read32(0x0c012210);
    write32(0x0c0120a8, read32(0x0c0120a8) & ~(BIT(5) | BIT(6)));
    if (phy_power & 1) write32(0x0c012210, phy_power & ~1u);
    delay_us(200);
    if (!wait_bits(0x0c0121a0, 1, 1)) return 0;
    debug_stage = 3;
    uint32_t id = usb_read(0xc120);
    lilac_debug_display("DWC3 controller", id);
    if ((id & 0xffff0000u) != 0x55330000) { failed = 1; return 0; }
    usb_write(DCTL, usb_read(DCTL) & ~BIT(31));
    delay_us(20000);
    usb_write(DCTL, BIT(30));
    debug_stage = 4;
    if (!wait_bits(USB_BASE + DCTL, BIT(30), 0)) return 0;
    usb_write(0xc110, (usb_read(0xc110) & ~(3u << 12)) | (2u << 12) | 1);
    usb_write(0xc200, usb_read(0xc200) & ~(BIT(6) | BIT(8)));
    usb_write(0xf8810, usb_read(0xf8810) | BIT(20) | BIT(28));
    usb_write(0xf8830, usb_read(0xf8830) | BIT(24));
    usb_write(0xc400, (uint32_t)(uintptr_t)events); usb_write(0xc404, 0);
    usb_write(0xc408, sizeof(events)); usb_write(0xc40c, usb_read(0xc40c) & 0xfffc);
    event_read = 0;
    usb_write(DCFG, (usb_read(DCFG) & ~(0x3ffu | BIT(22))) | (16u << 17));
    usb_write(0xc708, 7 | BIT(9) | BIT(11));
    debug_stage = 5;
    endpoint_command(0, 9, 0, 0, 0);
    configure_endpoint(0, 0); configure_endpoint(1, 0);
    usb_write(0xc720, 3); receive_setup();
    usb_write(DCTL, usb_read(DCTL) | BIT(31));
    if (!failed) debug_stage = 6;
    return !failed;
}

void lilac_debug_usb(const void *log, uint32_t length)
{
    if (active || !log || !length || length > LILAC_DEBUG_LOG_LIMIT) return;
    active = 1;
    session = (struct lilac_debug_session){.run_id = LILAC_DEBUG_RUN_ID, .log = log,
        .length = length, .crc = lilac_debug_crc(log, length)};
    lilac_debug_display("Starting RAM-only USB log server", session.crc);
    if (initialize_usb()) {
        lilac_debug_display("USB: waiting for host log collection", length);
        unsigned screen_poll = 0;
        while (!failed) {
            poll_events();
            if (configured && finished[2]) {
                finished[2] = 0;
                uint32_t remaining = transfers[2].size & 0xffffff;
                if (remaining > requested[2]) { failed = 1; break; }
                char reply[64];
                enum lilac_debug_action action = lilac_debug_command(&session, receive_data,
                    requested[2] - remaining, reply);
                if (bulk_send((const uint8_t *)reply, text_length(reply))) {
                    if (action == LILAC_DEBUG_UPLOAD && bulk_send(session.log, session.length) &&
                        bulk_send((const uint8_t *)"OKAY", 4)) session.uploaded = 1;
                    else if (action == LILAC_DEBUG_REBOOT) { delay_us(100000); reboot_bootloader(); }
                }
                if (!failed && configured && !pending[2]) start_transfer(2, receive_data, packet_size, 1);
            }
            if (++screen_poll == 10000) {
                lilac_debug_diagnostics(debug_stage, last_register, last_value, debug_events, debug_last_event);
                screen_poll = 0;
            }
            delay_us(100);
        }
    }
    lilac_debug_diagnostics(debug_stage, last_register, last_value, debug_events, debug_last_event);
    lilac_debug_display("USB failed at register; log retained", last_register);
}
