#include "os.h"

#define FDT_MAGIC 0xd00dfeedU
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_END 9U

typedef void (*fdt_virtio_callback)(uintptr_t base, void *context);

static uint32_t fdt_u32(const void *base, uint32_t offset) {
    const uint8_t *bytes = (const uint8_t *)base + offset;
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static int fdt_streq(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int fdt_compatible_is_virtio(const void *value, uint32_t length) {
    const char *cursor = (const char *)value;
    const char *end = cursor + length;
    while (cursor < end && *cursor) {
        if (fdt_streq(cursor, "virtio,mmio")) {
            return 1;
        }
        while (cursor < end && *cursor) {
            ++cursor;
        }
        ++cursor;
    }
    return 0;
}

static int fdt_foreach_virtio_mmio(const void *dtb, fdt_virtio_callback callback,
                                   void *context) {
    if (!dtb || fdt_u32(dtb, 0) != FDT_MAGIC) {
        return 0;
    }

    uint32_t struct_offset = fdt_u32(dtb, 8);
    uint32_t strings_offset = fdt_u32(dtb, 12);
    const uint8_t *cursor = (const uint8_t *)dtb + struct_offset;
    const char *strings = (const char *)dtb + strings_offset;
    int compatible = 0;
    uintptr_t reg_base = 0;
    int found = 0;

    while (1) {
        uint32_t token = fdt_u32(cursor, 0);
        cursor += 4;
        if (token == FDT_END) {
            break;
        }
        if (token == FDT_BEGIN_NODE) {
            compatible = 0;
            reg_base = 0;
            while (*cursor) {
                ++cursor;
            }
            ++cursor;
            cursor = (const uint8_t *)(((uintptr_t)cursor + 3U) & ~((uintptr_t)3U));
            continue;
        }
        if (token == FDT_END_NODE) {
            if (compatible && reg_base != 0) {
                callback(reg_base, context);
                found = 1;
            }
            compatible = 0;
            reg_base = 0;
            continue;
        }
        if (token != FDT_PROP) {
            return found;
        }

        uint32_t length = fdt_u32(cursor, 0);
        uint32_t name_offset = fdt_u32(cursor, 4);
        const uint8_t *value = cursor + 8;
        const char *prop_name = strings + name_offset;
        cursor = value + ((length + 3U) & ~3U);

        if (fdt_streq(prop_name, "compatible") &&
            fdt_compatible_is_virtio(value, length)) {
            compatible = 1;
        } else if (fdt_streq(prop_name, "reg") && length >= 8) {
            reg_base = (uintptr_t)fdt_u32(value, 0);
        }
    }

    return found;
}

int fdt_probe_virtio_mmio(const void *dtb, fdt_virtio_callback callback,
                          void *context) {
    return fdt_foreach_virtio_mmio(dtb, callback, context);
}
