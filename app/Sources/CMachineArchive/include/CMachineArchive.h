#ifndef CLASSICMAC_MACHINE_ARCHIVE_H
#define CLASSICMAC_MACHINE_ARCHIVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM_TAR_BLOCK 512
#define CM_SPARSE_HEADER_LIMIT 65536
#define CM_SPARSE_MAP_LIMIT (8 * 1048576)

typedef struct {
    int64_t offset;
    int64_t length;
} CMSparseExtent;

typedef bool (*CMTarReadBlock)(void *context, uint8_t *block);

// The only PAX extension accepted by the machine importer is a sparse disk.
bool cm_sparse_disk_size(const uint8_t *pax, size_t length, int64_t *disk_size);

// Reads the GNU sparse 1.0 map, consuming whole blocks. The caller frees the
// returned extents. A null result indicates a malformed map or a read failure.
CMSparseExtent *cm_sparse_read_map(CMTarReadBlock read, void *context,
                                 int64_t disk_size, int64_t member_size,
                                 size_t *count);

#ifdef __cplusplus
}
#endif
#endif
