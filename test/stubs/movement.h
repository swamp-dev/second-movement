// Host-test stand-in for movement.h: only the type filesystem.h needs.
#ifndef MOVEMENT_H_
#define MOVEMENT_H_

#include <stdint.h>

// Copied from movement.h.
typedef union {
    struct {
        int16_t latitude : 16;
        int16_t longitude : 16;
    } bit;
    uint32_t reg;
} movement_location_t;

#endif // MOVEMENT_H_
