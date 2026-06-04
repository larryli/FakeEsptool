/*
 * deflate.h - Minimal DEFLATE decompressor
 *
 * Implements RFC 1951 DEFLATE decompression for esptool protocol.
 * Only supports decompression (no compression).
 */

#ifndef DEFLATE_H
#define DEFLATE_H

#include <windows.h>

/* Return codes */
#define DEFLATE_OK          0
#define DEFLATE_ERROR      -1
#define DEFLATE_BAD_INPUT  -2
#define DEFLATE_NO_MEMORY  -3

/* Maximum window size for DEFLATE (32KB) */
#define DEFLATE_MAX_WINDOW  32768

/* Maximum match length for LZ77 */
#define DEFLATE_MAX_MATCH   258

/* Minimum match length for LZ77 */
#define DEFLATE_MIN_MATCH   3

/* Huffman code structure */
typedef struct {
    WORD *counts;    /* Number of codes of each length */
    WORD *symbols;   /* Symbols sorted by code */
    int max_length;  /* Maximum code length */
} DEFLATE_HUFF;

/* Decompressor context */
typedef struct {
    const BYTE *in_buf;     /* Input buffer */
    size_t in_len;          /* Input length */
    size_t in_pos;          /* Current position in input */

    BYTE *out_buf;          /* Output buffer */
    size_t out_len;         /* Output buffer size */
    size_t out_pos;         /* Current position in output */

    DWORD bit_buf;          /* Bit buffer */
    int bit_count;          /* Number of bits in bit buffer */

    DEFLATE_HUFF lit_huff;  /* Literal/length Huffman code */
    DEFLATE_HUFF dist_huff; /* Distance Huffman code */
} DEFLATE_CTX;

/*
 * Decompress DEFLATE data
 *
 * ctx: Decompressor context (must be initialized)
 * Returns: DEFLATE_OK on success, negative error code on failure
 */
int deflate_decompress(DEFLATE_CTX *ctx);

/*
 * Initialize decompressor context
 *
 * ctx: Context to initialize
 * in_buf: Input buffer with compressed data
 * in_len: Length of input data
 * out_buf: Output buffer for decompressed data
 * out_len: Size of output buffer
 */
void deflate_init(DEFLATE_CTX *ctx, const BYTE *in_buf, size_t in_len,
                  BYTE *out_buf, size_t out_len);

#endif /* DEFLATE_H */
