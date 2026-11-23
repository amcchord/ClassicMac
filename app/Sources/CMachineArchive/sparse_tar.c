#include "CMachineArchive.h"
#include <stdlib.h>
#include <string.h>

static bool decimal(const uint8_t *s, size_t length, int64_t *value)
{
    if (!length || length > 15) {
        return false;
    }
    *value = 0;
    for (size_t i = 0; i < length; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        *value = *value * 10 + s[i] - '0';
    }
    return true;
}

bool cm_sparse_disk_size(const uint8_t *pax, size_t length, int64_t *disk_size)
{
    const char *keys[] = {
        "GNU.sparse.major", "GNU.sparse.minor", "GNU.sparse.name",
        "GNU.sparse.realsize", "mtime", "atime", "ctime", "uid", "gid",
        "uname", "gname",
    };
    unsigned seen = 0;
    if (!length || length > CM_SPARSE_HEADER_LIMIT) {
        return false;
    }
    while (length) {
        const uint8_t *space = memchr(pax, ' ', length);
        int64_t record;
        if (!space || !decimal(pax, space - pax, &record) ||
            record > (int64_t)length || record < space - pax + 4 ||
            pax[record - 1] != '\n') {
            return false;
        }
        const uint8_t *key = space + 1;
        const uint8_t *end = pax + record - 1;
        const uint8_t *equals = memchr(key, '=', end - key);
        if (!equals || memchr(key, '\0', end - key) ||
            memchr(key, '\n', end - key)) {
            return false;
        }
        size_t index;
        for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++) {
            if ((size_t)(equals - key) == strlen(keys[index]) &&
                !memcmp(key, keys[index], equals - key)) {
                break;
            }
        }
        if (index == sizeof(keys) / sizeof(keys[0]) || (seen & (1u << index))) {
            return false;
        }
        seen |= 1u << index;
        const uint8_t *value = equals + 1;
        size_t size = end - value;
        if ((index == 0 && (size != 1 || value[0] != '1')) ||
            (index == 1 && (size != 1 || value[0] != '0')) ||
            (index == 2 && (size != 8 || memcmp(value, "disk.img", 8))) ||
            (index == 3 && (!decimal(value, size, disk_size) ||
                            *disk_size <= 0 || *disk_size % CM_TAR_BLOCK))) {
            return false;
        }
        pax += record;
        length -= record;
    }
    return (seen & 15u) == 15u;
}

typedef struct {
    CMTarReadBlock read;
    void *context;
    uint8_t block[CM_TAR_BLOCK];
    size_t cursor;
    int64_t bytes;
    int64_t limit;
} MapReader;

static bool map_number(MapReader *map, int64_t *value)
{
    uint8_t digits[15];
    size_t count = 0;
    while (true) {
        if (map->cursor == CM_TAR_BLOCK) {
            if (map->limit - map->bytes < CM_TAR_BLOCK ||
                !map->read(map->context, map->block)) {
                return false;
            }
            map->bytes += CM_TAR_BLOCK;
            map->cursor = 0;
        }
        uint8_t ch = map->block[map->cursor++];
        if (ch == '\n') {
            return decimal(digits, count, value);
        }
        if (count == sizeof(digits) || ch < '0' || ch > '9') {
            return false;
        }
        digits[count++] = ch;
    }
}

CMSparseExtent *cm_sparse_read_map(CMTarReadBlock read, void *context,
                                 int64_t disk_size, int64_t member_size,
                                 size_t *count)
{
    MapReader map = {.read = read, .context = context, .cursor = CM_TAR_BLOCK,
                     .limit = member_size < CM_SPARSE_MAP_LIMIT
                         ? member_size : CM_SPARSE_MAP_LIMIT};
    int64_t entries;
    if (!map_number(&map, &entries) || entries < 1 || entries > (1 << 18)) {
        return NULL;
    }
    CMSparseExtent *extents = malloc((size_t)entries * sizeof(*extents));
    if (!extents) {
        return NULL;
    }
    int64_t end = 0, stored = 0;
    for (int64_t i = 0; i < entries; i++) {
        int64_t offset, length;
        if (!map_number(&map, &offset) || !map_number(&map, &length) ||
            offset < end || offset > disk_size || length > disk_size - offset ||
            offset % CM_TAR_BLOCK || length % CM_TAR_BLOCK ||
            (!length && (i != entries - 1 || offset != disk_size))) {
            free(extents);
            return NULL;
        }
        extents[i] = (CMSparseExtent){offset, length};
        end = offset + length;
        stored += length;
    }
    for (size_t i = map.cursor; i < CM_TAR_BLOCK; i++) {
        if (map.block[i] != 0) {
            free(extents);
            return NULL;
        }
    }
    if (member_size - map.bytes != stored) {
        free(extents);
        return NULL;
    }
    *count = (size_t)entries;
    return extents;
}
