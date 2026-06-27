/*
 * slip.h - SLIP protocol encoder/decoder
 *
 * Handles SLIP framing for esptool serial communication.
 */

#ifndef ESP_SLIP_H
#define ESP_SLIP_H

#include <stdint.h>
#include <stdbool.h>

/* SLIP protocol constants */
#define SLIP_END 0xC0     /* Frame delimiter */
#define SLIP_ESC 0xDB     /* Escape character */
#define SLIP_ESC_END 0xDC /* Escaped 0xC0 */
#define SLIP_ESC_ESC 0xDD /* Escaped 0xDB */

/* Maximum SLIP frame size (must accommodate largest packet: block_size + 16 + 8
 * header) */
#define SLIP_MAX_FRAME 32768

/* SLIP decoder context */
typedef struct {
    uint8_t buf[SLIP_MAX_FRAME]; /* Frame buffer */
    int len;                  /* Current frame length */
    bool in_frame;            /* true if inside a frame */
    bool escaped;             /* true if previous uint8_t was ESC */
} SLIP_CTX;

/*
 * Slip_Init - Initialize SLIP decoder
 */
void Slip_Init(SLIP_CTX *ctx);

/*
 * Slip_PutByte - Feed a uint8_t to the decoder
 *
 * Returns true when frame is complete.
 */
bool Slip_PutByte(SLIP_CTX *ctx, uint8_t b);

/*
 * Slip_IsComplete - Check if a complete frame has been received
 */
bool Slip_IsComplete(const SLIP_CTX *ctx);

/*
 * Slip_GetPayload - Get pointer to decoded frame payload
 */
const uint8_t *Slip_GetPayload(const SLIP_CTX *ctx);

/*
 * Slip_GetLength - Get decoded frame length
 */
int Slip_GetLength(const SLIP_CTX *ctx);

/*
 * Slip_Reset - Reset decoder state for next frame
 */
void Slip_Reset(SLIP_CTX *ctx);

/*
 * Slip_Encode - Encode data into SLIP frame
 *
 * Returns encoded length.
 */
int Slip_Encode(const uint8_t *data, int len, uint8_t *out, int out_max);

#endif
