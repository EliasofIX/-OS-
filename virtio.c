#include "os.h"

#define VIRTIO_BASE     0x0a000000UL
#define VIRTIO_SLOTS    32
#define VIRTIO_STRIDE   0x200UL

#define V_MAGIC               0x000
#define V_VERSION             0x004
#define V_DEVICE_ID           0x008
#define V_DEVICE_FEATURES     0x010
#define V_DEVICE_FEATURES_SEL 0x014
#define V_DRIVER_FEATURES     0x020
#define V_DRIVER_FEATURES_SEL 0x024
#define V_QUEUE_SEL           0x030
#define V_QUEUE_NUM_MAX       0x034
#define V_QUEUE_NUM           0x038
#define V_QUEUE_READY         0x044
#define V_QUEUE_NOTIFY        0x050
#define V_STATUS              0x070
#define V_QUEUE_DESC_LOW      0x080
#define V_QUEUE_DESC_HIGH     0x084
#define V_QUEUE_AVAIL_LOW     0x090
#define V_QUEUE_AVAIL_HIGH    0x094
#define V_QUEUE_USED_LOW      0x0a0
#define V_QUEUE_USED_HIGH     0x0a4
#define V_CONFIG              0x100

#define STATUS_ACKNOWLEDGE 1U
#define STATUS_DRIVER 2U
#define STATUS_DRIVER_OK 4U
#define STATUS_FEATURES_OK 8U
#define STATUS_FAILED 128U

#define DESC_NEXT 1U
#define DESC_WRITE 2U
#define DEVICE_BLOCK 2U
#define DEVICE_INPUT 18U

#define INPUT_QUEUE_SIZE 64
#define BLOCK_QUEUE_SIZE 8

#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define SYN_REPORT 0
#define ABS_X 0
#define ABS_Y 1
#define BTN_LEFT 0x110

struct virtq_desc {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} __attribute__((aligned(16)));

struct virtq_used_element {
    uint32_t id;
    uint32_t length;
};

struct input_available {
    uint16_t flags;
    volatile uint16_t index;
    uint16_t ring[INPUT_QUEUE_SIZE];
    uint16_t used_event;
};

struct input_used {
    uint16_t flags;
    volatile uint16_t index;
    struct virtq_used_element ring[INPUT_QUEUE_SIZE];
    uint16_t available_event;
};

struct input_packet {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

struct input_queue {
    struct virtq_desc descriptors[INPUT_QUEUE_SIZE];
    struct input_available available;
    uint8_t available_pad[2];
    struct input_used used;
    struct input_packet packets[INPUT_QUEUE_SIZE];
} __attribute__((aligned(16)));

enum input_kind { INPUT_KEYBOARD, INPUT_TABLET };

struct input_device {
    uintptr_t base;
    enum input_kind kind;
    uint16_t last_used;
    uint16_t queue_size;
    int pointer_x;
    int pointer_y;
    int move_pending;
    int abs_x_fresh;
    int abs_y_fresh;
    struct input_queue queue;
};

struct block_available {
    uint16_t flags;
    volatile uint16_t index;
    uint16_t ring[BLOCK_QUEUE_SIZE];
    uint16_t used_event;
};

struct block_used {
    uint16_t flags;
    volatile uint16_t index;
    struct virtq_used_element ring[BLOCK_QUEUE_SIZE];
    uint16_t available_event;
};

struct block_request {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct block_queue {
    struct virtq_desc descriptors[BLOCK_QUEUE_SIZE];
    struct block_available available;
    uint8_t available_pad[2];
    struct block_used used;
};

struct block_device {
    uintptr_t base;
    uint64_t capacity;
    uint16_t available_index;
    uint16_t used_index;
    int ready;
    int flush;
    struct block_queue queue;
    struct block_request request;
    uint8_t sector_buffer[512];
    volatile uint8_t request_status;
};

static struct input_device inputs[2];
static int input_count;
static struct block_device block;
static uintptr_t seen_bases[34];
static int seen_count;

extern int fdt_probe_virtio_mmio(const void *dtb,
                                 void (*callback)(uintptr_t base, void *context),
                                 void *context);

static int transport_seen(uintptr_t base) {
    for (int index = 0; index < seen_count; ++index) {
        if (seen_bases[index] == base) {
            return 1;
        }
    }
    return 0;
}

static void remember_transport(uintptr_t base) {
    if (seen_count >= (int)(sizeof(seen_bases) / sizeof(seen_bases[0]))) {
        return;
    }
    seen_bases[seen_count++] = base;
}

static inline uint32_t mmio_read(uintptr_t base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static inline void mmio_write(uintptr_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}

static void zero_memory(void *memory, size_t size) {
    uint8_t *bytes = memory;
    while (size--) {
        *bytes++ = 0;
    }
}

static void copy_memory(void *destination, const void *source, size_t size) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    while (size--) {
        *out++ = *in++;
    }
}

static int memory_is_zero(const void *memory, size_t size) {
    const uint8_t *bytes = memory;
    while (size--) {
        if (*bytes++) {
            return 0;
        }
    }
    return 1;
}

static void address_registers(uintptr_t base, uint32_t low_offset,
                              const void *address) {
    uint64_t physical = (uint64_t)(uintptr_t)address;
    mmio_write(base, low_offset, (uint32_t)physical);
    mmio_write(base, low_offset + 4, (uint32_t)(physical >> 32));
}

static void fail_device(uintptr_t base) {
    mmio_write(base, V_STATUS, mmio_read(base, V_STATUS) | STATUS_FAILED);
}

static int virtio_reset(uintptr_t base) {
    mmio_write(base, V_STATUS, 0);
    for (uint32_t spin = 0; spin < 1000000U && mmio_read(base, V_STATUS) != 0;
         ++spin) {}
    if (mmio_read(base, V_STATUS) != 0) {
        return 0;
    }
    mmio_write(base, V_STATUS, STATUS_ACKNOWLEDGE);
    mmio_write(base, V_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);
    return 1;
}

static int virtio_negotiate(uintptr_t base, uint32_t driver_low,
                            uint32_t driver_high) {
    mmio_write(base, V_DEVICE_FEATURES_SEL, 1);
    if (!(mmio_read(base, V_DEVICE_FEATURES) & 1U)) {
        return 0;
    }
    mmio_write(base, V_DRIVER_FEATURES_SEL, 0);
    mmio_write(base, V_DRIVER_FEATURES, driver_low);
    mmio_write(base, V_DRIVER_FEATURES_SEL, 1);
    mmio_write(base, V_DRIVER_FEATURES, driver_high);
    mmio_write(base, V_STATUS,
               STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    return (mmio_read(base, V_STATUS) & STATUS_FEATURES_OK) != 0;
}

static uint32_t virtio_feature_low(uintptr_t base) {
    mmio_write(base, V_DEVICE_FEATURES_SEL, 0);
    return mmio_read(base, V_DEVICE_FEATURES);
}

static int probe_transport(uintptr_t base) {
    return mmio_read(base, V_MAGIC) == 0x74726976U &&
           mmio_read(base, V_VERSION) == 2 &&
           mmio_read(base, V_DEVICE_ID) != 0;
}

static int input_has_absolute_axes(uintptr_t base) {
    volatile uint8_t *config = (volatile uint8_t *)(base + V_CONFIG);
    config[0] = 0x11;
    config[1] = EV_ABS;
    __asm__ volatile("dmb osh" ::: "memory");
    uint8_t size = config[2];
    return size > 0 && (config[8] & 0x03U) == 0x03U;
}

static int init_input(uintptr_t base) {
    if (input_count >= 2 || !virtio_reset(base) ||
        !virtio_negotiate(base, 0, 1)) {
        return 0;
    }

    struct input_device *device = &inputs[input_count];
    zero_memory(device, sizeof(*device));
    device->base = base;
    device->kind = input_has_absolute_axes(base) ? INPUT_TABLET : INPUT_KEYBOARD;
    device->pointer_x = SCREEN_WIDTH / 2;
    device->pointer_y = SCREEN_HEIGHT / 2;

    mmio_write(base, V_QUEUE_SEL, 0);
    uint32_t maximum = mmio_read(base, V_QUEUE_NUM_MAX);
    if (maximum == 0) {
        fail_device(base);
        return 0;
    }
    device->queue_size =
        maximum < INPUT_QUEUE_SIZE ? (uint16_t)maximum : INPUT_QUEUE_SIZE;
    mmio_write(base, V_QUEUE_NUM, device->queue_size);
    address_registers(base, V_QUEUE_DESC_LOW, device->queue.descriptors);
    address_registers(base, V_QUEUE_AVAIL_LOW, &device->queue.available);
    address_registers(base, V_QUEUE_USED_LOW, &device->queue.used);
    device->queue.available.flags = 1;
    for (uint16_t i = 0; i < device->queue_size; ++i) {
        device->queue.descriptors[i].address =
            (uint64_t)(uintptr_t)&device->queue.packets[i];
        device->queue.descriptors[i].length = sizeof(struct input_packet);
        device->queue.descriptors[i].flags = DESC_WRITE;
        device->queue.available.ring[i] = i;
    }
    __asm__ volatile("dmb oshst" ::: "memory");
    device->queue.available.index = device->queue_size;
    mmio_write(base, V_QUEUE_READY, 1);
    mmio_write(base, V_STATUS,
               STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK |
                   STATUS_DRIVER_OK);
    mmio_write(base, V_QUEUE_NOTIFY, 0);
    ++input_count;
    uart_puts(device->kind == INPUT_TABLET ? "virtio: tablet ready\n"
                                           : "virtio: keyboard ready\n");
    return 1;
}

static void block_reset_state(void) {
    zero_memory(&block, sizeof(block));
}

static int init_block(uintptr_t base) {
    if (!virtio_reset(base)) {
        return 0;
    }

    uint32_t offered_low = virtio_feature_low(base);
    if (offered_low & (1U << 5)) {
        return 0;
    }
    uint32_t accepted_low = offered_low & (1U << 9);
    if (!virtio_negotiate(base, accepted_low, 1)) {
        return 0;
    }

    block_reset_state();
    block.base = base;
    block.flush = (accepted_low & (1U << 9)) != 0;

    mmio_write(base, V_QUEUE_SEL, 0);
    if (mmio_read(base, V_QUEUE_NUM_MAX) < BLOCK_QUEUE_SIZE) {
        fail_device(base);
        return 0;
    }
    mmio_write(base, V_QUEUE_NUM, BLOCK_QUEUE_SIZE);
    address_registers(base, V_QUEUE_DESC_LOW, block.queue.descriptors);
    address_registers(base, V_QUEUE_AVAIL_LOW, &block.queue.available);
    address_registers(base, V_QUEUE_USED_LOW, &block.queue.used);
    block.queue.available.flags = 1;
    mmio_write(base, V_QUEUE_READY, 1);

    uint32_t low = mmio_read(base, V_CONFIG);
    uint32_t high = mmio_read(base, V_CONFIG + 4);
    block.capacity = ((uint64_t)high << 32) | low;
    mmio_write(base, V_STATUS,
               STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK |
                   STATUS_DRIVER_OK);
    block.ready = block.capacity >= 16;
    if (block.ready) {
        uart_puts("virtio: persistent document disk ready\n");
    }
    return block.ready;
}

static void try_init_transport(uintptr_t base, void *context) {
    (void)context;
    if (transport_seen(base) || !probe_transport(base)) {
        return;
    }
    remember_transport(base);
    uint32_t id = mmio_read(base, V_DEVICE_ID);
    if (id == DEVICE_INPUT) {
        (void)init_input(base);
    } else if (id == DEVICE_BLOCK && !block.ready) {
        (void)init_block(base);
    }
}

static void scan_fixed_virtio(void) {
    for (int slot = 0; slot < VIRTIO_SLOTS; ++slot) {
        try_init_transport(VIRTIO_BASE + (uintptr_t)slot * VIRTIO_STRIDE, 0);
    }
}

void virtio_init(const void *dtb) {
    input_count = 0;
    seen_count = 0;
    block_reset_state();
    if (dtb) {
        (void)fdt_probe_virtio_mmio(dtb, try_init_transport, 0);
    }
    if (input_count == 0 || !block.ready) {
        scan_fixed_virtio();
    }
}

static void recycle_input_buffer(struct input_device *device, uint16_t id) {
    uint16_t available = device->queue.available.index;
    device->queue.available.ring[available % device->queue_size] = id;
    __asm__ volatile("dmb oshst" ::: "memory");
    device->queue.available.index = (uint16_t)(available + 1);
    mmio_write(device->base, V_QUEUE_NOTIFY, 0);
}

static int emit_tablet_move(struct input_device *device, struct dc_event *event) {
    if (!device->move_pending) {
        return 0;
    }
    device->move_pending = 0;
    event->kind = DC_EVENT_POINTER_MOVE;
    event->x = device->pointer_x;
    event->y = device->pointer_y;
    return 1;
}

static int handle_input_packet(struct input_device *device,
                               const struct input_packet *packet,
                               struct dc_event *event) {
    if (device->kind == INPUT_TABLET && packet->type == EV_ABS) {
        if (packet->code == ABS_X) {
            device->pointer_x = (int)((uint64_t)packet->value * (SCREEN_WIDTH - 1) /
                                      32767U);
            device->abs_x_fresh = 1;
            device->move_pending = 1;
        } else if (packet->code == ABS_Y) {
            device->pointer_y =
                (int)((uint64_t)packet->value * (SCREEN_HEIGHT - 1) / 32767U);
            device->abs_y_fresh = 1;
            device->move_pending = 1;
        }
        /* Emit only once both axes are fresh (QMP often omits SYN). */
        if (device->abs_x_fresh && device->abs_y_fresh) {
            device->abs_x_fresh = 0;
            device->abs_y_fresh = 0;
            return emit_tablet_move(device, event);
        }
        return 0;
    }
    if (device->kind == INPUT_TABLET && packet->type == EV_SYN &&
        packet->code == SYN_REPORT) {
        device->abs_x_fresh = 0;
        device->abs_y_fresh = 0;
        return emit_tablet_move(device, event);
    }
    if (device->kind == INPUT_TABLET && packet->type == EV_KEY &&
        packet->code == BTN_LEFT) {
        event->kind = DC_EVENT_POINTER_BUTTON;
        event->x = device->pointer_x;
        event->y = device->pointer_y;
        event->value = (int32_t)packet->value;
        return 1;
    }
    if (device->kind == INPUT_KEYBOARD && packet->type == EV_KEY) {
        event->kind = DC_EVENT_KEY;
        event->code = packet->code;
        event->value = (int32_t)packet->value;
        return 1;
    }
    return 0;
}

int input_poll(struct dc_event *event) {
    for (int index = 0; index < input_count; ++index) {
        struct input_device *device = &inputs[index];
        if (device->last_used == device->queue.used.index) {
            continue;
        }
        __asm__ volatile("dmb oshld" ::: "memory");
        struct virtq_used_element used =
            device->queue.used.ring[device->last_used % device->queue_size];
        ++device->last_used;
        if (used.id >= device->queue_size) {
            continue;
        }

        struct input_packet packet = device->queue.packets[used.id];
        recycle_input_buffer(device, (uint16_t)used.id);

        event->kind = DC_EVENT_NONE;
        if (handle_input_packet(device, &packet, event)) {
            return 1;
        }
    }
    return 0;
}

static void block_disable(void) {
    if (block.base) {
        fail_device(block.base);
    }
    block.ready = 0;
}

static int block_consume_used(void) {
    __asm__ volatile("dmb oshld" ::: "memory");
    if (block.queue.used.index == block.used_index) {
        return 0;
    }
    struct virtq_used_element element =
        block.queue.used.ring[block.used_index % BLOCK_QUEUE_SIZE];
    if (element.id != 0) {
        block_disable();
        return 0;
    }
    ++block.used_index;
    return 1;
}

static int block_wait_used(void) {
    uint32_t timeout = 10000000U;
    while (block.queue.used.index == block.used_index && timeout--) {
        __asm__ volatile("wfe" ::: "memory");
    }
    if (!block_consume_used()) {
        block_disable();
        return 0;
    }
    return 1;
}

static int block_request(uint32_t type, uint64_t sector, void *buffer) {
    if (!block.ready || sector >= block.capacity) {
        return 0;
    }

    block.request.type = type;
    block.request.reserved = 0;
    block.request.sector = sector;
    block.request_status = 0xff;

    block.queue.descriptors[0] = (struct virtq_desc){
        (uint64_t)(uintptr_t)&block.request, sizeof(block.request), DESC_NEXT, 1};
    if (type == 4) {
        block.queue.descriptors[1] = (struct virtq_desc){
            (uint64_t)(uintptr_t)&block.request_status, 1, DESC_WRITE, 0};
    } else {
        if (type == 1) {
            copy_memory(block.sector_buffer, buffer, 512);
        }
        block.queue.descriptors[1] = (struct virtq_desc){
            (uint64_t)(uintptr_t)block.sector_buffer,
            512,
            (uint16_t)(DESC_NEXT | (type == 0 ? DESC_WRITE : 0)),
            2};
        block.queue.descriptors[2] = (struct virtq_desc){
            (uint64_t)(uintptr_t)&block.request_status, 1, DESC_WRITE, 0};
    }

    block.queue.available.ring[block.available_index % BLOCK_QUEUE_SIZE] = 0;
    __asm__ volatile("dmb oshst" ::: "memory");
    block.queue.available.index = ++block.available_index;
    __asm__ volatile("dmb oshst" ::: "memory");
    mmio_write(block.base, V_QUEUE_NOTIFY, 0);

    if (!block_wait_used()) {
        return 0;
    }
    if (block.request_status != 0) {
        block_disable();
        return 0;
    }
    if (type == 0) {
        copy_memory(buffer, block.sector_buffer, 512);
    }
    return 1;
}

static int read_sector(uint64_t sector, void *buffer) {
    return block_request(0, sector, buffer);
}

static int write_sector(uint64_t sector, const void *buffer) {
    return block_request(1, sector, (void *)buffer);
}

static void flush_disk(void) {
    if (block.flush) {
        (void)block_request(4, 0, 0);
    }
}

#define DOCUMENT_MAGIC_V1 0x4443415649415231ULL
#define DOCUMENT_MAGIC_V2 0x4443415649415232ULL
#define SLOT_SECTORS_V1 5
#define SLOT_SECTORS SCRIPT_SLOT_SECTORS

struct document_commit {
    uint64_t magic;
    uint64_t generation;
    uint64_t generation_inverse;
    uint32_t length;
    uint32_t length_inverse;
    uint32_t checksum;
    uint32_t metadata_checksum;
    uint32_t embed_valid;
    uint32_t embed_width;
    uint32_t embed_height;
    uint32_t embed_checksum;
    uint8_t reserved[456];
};

static uint32_t checksum(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t value = 2166136261U;
    while (length--) {
        value ^= *bytes++;
        value *= 16777619U;
    }
    return value;
}

static int valid_commit_v1(const struct document_commit *commit) {
    if (commit->magic != DOCUMENT_MAGIC_V1 ||
        commit->generation_inverse != ~commit->generation ||
        commit->length_inverse != ~commit->length ||
        commit->length > DOCUMENT_CAPACITY) {
        return 0;
    }
    return commit->metadata_checksum ==
           checksum(commit, offsetof(struct document_commit, metadata_checksum));
}

static int valid_commit_v2(const struct document_commit *commit) {
    if (commit->magic != DOCUMENT_MAGIC_V2 ||
        commit->generation_inverse != ~commit->generation ||
        commit->length_inverse != ~commit->length ||
        commit->length > DOCUMENT_CAPACITY ||
        commit->embed_width > PAINT_WIDTH ||
        commit->embed_height > PAINT_HEIGHT) {
        return 0;
    }
    return commit->metadata_checksum ==
           checksum(commit, offsetof(struct document_commit, metadata_checksum));
}

static uint8_t pack_attr(uint8_t font, uint8_t style, uint8_t size) {
    return (uint8_t)((font & 3U) | ((style & 3U) << 2) | ((size & 3U) << 4));
}

static void unpack_attr(uint8_t packed, uint8_t *font, uint8_t *style,
                        uint8_t *size) {
    *font = packed & 3U;
    *style = (packed >> 2) & 3U;
    *size = (packed >> 4) & 3U;
    if (*size == 0) {
        *size = 1;
    }
}

static int read_slot_v1(int slot, struct document_commit *commit, char *text) {
    uint64_t first = (uint64_t)slot * SLOT_SECTORS_V1;
    if (!read_sector(first, commit)) {
        return 0;
    }
    if (memory_is_zero(commit, sizeof(*commit))) {
        return 0;
    }
    if (!valid_commit_v1(commit)) {
        return -1;
    }
    for (int sector = 0; sector < SCRIPT_TEXT_SECTORS; ++sector) {
        if (!read_sector(first + 1 + sector, text + sector * 512)) {
            return -1;
        }
    }
    return checksum(text, commit->length) == commit->checksum ? 1 : -1;
}

static int read_slot_v2(int slot, struct document_commit *commit, char *text,
                        uint8_t *attrs, uint8_t *embed_image) {
    uint64_t first = (uint64_t)slot * SLOT_SECTORS;
    if (!read_sector(first, commit)) {
        return 0;
    }
    if (memory_is_zero(commit, sizeof(*commit))) {
        return 0;
    }
    if (!valid_commit_v2(commit)) {
        return -1;
    }
    for (int sector = 0; sector < SCRIPT_TEXT_SECTORS; ++sector) {
        if (!read_sector(first + 1 + sector, text + sector * 512)) {
            return -1;
        }
    }
    for (int sector = 0; sector < SCRIPT_ATTR_SECTORS; ++sector) {
        if (!read_sector(first + 1 + SCRIPT_TEXT_SECTORS + sector,
                         attrs + sector * 512)) {
            return -1;
        }
    }
    static uint8_t embed_meta[512];
    if (!read_sector(first + 1 + SCRIPT_TEXT_SECTORS + SCRIPT_ATTR_SECTORS,
                     embed_meta)) {
        return -1;
    }
    for (int sector = 0; sector < EMBED_IMAGE_SECTORS; ++sector) {
        if (!read_sector(first + 1 + SCRIPT_TEXT_SECTORS + SCRIPT_ATTR_SECTORS +
                             1 + sector,
                         embed_image + sector * 512)) {
            return -1;
        }
    }
    uint32_t rich = checksum(text, commit->length) ^
                    checksum(attrs, commit->length);
    if (commit->embed_valid) {
        rich ^= checksum(embed_image,
                         (size_t)commit->embed_width * commit->embed_height);
    }
    return rich == commit->checksum ? 1 : -1;
}

int storage_available(void) { return block.ready; }

enum storage_load_result storage_load_document(char *text, size_t capacity,
                                               size_t *length) {
    struct script_store doc;
    enum storage_load_result result = storage_load_script(&doc);
    if (result != STORAGE_LOAD_OK) {
        return result;
    }
    if (capacity < doc.length) {
        return STORAGE_LOAD_CORRUPT;
    }
    copy_memory(text, doc.text, doc.length);
    text[doc.length] = '\0';
    *length = doc.length;
    return STORAGE_LOAD_OK;
}

int storage_save_document(const char *text, size_t length) {
    struct script_store doc;
    zero_memory(&doc, sizeof(doc));
    if (length > DOCUMENT_CAPACITY) {
        return 0;
    }
    copy_memory(doc.text, text, length);
    doc.length = length;
    for (size_t i = 0; i < length; ++i) {
        doc.font[i] = DC_FONT_CHICAGO;
        doc.style[i] = 0;
        doc.size[i] = 1;
    }
    return storage_save_script(&doc);
}

enum storage_load_result storage_load_script(struct script_store *doc) {
    if (!block.ready) {
        return STORAGE_LOAD_NO_MEDIA;
    }
    zero_memory(doc, sizeof(*doc));

    struct document_commit first;
    struct document_commit second;
    static char first_text[DOCUMENT_CAPACITY + 1];
    static char second_text[DOCUMENT_CAPACITY + 1];
    static uint8_t first_attrs[DOCUMENT_CAPACITY];
    static uint8_t second_attrs[DOCUMENT_CAPACITY];
    static uint8_t first_embed[EMBED_IMAGE_BYTES];
    static uint8_t second_embed[EMBED_IMAGE_BYTES];

    int first_v2 = read_slot_v2(0, &first, first_text, first_attrs, first_embed);
    int second_v2 =
        read_slot_v2(1, &second, second_text, second_attrs, second_embed);

    if (first_v2 > 0 || second_v2 > 0) {
        int use_second =
            second_v2 > 0 && (first_v2 <= 0 || second.generation > first.generation);
        struct document_commit *chosen = use_second ? &second : &first;
        const char *source = use_second ? second_text : first_text;
        const uint8_t *attrs = use_second ? second_attrs : first_attrs;
        const uint8_t *embed = use_second ? second_embed : first_embed;
        copy_memory(doc->text, source, chosen->length);
        doc->text[chosen->length] = '\0';
        doc->length = chosen->length;
        for (size_t i = 0; i < chosen->length; ++i) {
            unpack_attr(attrs[i], &doc->font[i], &doc->style[i], &doc->size[i]);
        }
        doc->embed_valid = chosen->embed_valid != 0;
        doc->embed_width = (int)chosen->embed_width;
        doc->embed_height = (int)chosen->embed_height;
        if (doc->embed_valid) {
            copy_memory(doc->embed_image, embed,
                        (size_t)doc->embed_width * doc->embed_height);
        }
        return STORAGE_LOAD_OK;
    }

    /* Fall back to legacy V1 text-only slots. */
    int first_v1 = read_slot_v1(0, &first, first_text);
    int second_v1 = read_slot_v1(1, &second, second_text);
    if (first_v1 < 0 || second_v1 < 0) {
        return STORAGE_LOAD_CORRUPT;
    }
    if (first_v1 == 0 && second_v1 == 0) {
        return STORAGE_LOAD_EMPTY;
    }
    int use_second =
        second_v1 > 0 && (first_v1 == 0 || second.generation > first.generation);
    struct document_commit *chosen = use_second ? &second : &first;
    const char *source = use_second ? second_text : first_text;
    copy_memory(doc->text, source, chosen->length);
    doc->text[chosen->length] = '\0';
    doc->length = chosen->length;
    for (size_t i = 0; i < chosen->length; ++i) {
        doc->font[i] = DC_FONT_CHICAGO;
        doc->style[i] = 0;
        doc->size[i] = 1;
    }
    return STORAGE_LOAD_OK;
}

int storage_save_script(const struct script_store *doc) {
    if (!block.ready || doc->length > DOCUMENT_CAPACITY) {
        return 0;
    }

    struct document_commit first;
    struct document_commit second;
    static char scratch[DOCUMENT_CAPACITY + 1];
    static uint8_t attrs[DOCUMENT_CAPACITY];
    static uint8_t embed_scratch[EMBED_IMAGE_BYTES];
    int first_status = read_slot_v2(0, &first, scratch, attrs, embed_scratch);
    int second_status = read_slot_v2(1, &second, scratch, attrs, embed_scratch);

    uint64_t generation = 1;
    int slot = 0;
    if (first_status > 0 || second_status > 0) {
        if (second_status > 0 &&
            (first_status <= 0 || second.generation > first.generation)) {
            generation = second.generation + 1;
            slot = 0;
        } else if (first_status > 0) {
            generation = first.generation + 1;
            slot = 1;
        }
    }

    zero_memory(scratch, sizeof(scratch));
    zero_memory(attrs, sizeof(attrs));
    copy_memory(scratch, doc->text, doc->length);
    for (size_t i = 0; i < doc->length; ++i) {
        attrs[i] = pack_attr(doc->font[i], doc->style[i], doc->size[i]);
    }

    uint64_t first_sector = (uint64_t)slot * SLOT_SECTORS;
    for (int sector = 0; sector < SCRIPT_TEXT_SECTORS; ++sector) {
        if (!write_sector(first_sector + 1 + sector, scratch + sector * 512)) {
            return 0;
        }
    }
    for (int sector = 0; sector < SCRIPT_ATTR_SECTORS; ++sector) {
        if (!write_sector(first_sector + 1 + SCRIPT_TEXT_SECTORS + sector,
                          attrs + sector * 512)) {
            return 0;
        }
    }
    static uint8_t embed_meta[512];
    zero_memory(embed_meta, sizeof(embed_meta));
    embed_meta[0] = doc->embed_valid ? 1 : 0;
    if (!write_sector(first_sector + 1 + SCRIPT_TEXT_SECTORS + SCRIPT_ATTR_SECTORS,
                      embed_meta)) {
        return 0;
    }
    zero_memory(embed_scratch, sizeof(embed_scratch));
    if (doc->embed_valid) {
        copy_memory(embed_scratch, doc->embed_image,
                    (size_t)doc->embed_width * doc->embed_height);
    }
    for (int sector = 0; sector < EMBED_IMAGE_SECTORS; ++sector) {
        if (!write_sector(first_sector + 1 + SCRIPT_TEXT_SECTORS +
                              SCRIPT_ATTR_SECTORS + 1 + sector,
                          embed_scratch + sector * 512)) {
            return 0;
        }
    }
    flush_disk();

    struct document_commit commit;
    zero_memory(&commit, sizeof(commit));
    commit.magic = DOCUMENT_MAGIC_V2;
    commit.generation = generation;
    commit.generation_inverse = ~generation;
    commit.length = (uint32_t)doc->length;
    commit.length_inverse = ~commit.length;
    commit.embed_valid = doc->embed_valid ? 1U : 0U;
    commit.embed_width = (uint32_t)doc->embed_width;
    commit.embed_height = (uint32_t)doc->embed_height;
    commit.checksum = checksum(scratch, doc->length) ^ checksum(attrs, doc->length);
    if (doc->embed_valid) {
        commit.checksum ^=
            checksum(embed_scratch,
                     (size_t)doc->embed_width * doc->embed_height);
    }
    commit.metadata_checksum =
        checksum(&commit, offsetof(struct document_commit, metadata_checksum));
    if (!write_sector(first_sector, &commit)) {
        return 0;
    }
    flush_disk();
    return 1;
}
