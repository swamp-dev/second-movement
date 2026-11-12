// Host-test stand-in for watch.h: only what filesystem/filesystem.c uses.
// The flash geometry matches gossamer/chips/saml22/include/instance/nvmctrl.h.
#ifndef WATCH_H_
#define WATCH_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NVMCTRL_PAGE_SIZE 64
#define NVMCTRL_ROW_SIZE 256
#define NVMCTRL_RWWEE_PAGES 128

// Same signatures as watch-library/shared/watch/watch_storage.h; the test provides them.
bool watch_storage_read(uint32_t row, uint32_t offset, uint8_t *buffer, uint32_t size);
bool watch_storage_write(uint32_t row, uint32_t offset, const uint8_t *buffer, uint32_t size);
bool watch_storage_erase(uint32_t row);
bool watch_storage_sync(void);

#endif // WATCH_H_
