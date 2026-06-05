/*
 * deflate.c - Minimal DEFLATE decompressor implementation
 *
 * Implements RFC 1951 DEFLATE decompression for esptool protocol.
 * Only supports decompression (no compression).
 */

#include "deflate.h"
#include <string.h>

/* Memory allocation helpers */
static inline void *deflate_malloc(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static inline void deflate_free(void *ptr) {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

/* Static Huffman code tables for DEFLATE */
static const WORD deflate_lit_lengths[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const BYTE deflate_lit_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const WORD deflate_dist_codes[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

static const BYTE deflate_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* Code length order for dynamic block */
static const BYTE deflate_cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* Read bits from bit buffer */
static int deflate_read_bits(DEFLATE_CTX *ctx, int count)
{
    int value = 0;
    int shift = 0;

    while (count > 0) {
        if (ctx->bit_count == 0) {
            if (ctx->in_pos >= ctx->in_len)
                return -1;
            ctx->bit_buf = ctx->in_buf[ctx->in_pos++];
            ctx->bit_count = 8;
        }

        int bits = count;
        if (bits > ctx->bit_count)
            bits = ctx->bit_count;

        value |= (ctx->bit_buf & ((1 << bits) - 1)) << shift;
        ctx->bit_buf >>= bits;
        ctx->bit_count -= bits;
        shift += bits;
        count -= bits;
    }

    return value;
}

/* Build Huffman code from code lengths */
static int deflate_build_huffman(DEFLATE_HUFF *huff, const BYTE *lengths, int count)
{
    int i;
    int max_len = 0;
    int total = 0;

    /* Find maximum code length */
    for (i = 0; i < count; i++) {
        if (lengths[i] > max_len)
            max_len = lengths[i];
    }

    if (max_len == 0) {
        huff->counts = NULL;
        huff->symbols = NULL;
        huff->max_length = 0;
        return DEFLATE_OK;
    }

    /* Allocate counts array */
    huff->counts = (WORD *)deflate_malloc((max_len + 1) * sizeof(WORD));
    if (!huff->counts)
        return DEFLATE_NO_MEMORY;

    /* Count codes of each length */
    for (i = 0; i < count; i++) {
        if (lengths[i] > 0)
            huff->counts[lengths[i]]++;
    }

    /* Calculate total number of symbols */
    for (i = 1; i <= max_len; i++)
        total += huff->counts[i];

    /* Allocate symbols array */
    huff->symbols = (WORD *)deflate_malloc(total * sizeof(WORD));
    if (!huff->symbols) {
        deflate_free(huff->counts);
        huff->counts = NULL;
        return DEFLATE_NO_MEMORY;
    }

    /* Sort symbols by code length */
    {
        WORD *offsets = (WORD *)deflate_malloc((max_len + 1) * sizeof(WORD));
        if (!offsets) {
            deflate_free(huff->counts);
            deflate_free(huff->symbols);
            huff->counts = NULL;
            huff->symbols = NULL;
            return DEFLATE_NO_MEMORY;
        }

        /* Calculate offsets for each code length */
        offsets[1] = 0;
        for (i = 2; i <= max_len; i++)
            offsets[i] = offsets[i - 1] + huff->counts[i - 1];

        /* Place symbols in sorted order */
        for (i = 0; i < count; i++) {
            if (lengths[i] > 0)
                huff->symbols[offsets[lengths[i]]++] = (WORD)i;
        }

        deflate_free(offsets);
    }

    huff->max_length = max_len;
    return DEFLATE_OK;
}

/* Free Huffman code */
static void deflate_free_huffman(DEFLATE_HUFF *huff)
{
    if (huff->counts) {
        deflate_free(huff->counts);
        huff->counts = NULL;
    }
    if (huff->symbols) {
        deflate_free(huff->symbols);
        huff->symbols = NULL;
    }
    huff->max_length = 0;
}

/* Decode a Huffman code */
static int deflate_decode_huffman(DEFLATE_CTX *ctx, const DEFLATE_HUFF *huff)
{
    int code = 0;
    int first = 0;
    int index = 0;
    int len;

    for (len = 1; len <= huff->max_length; len++) {
        int bit = deflate_read_bits(ctx, 1);
        if (bit < 0)
            return -1;

        code = (code << 1) | bit;

        int count = huff->counts[len];
        if (code - first < count)
            return huff->symbols[index + (code - first)];

        index += count;
        first = (first + count) << 1;
    }

    return -1;
}

/* Decode a dynamic Huffman block */
static int deflate_decode_dynamic(DEFLATE_CTX *ctx)
{
    int hlit, hdist, hclen;
    int i, j;
    BYTE cl_lengths[19] = {0};
    BYTE lengths[320];  /* Max literal/length + distance codes */
    DEFLATE_HUFF cl_huff = {0};
    int ret;

    /* Read code length counts */
    hlit = deflate_read_bits(ctx, 5);
    hdist = deflate_read_bits(ctx, 5);
    hclen = deflate_read_bits(ctx, 4);

    if (hlit < 0 || hdist < 0 || hclen < 0)
        return DEFLATE_BAD_INPUT;

    hlit += 257;
    hdist += 1;
    hclen += 4;

    /* Read code length code lengths */
    for (i = 0; i < hclen; i++) {
        int len = deflate_read_bits(ctx, 3);
        if (len < 0)
            return DEFLATE_BAD_INPUT;
        cl_lengths[deflate_cl_order[i]] = (BYTE)len;
    }

    /* Build code length Huffman code */
    ret = deflate_build_huffman(&cl_huff, cl_lengths, 19);
    if (ret != DEFLATE_OK)
        return ret;

    /* Decode literal/length and distance code lengths */
    i = 0;
    while (i < hlit + hdist) {
        int sym = deflate_decode_huffman(ctx, &cl_huff);
        if (sym < 0) {
            deflate_free_huffman(&cl_huff);
            return DEFLATE_BAD_INPUT;
        }

        if (sym < 16) {
            lengths[i++] = (BYTE)sym;
        } else if (sym == 16) {
            if (i == 0) {
                deflate_free_huffman(&cl_huff);
                return DEFLATE_BAD_INPUT;
            }
            int repeat = deflate_read_bits(ctx, 2) + 3;
            if (repeat < 0) {
                deflate_free_huffman(&cl_huff);
                return DEFLATE_BAD_INPUT;
            }
            {
                BYTE prev = lengths[i - 1];
                for (j = 0; j < repeat && i < hlit + hdist; j++)
                    lengths[i++] = prev;
            }
        } else if (sym == 17) {
            int repeat = deflate_read_bits(ctx, 3) + 3;
            if (repeat < 0) {
                deflate_free_huffman(&cl_huff);
                return DEFLATE_BAD_INPUT;
            }
            for (j = 0; j < repeat && i < hlit + hdist; j++)
                lengths[i++] = 0;
        } else if (sym == 18) {
            int repeat = deflate_read_bits(ctx, 7) + 11;
            if (repeat < 0) {
                deflate_free_huffman(&cl_huff);
                return DEFLATE_BAD_INPUT;
            }
            for (j = 0; j < repeat && i < hlit + hdist; j++)
                lengths[i++] = 0;
        } else {
            deflate_free_huffman(&cl_huff);
            return DEFLATE_BAD_INPUT;
        }
    }

    deflate_free_huffman(&cl_huff);

    /* Build literal/length Huffman code */
    ret = deflate_build_huffman(&ctx->lit_huff, lengths, hlit);
    if (ret != DEFLATE_OK)
        return ret;

    /* Build distance Huffman code */
    ret = deflate_build_huffman(&ctx->dist_huff, lengths + hlit, hdist);
    if (ret != DEFLATE_OK) {
        deflate_free_huffman(&ctx->lit_huff);
        return ret;
    }

    return DEFLATE_OK;
}

/* Decode a static Huffman block */
static int deflate_decode_static(DEFLATE_CTX *ctx)
{
    BYTE lengths[288];
    int i;

    /* Literal/length code lengths */
    for (i = 0; i < 144; i++)
        lengths[i] = 8;
    for (i = 144; i < 256; i++)
        lengths[i] = 9;
    for (i = 256; i < 280; i++)
        lengths[i] = 7;
    for (i = 280; i < 288; i++)
        lengths[i] = 8;

    /* Build literal/length Huffman code */
    int ret = deflate_build_huffman(&ctx->lit_huff, lengths, 288);
    if (ret != DEFLATE_OK)
        return ret;

    /* Distance code lengths (all 5 bits) */
    for (i = 0; i < 32; i++)
        lengths[i] = 5;

    /* Build distance Huffman code */
    ret = deflate_build_huffman(&ctx->dist_huff, lengths, 32);
    if (ret != DEFLATE_OK) {
        deflate_free_huffman(&ctx->lit_huff);
        return ret;
    }

    return DEFLATE_OK;
}

/* Process a DEFLATE block */
static int deflate_process_block(DEFLATE_CTX *ctx)
{
    int bfinal, btype;
    int ret;

    /* Read block header */
    bfinal = deflate_read_bits(ctx, 1);
    btype = deflate_read_bits(ctx, 2);

    if (bfinal < 0 || btype < 0)
        return DEFLATE_BAD_INPUT;

    /* Handle block based on type */
    if (btype == 0) {
        /* Uncompressed block */
        int len, nlen;

        /* Align to byte boundary */
        ctx->bit_buf = 0;
        ctx->bit_count = 0;

        /* Read length */
        if (ctx->in_pos + 4 > ctx->in_len)
            return DEFLATE_BAD_INPUT;

        len = ctx->in_buf[ctx->in_pos] | (ctx->in_buf[ctx->in_pos + 1] << 8);
        nlen = ctx->in_buf[ctx->in_pos + 2] | (ctx->in_buf[ctx->in_pos + 3] << 8);
        ctx->in_pos += 4;

        /* Verify length */
        if (len != (~nlen & 0xFFFF))
            return DEFLATE_BAD_INPUT;

        /* Check bounds */
        if (ctx->in_pos + len > ctx->in_len)
            return DEFLATE_BAD_INPUT;
        if (ctx->out_pos + len > ctx->out_len)
            return DEFLATE_ERROR;

        /* Copy data */
        memcpy(ctx->out_buf + ctx->out_pos, ctx->in_buf + ctx->in_pos, len);
        ctx->in_pos += len;
        ctx->out_pos += len;
    } else if (btype == 1 || btype == 2) {
        /* Static or dynamic Huffman block */
        if (btype == 1) {
            ret = deflate_decode_static(ctx);
        } else {
            ret = deflate_decode_dynamic(ctx);
        }

        if (ret != DEFLATE_OK)
            return ret;

        /* Decode symbols */
        for (;;) {
            int sym = deflate_decode_huffman(ctx, &ctx->lit_huff);
            if (sym < 0) {
                deflate_free_huffman(&ctx->lit_huff);
                deflate_free_huffman(&ctx->dist_huff);
                return DEFLATE_BAD_INPUT;
            }

            /* End of block */
            if (sym == 256) {
                deflate_free_huffman(&ctx->lit_huff);
                deflate_free_huffman(&ctx->dist_huff);
                break;
            }

            /* Literal byte */
            if (sym < 256) {
                if (ctx->out_pos >= ctx->out_len) {
                    deflate_free_huffman(&ctx->lit_huff);
                    deflate_free_huffman(&ctx->dist_huff);
                    return DEFLATE_ERROR;
                }
                ctx->out_buf[ctx->out_pos++] = (BYTE)sym;
                continue;
            }

            /* Length/distance pair */
            int length_idx = sym - 257;
            if (length_idx < 0 || length_idx >= 29) {
                deflate_free_huffman(&ctx->lit_huff);
                deflate_free_huffman(&ctx->dist_huff);
                return DEFLATE_BAD_INPUT;
            }

            int length = deflate_lit_lengths[length_idx];
            int extra = deflate_lit_extra[length_idx];
            if (extra > 0) {
                int extra_bits = deflate_read_bits(ctx, extra);
                if (extra_bits < 0) {
                    deflate_free_huffman(&ctx->lit_huff);
                    deflate_free_huffman(&ctx->dist_huff);
                    return DEFLATE_BAD_INPUT;
                }
                length += extra_bits;
            }

            /* Decode distance */
            int dist_sym = deflate_decode_huffman(ctx, &ctx->dist_huff);
            if (dist_sym < 0 || dist_sym >= 30) {
                deflate_free_huffman(&ctx->lit_huff);
                deflate_free_huffman(&ctx->dist_huff);
                return DEFLATE_BAD_INPUT;
            }

            int distance = deflate_dist_codes[dist_sym];
            extra = deflate_dist_extra[dist_sym];
            if (extra > 0) {
                int extra_bits = deflate_read_bits(ctx, extra);
                if (extra_bits < 0) {
                    deflate_free_huffman(&ctx->lit_huff);
                    deflate_free_huffman(&ctx->dist_huff);
                    return DEFLATE_BAD_INPUT;
                }
                distance += extra_bits;
            }

            /* Copy match */
            if ((size_t)length > ctx->out_len - ctx->out_pos) {
                deflate_free_huffman(&ctx->lit_huff);
                deflate_free_huffman(&ctx->dist_huff);
                return DEFLATE_ERROR;
            }

            if ((size_t)distance > ctx->out_pos) {
                deflate_free_huffman(&ctx->lit_huff);
                deflate_free_huffman(&ctx->dist_huff);
                return DEFLATE_BAD_INPUT;
            }

            /* Handle overlapping copies */
            int src = ctx->out_pos - distance;
            for (int k = 0; k < length; k++) {
                ctx->out_buf[ctx->out_pos + k] = ctx->out_buf[src + k];
            }
            ctx->out_pos += length;
        }
    } else {
        /* Reserved block type */
        return DEFLATE_BAD_INPUT;
    }

    return bfinal ? DEFLATE_OK : 1;  /* Return 1 if not final block */
}

/* Decompress DEFLATE data */
int deflate_decompress(DEFLATE_CTX *ctx)
{
    int ret;

    /* Process blocks until final block */
    do {
        ret = deflate_process_block(ctx);
        if (ret < 0)
            return ret;
    } while (ret == 1);

    return DEFLATE_OK;
}

/* Initialize decompressor context */
void deflate_init(DEFLATE_CTX *ctx, const BYTE *in_buf, size_t in_len,
                  BYTE *out_buf, size_t out_len)
{
    ctx->in_buf = in_buf;
    ctx->in_len = in_len;
    ctx->in_pos = 0;

    ctx->out_buf = out_buf;
    ctx->out_len = out_len;
    ctx->out_pos = 0;

    ctx->bit_buf = 0;
    ctx->bit_count = 0;

    ctx->lit_huff.counts = NULL;
    ctx->lit_huff.symbols = NULL;
    ctx->lit_huff.max_length = 0;

    ctx->dist_huff.counts = NULL;
    ctx->dist_huff.symbols = NULL;
    ctx->dist_huff.max_length = 0;
}
