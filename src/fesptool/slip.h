/*
 * slip.h - SLIP protocol encoder/decoder (public API)
 */

#ifndef FESP_SLIP_H
#define FESP_SLIP_H

#include <stdbool.h>
#include <stdint.h>

#define FESP_SLIP_END 0xC0
#define FESP_SLIP_ESC 0xDB
#define FESP_SLIP_ESC_END 0xDC
#define FESP_SLIP_ESC_ESC 0xDD
#define FESP_SLIP_MAX_FRAME 32768

typedef struct {
    uint8_t buf[FESP_SLIP_MAX_FRAME];
    int len;
    bool in_frame;
    bool escaped;
} fesp_slip_ctx_t;

void fesp_slip_init(fesp_slip_ctx_t *ctx);
bool fesp_slip_put_byte(fesp_slip_ctx_t *ctx, uint8_t b);
bool fesp_slip_is_complete(const fesp_slip_ctx_t *ctx);
const uint8_t *fesp_slip_get_payload(const fesp_slip_ctx_t *ctx);
int fesp_slip_get_length(const fesp_slip_ctx_t *ctx);
void fesp_slip_reset(fesp_slip_ctx_t *ctx);
int fesp_slip_encode(const uint8_t *data, int len, uint8_t *out, int out_max);

#endif /* FESP_SLIP_H */
