#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LILAC_DEBUG_HOST_TEST 1
#define LILAC_DEBUG_RUN_ID "0123456789abcdef0123456789abcdef"
#include "../../bootloader/debug-usb.c"

struct test_register { uint64_t address; uint32_t value; };
static struct test_register registers[128];
static unsigned register_count, writes;
static uint64_t fault_at;
static unsigned hardware_active[4], hardware_resource[4], start_count[4], stop_count[4], duplicate_starts;

static uint32_t *register_value(uint64_t address)
{
    for (unsigned index = 0; index < register_count; index++)
        if (registers[index].address == address) return &registers[index].value;
    assert(register_count < 128);
    registers[register_count].address = address;
    return &registers[register_count++].value;
}

int shim_debug_read32(uint64_t address, uint32_t *value)
{
    if (address == fault_at) return 0;
    *value = *register_value(address);
    return 1;
}

int shim_debug_write32(uint64_t address, uint32_t value)
{
    if (address == fault_at) return 0;
    writes++;
    if (address == USB_BASE + 0xc40c) *register_value(address) -= value;
    else if (address >= USB_BASE + 0xc800 && address < USB_BASE + 0xc840 && (address & 15) == 12) {
        unsigned endpoint = (address - USB_BASE - 0xc800) / 16;
        uint32_t result = value & ~BIT(10);
        if ((value & 15) == 6) {
            start_count[endpoint]++;
            if (hardware_active[endpoint]) { result |= 0x1000; duplicate_starts++; }
            else {
                hardware_active[endpoint] = 1;
                hardware_resource[endpoint] = endpoint + 1;
                result |= hardware_resource[endpoint] << 16;
            }
        } else if ((value & 15) == 8) {
            assert(hardware_active[endpoint]);
            assert(((value >> 16) & 127) == hardware_resource[endpoint]);
            hardware_active[endpoint] = 0; hardware_resource[endpoint] = 0;
            stop_count[endpoint]++;
        }
        *register_value(address) = result;
    }
    else if (address == USB_BASE + DCTL) *register_value(address) = value & ~BIT(30);
    else *register_value(address) = value;
    return 1;
}

void lilac_debug_display(const char *message, uint32_t value) { (void)message; (void)value; }
void lilac_debug_diagnostics(uint32_t stage, uint32_t address, uint32_t value,
    uint32_t event_count, uint32_t last_event)
{ (void)stage; (void)address; (void)value; (void)event_count; (void)last_event; }

static void reset_test(void)
{
    memset(registers, 0, sizeof(registers)); register_count = writes = 0; fault_at = 0;
    memset((void *)transfers, 0, sizeof(transfers)); memset((void *)events, 0, sizeof(events));
    memset(pending, 0, sizeof(pending)); memset(finished, 0, sizeof(finished));
    memset(resource, 0, sizeof(resource));
    memset(hardware_active, 0, sizeof(hardware_active));
    memset(hardware_resource, 0, sizeof(hardware_resource));
    memset(start_count, 0, sizeof(start_count)); memset(stop_count, 0, sizeof(stop_count));
    duplicate_starts = 0;
    failed = configured = active = transport_generation = control_phase = event_read = 0;
    packet_size = 512;
    session = (struct lilac_debug_session){0};
    *register_value(GCC_BASE + 0xf004) = BIT(31);
    *register_value(USB_BASE + 0xc120) = 0x5533290a;
    *register_value(0xc0121a0) = 1;
}

static void event(uint32_t value)
{
    if (!(value & 1) && ((value >> 6) & 15) == 1) {
        unsigned endpoint = (value >> 1) & 31;
        assert(endpoint < 4);
        hardware_active[endpoint] = 0; hardware_resource[endpoint] = 0;
        transfers[endpoint].control &= ~BIT(0);
    }
    events[event_read] = value;
    *register_value(USB_BASE + 0xc40c) = 4;
    poll_events();
}

static void request(uint8_t type, uint8_t code, uint16_t value, uint16_t length)
{
    memset(setup, 0, sizeof(setup));
    setup[0] = type; setup[1] = code; setup[2] = value; setup[3] = value >> 8;
    setup[6] = length; setup[7] = length >> 8;
    event(1u << 6);
}

int main(void)
{
    reset_test(); assert(initialize_usb());
    assert(pending[0] && !configured && *register_value(USB_BASE + 0xc720) == 3);
    for (unsigned reset = 0; reset < 100; reset++) event(0x101);
    assert(!failed && pending[0] && resource[0] == 1 && start_count[0] == 1 && !duplicate_starts);
    event(1 | (1u << 8)); event(1 | (2u << 8));
    request(0x80, 6, 0x100, 18);
    assert(!failed && control_phase == 1 && pending[1] && requested[1] == 18 && control_data[0] == 18);
    event((1u << 1) | (1u << 6));
    assert(control_phase == 2 && !pending[1]);
    event((3u << 6) | (2u << 12));
    assert(control_phase == 3 && pending[0] && requested[0] == 0);
    event(1u << 6); assert(control_phase == 0 && pending[0]);
    request(0, 9, 1, 0);
    assert(configured && pending[2] && requested[2] == 512);
    event((1u << 1) | (3u << 6) | (2u << 12));
    event((1u << 1) | (1u << 6));
    assert(control_phase == 0 && pending[0] && !failed);
    request(0x80, 6, 0x9900, 8); assert(!failed && pending[0] && control_phase == 0);
    session.uploaded = session.acknowledged = 1;
    event(1 | (1u << 8));
    assert(!configured && !pending[2] && !session.uploaded && !session.acknowledged);
    assert(stop_count[2] == 1 && !hardware_active[2] && !duplicate_starts);
    *register_value(USB_BASE + DSTS) = 1; event(1 | (2u << 8)); assert(packet_size == 64);
    event((1u << 6) | (1u << 12)); assert(failed);
    reset_test(); *register_value(USB_BASE + 0xc40c) = 4100; poll_events(); assert(failed);
    reset_test(); *register_value(USB_BASE + 0xc120) = 0; assert(!initialize_usb());
    reset_test(); fault_at = GCC_BASE + 0xf004; assert(!initialize_usb() && writes == 0);
    reset_test(); *register_value(0xc0121a0) = 0; assert(!initialize_usb());
    reset_test(); configured = 1;
    assert(!bulk_send((const uint8_t *)"test", 4) && failed);
    reset_test(); assert(initialize_usb());
    request(0x80, 6, 0x100, 18);
    event(0x101);
    assert(!failed && !pending[1] && pending[0] && stop_count[1] == 1 && !duplicate_starts);
    request(0x80, 6, 0x100, 18);
    event((1u << 1) | (1u << 6));
    event((3u << 6) | (2u << 12));
    assert(control_phase == 3 && pending[0]);
    event(0x101);
    assert(!failed && control_phase == 0 && pending[0] && stop_count[0] == 1 && !duplicate_starts);
    unsigned starts = start_count[0];
    event(1); event(0x101);
    assert(!failed && pending[0] && start_count[0] == starts);
    endpoint_command(0, 6, 0, 0, 0);
    assert(failed && duplicate_starts == 1 && (*register_value(USB_BASE + 0xc80c) & 0xffff) == 0x1006);
    puts("PASS: DWC3 resource ownership across repeated resets, data/status aborts, enumeration and faults");
}
