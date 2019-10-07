/*
  gust_enc - Encoder/Decoder for Gust (Koei/Tecmo) .e files
  Copyright © 2019 VitaSmith

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*

  Note to Gust: I love you. And I love playing your games.

  And yet I can't remember the last time I've had as much fun as I did
  breaking this weird unscrambling/decompression algorithm of yours.

  So thank you very much for (unwillingly) creating one of the best
  detective games ever!

  And _please_ don't try to fight modders: We are on your side!

  Sincerely,

  -- VitaSmith, 2019-10-02

 */

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"
#include "parson.h"

#define E_HEADER_SIZE       0x10
#define E_FOOTER_SIZE       0x10

// Both of these are prime numbers
#define SEED_CONSTANT       0x3b9a73c9
#define SEED_INCREMENT      0x2f09

// For Sophie's GrowData.xml.e
//#define VALIDATE_CHECKSUM   0x52ccbbab
// For Sophie's Marquee.xml.e
//#define VALIDATE_CHECKSUM 0x92ca8716

//#define CREATE_EXTRA_FILES

typedef struct {
    uint32_t main[3];
    uint32_t table[3];
    uint32_t length[3];
    uint32_t fence;
} seed_data;

/*
 * Stupid sexy scramblers ("Feels like I'm reading nothing at all!")
 *
 * All the scramblers below are seeded scramblers that derive values from the formula
 * seed[1] = seed[0] * seed[1] + 0x2f09, with seed[0] being prime number 0x3b9a73c9
 * (or a variation thereof) and seed[1] another 16-bit prime number.
 *
 * From there, they only differ in the manner with which they use the updated seed.
 */

// Scramble individual bits between two semi-random bit positions within a slice.
static bool bit_scrambler(uint8_t* chunk, uint32_t chunk_size, uint32_t seed[2],
                          uint16_t slice_size, bool descramble)
{
    // Table_size needs to be 8 * slice_size, to encompass all individual bit positions
    uint32_t x, table_size = slice_size << 3;

    uint16_t* base_table = calloc(table_size, sizeof(uint16_t));
    uint16_t* scrambling_table = calloc(table_size, sizeof(uint16_t));
    if ((table_size < 4) || (base_table == NULL) || (scrambling_table == NULL)) {
        free(base_table);
        free(scrambling_table);
        return false;
    }

    uint8_t* max_chunk = &chunk[chunk_size];
    while (chunk < max_chunk) {
        // Make sure we don't overflow our table, else we're going to pick
        // bits located outside our chunk
        table_size = min(table_size, chunk_size << 3);
        // Create a base table of incremental 16-bit values
        for (uint32_t i = 0; i < table_size; i++)
            base_table[i] = (uint16_t)i;

        // Now create a scrambled table from the above
        for (uint32_t i = 0; i < table_size; i++) {
            seed[1] = seed[0] * seed[1] + SEED_INCREMENT;
            // Translate this semi-random value to a base_table index we haven't used yet
            x = ((seed[1] >> 16) & 0x7FFF) % (table_size - i);
            scrambling_table[i] = base_table[x];
            // Now remove the value we used from base_table
            memmove(&base_table[x], &base_table[x + 1], (size_t)(table_size - i - x) * 2);
        }

        // This scrambler uses a pair of byte and bit positions that are derived from
        // values picked in the scrambling table (>>3 for byte pos and &7 for bit pos)
        // From there, the scrambler swaps the bits at position p0.b0 and p1.b1.
        // To perform the reverse operation, the scrambling table must be parsed in the
        // reverse direction since sequential bit swaps are not commutative.
        uint8_t p0, p1, b0, b1, v0, v1;
        int32_t start_value = descramble ? 0 : (int32_t)min(table_size, chunk_size << 3) - 2;
        int32_t increment = descramble ? +2 : -2;
        for (int32_t i = start_value; (i >= 0) && (i < (int32_t)min(table_size, chunk_size << 3)); i += increment) {
            p0 = (uint8_t)(scrambling_table[i] >> 3);
            b0 = (uint8_t)(scrambling_table[i] & 7);
            p1 = (uint8_t)(scrambling_table[i + 1] >> 3);
            b1 = (uint8_t)(scrambling_table[i + 1] & 7);
            // Keep the bit values
            v0 = (chunk[p0] & (1 << b0)) >> b0;
            v1 = (chunk[p1] & (1 << b1)) >> b1;
            // Filter out bit b0 from the byte at position b
            chunk[p0] &= ~(1 << b0);
            chunk[p0] |= v1 << b0;
            chunk[p1] &= ~(1 << b1);
            chunk[p1] |= v0 << b1;
        }

        chunk = &chunk[slice_size];
        chunk_size -= slice_size;
    }

    free(base_table);
    free(scrambling_table);
    return true;
}

// Sequentially scramble bytes by adding the updated seed and, depending on whether
// the modulo with the current seed falls above or below a "fence", XORing the seed.
static bool fenced_scrambler(uint8_t* buf, uint32_t buf_size, seed_data* seeds, bool descramble)
{
    uint32_t seed[2] = { SEED_CONSTANT, seeds->main[1] };
    for (uint32_t i = 0; i < buf_size; i += 2) {
        seed[1] = seed[0] * seed[1] + SEED_INCREMENT;
        uint32_t x = (seed[1] >> 16) & 0x7fff;
        uint16_t w = getbe16(&buf[i]);
        // I strongly suspect that the fence is derived from the other seeds
        // but I haven't been able to figure the mathematical formula for that yet.
        if (descramble) {
            if (x % seeds->fence >= seeds->fence / 2)
                w ^= (uint16_t)x;
            w -= (uint16_t)x;
        } else {
            w += (uint16_t)x;
            if (x % seeds->fence >= seeds->fence / 2)
                w ^= (uint16_t)x;
        }
        setbe16(&buf[i], w);
    }
    return true;
}

// Sequentially scramble bytes by XORing them with a set of 3 rotated seeds.
static bool rotating_scrambler(uint8_t* buf, uint32_t buf_size, seed_data* seeds, uint32_t file_checksum)
{
    uint32_t seed[2] = { file_checksum + SEED_CONSTANT, seeds->table[0] };
    // We're updating seed values in the table, so make sure we work on a copy
    uint32_t seed_table[3] = { seeds->table[0], seeds->table[1], seeds->table[2] };
    uint32_t seed_index = 0;
    uint32_t seed_switch_fudge = 0;
    uint32_t processed_for_this_seed = 0;
    for (uint32_t i = 0; i < buf_size; i++) {
        seed[1] = seed[0] * seed[1] + SEED_INCREMENT;
        buf[i] ^= seed[1] >> 16;
        if (++processed_for_this_seed >= seeds->length[seed_index] + seed_switch_fudge) {
            seed_table[seed_index++] = seed[1];
            if (seed_index >= array_size(seed_table)) {
                seed_index = 0;
                seed_switch_fudge++;
            }
            seed[1] = seed_table[seed_index];
            processed_for_this_seed = 0;
        }
    }
    return true;
}

/*
  The following functions deal with the compression algorithm used by Gust, which
  looks like a derivative of LZ4 that I am calling 'Glaze', for "Gust Lempel–Ziv".
 */
typedef struct {
    uint8_t* buffer;
    uint32_t size;
    uint32_t pos;
    int getbits_buffer;
    int getbits_mask;
} getbits_ctx;

#define GETBITS_EOF 0xffffffff
static uint32_t getbits(getbits_ctx* ctx, int n)
{
    int x = 0;

    for (int i = 0; i < n; i++) {
        if (ctx->getbits_mask == 0x00) {
            if (ctx->pos >= ctx->size)
                return GETBITS_EOF;
            ctx->getbits_buffer = ctx->buffer[ctx->pos++];
            ctx->getbits_mask = 0x80;
        }
        x <<= 1;
        if (ctx->getbits_buffer & ctx->getbits_mask)
            x++;
        ctx->getbits_mask >>= 1;
    }

    return x;
}

// Boy with extended open hand, looking at butterfly: "Is this Huffman encoding?"
static uint8_t* build_code_table(uint8_t* bitstream, uint32_t bitstream_length)
{
    uint32_t code_table_length = getbe32(bitstream);
    uint8_t* code_table = malloc(code_table_length);
    if (code_table == NULL)
        return NULL;
    getbits_ctx ctx = { 0 };
    ctx.buffer = &bitstream[sizeof(uint32_t)];
    ctx.size = bitstream_length - sizeof(uint32_t);

    for (uint32_t c = getbits(&ctx, 1), i = 0; i < code_table_length; c = getbits(&ctx, 1), i++) {
        if (c == GETBITS_EOF) {
            break;
        } else if (c == 1) {
            // Bit sequence starts with 1 -> emit code 0x01
            code_table[i] = 1;
        } else {
            // Bit sequence starts with 0 -> get the length of code and emit it
            int code_len = 0;
            while ((++code_len < 8) && ((c = getbits(&ctx, 1)) == 0));
            if (c == GETBITS_EOF)
                break;
            if (code_len < 8)
                code_table[i] = (uint8_t)((c << code_len) | getbits(&ctx, code_len));
            else
                code_table[i] = 0;
        }
    }

    return code_table;
}

// Uncompress a glaze compressed buffer
static uint32_t unglaze(uint8_t* src, uint32_t src_length, uint8_t* dst, uint32_t dst_length)
{
    uint32_t dec_length = getbe32(src);
    src = &src[sizeof(uint32_t)];
    if (dec_length > dst_length) {
        fprintf(stderr, "ERROR: Glaze decompression buffer is too small\n");
        return 0;
    }

    uint32_t bitstream_length = getbe32(src);
    src = &src[sizeof(uint32_t)];
    if (bitstream_length <= sizeof(uint32_t)) {
        fprintf(stderr, "ERROR: Glaze decompression bitstream is too small\n");
        return 0;
    }

    uint32_t chk_length = bitstream_length + sizeof(uint32_t);
    if (chk_length >= src_length) {
        fprintf(stderr, "ERROR: Glaze decompression bitstream is too large\n");
        return 0;
    }

    uint32_t code_len = code_len = getbe32(src);
    uint8_t* code_table = build_code_table(src, bitstream_length);
    if (code_table == NULL)
        return 0;

    uint8_t* dict = &src[bitstream_length];
    uint32_t dict_len = getbe32(dict);
    dict = &dict[sizeof(uint32_t)];
    chk_length += dict_len + sizeof(uint32_t);
    if (chk_length >= src_length) {
        fprintf(stderr, "ERROR: Glaze decompression dictionary is too large\n");
        free(code_table);
        return 0;
    }

    uint8_t* len = &dict[dict_len];
    uint8_t* max_dict = len;
    uint32_t len_len = getbe32(len);
    len = &len[sizeof(uint32_t)];
    uint8_t* max_len = &len[len_len];
    chk_length += len_len + sizeof(uint32_t);
    if (chk_length >= src_length) {
        fprintf(stderr, "ERROR: Glaze decompression length table is too large\n");
        free(code_table);
        return 0;
    }

    int l, d;
    uint8_t* dst_max = &dst[dec_length];
    uint8_t* code = code_table;
    uint8_t* max_code = &code_table[code_len];
    while (dst < dst_max) {
        // Sanity checks
        if ((dict > max_dict) || (len > max_len) || (code > max_code)) {
            fprintf(stderr, "ERROR: Glaze decompression overflow\n");
            free(code_table);
            return 0;
        }
        switch (*code++) {
        case 0x01:  // 1-byte code
            // Copy one byte
            *dst++ = *dict++;
            break;
        case 0x02:  // 2-byte code
            // Duplicate one byte from pos -d where d is provided by the code table
            d = *code++;
            *dst++ = dst[-d];
            break;
        case 0x03:  // 3-byte code
            // Duplicate l bytes from position -(d + l) where both d and l are provided by the code table
            d = *code++;
            l = *code++;
            d += l;
            for (int i = ++l; i > 0; i--)
                *dst++ = dst[-d];
            break;
        case 0x04:  // 2-byte code
            // Duplicate l bytes from position -(d + l) where l is provided by the code table and d by the source
            l = *code++;
            d = *dict++ + l;
            for (int i = ++l; i > 0; i--)
                *dst++ = dst[-d];
            break;
        case 0x05:  // 3-byte code
            // Same as above except with a 16-bit distance where the MSB is provided by the code table and LSB by the source
            d = *code++ << 8 | *dict++;
            l = *code++;
            d += l;
            for (int i = ++l; i > 0; i--)
                *dst++ = dst[-d];
            break;
        case 0x06:  // 2-byte code
            // Copy l + 8 bytes from source where l is provided by the code table
            l = *code++ + 8;
            for (int i = l; i > 0; i--) {
                *dst++ = *dict++;
                if (dst > dst_max) {
                    fprintf(stderr, "WARNING: Dictionary overflow for bytecode 0x06 (%d bytes)\n", i);
                    break;
                }
            }
            break;
        case 0x07:  // 1-byte code + 1 byte from length table
            // Copy l + 14 bytes from the source where l is provided by the (separate) length table
            for (int i = *len++ + 14; i > 0; i--) {
                *dst++ = *dict++;
                if (dst > dst_max) {
                    fprintf(stderr, "WARNING: Dictionary overflow for bytecode 0x07 (%d bytes)\n", i);
                    break;
                }
            }
            break;
        }
    }

    free(code_table);
    return dec_length;
}

// "Compress" a payload
static uint32_t glaze(uint8_t* src, uint32_t src_size, uint8_t** dst)
{
    // Now, there is no way in hell I'm going to craft a bona fide LZ compressor when
    // I have a strong suspicion that this Glaze format that Gust uses comes from a
    // known public compression algorithm, that we simply haven't identified yet.
    // Considering that we have a length table, allowing us to copy ~256 bytes of
    // literals with a single bytecode, we're going to take a massive shortcut by:
    // - Copying all our decompressed data, as is, to the dictionary table
    // - Creating a length table for as many 256-byte blocks we need
    // - Creating a bytecode table, made of only 0x07's, so that only straight block
    //   copies from the dictionary are enacted.
    // Of course, this means the resulting file won't be compressed in the slightest.
    // But we don't really care about that for modding, do we?...

    bool short_last_block = (src_size % 256 <= 14);
    uint32_t num_blocks = ((src_size + 255) / 256);
    if (short_last_block)
        num_blocks--;
    // Each block translates to a 5-bit bitstream code (00111b) that yields bytecode 0x07
    uint32_t bitstream_size = ((5 * num_blocks) + 7) / 8;
    // A Glaze compressed file is structured as follows:
    // [decompressed_size] [bistream_size] [bytecode_size] <...bitstream...>
    // [dictionary_size] <...dictionary...> [length_table_size] <...length_table...>
    uint32_t compressed_size = 3 * sizeof(uint32_t) + bitstream_size + sizeof(uint32_t) + src_size + sizeof(uint32_t) + num_blocks;
    *dst = malloc(compressed_size);
    if (*dst == NULL)
        return 0;
    uint8_t* pos = *dst;
    setbe32(pos, src_size);
    pos = &pos[sizeof(uint32_t)];
    // The bitstream size includes the bytecode size field
    setbe32(pos, bitstream_size + sizeof(uint32_t));
    pos = &pos[sizeof(uint32_t)];
    // The bytecode size is our number of blocks
    setbe32(pos, num_blocks);
    pos = &pos[sizeof(uint32_t)];

    // Our bitstream data repeats every 5 bytes, which we use to our advantage
    for (uint32_t i = 0; i < bitstream_size; i += 5) {
        pos[i] = 0x39;
        pos[i + 1] = 0xce;
        pos[i + 2] = 0x73;
        pos[i + 3] = 0x9c;
        pos[i + 4] = 0xe7;
    }
    // Zero the overflow bitstream data just in case
    uint32_t nb_stream_bits_in_last_byte = (5 * num_blocks) % 8;
    if (nb_stream_bits_in_last_byte != 0)
        pos[bitstream_size - 1] &= 0xff << (8 - nb_stream_bits_in_last_byte);

    // Now copy the "dictionary" which is just a verbatim copy of our input
    pos = &pos[bitstream_size];
    setbe32(pos, src_size);
    pos = &pos[sizeof(uint32_t)];
    memcpy(pos, src, src_size);

    // Finally we add our length table
    pos = &pos[src_size];
    setbe32(pos, num_blocks);
    pos = &pos[sizeof(uint32_t)];
    memset(pos, 256 - 14, num_blocks - 1);
    pos = &pos[num_blocks - 1];
    // Our last block can be 1 to 256 bytes in length, but the size is offset by 14
    if (short_last_block)
        *pos = 255 - 14 + (src_size % 256);
    else
        *pos = (src_size - 14) % 256;

    return compressed_size;
}

static bool scramble(uint8_t* payload, uint32_t payload_size, char* path, seed_data* seeds,
                     uint32_t working_size)
{
    bool r = false;
    uint32_t checksum[3] = { 0, 0, 0 };

    // Align the size (plus an extra byte for the end marker) to 16-bytes
    uint32_t main_payload_size = (payload_size + 1 + 0xf) & ~0xf;
    uint8_t* buf = calloc((size_t)main_payload_size + E_HEADER_SIZE + E_FOOTER_SIZE, 1);
    if (buf == NULL)
        return false;
    uint8_t* main_payload = &buf[E_HEADER_SIZE];
    memcpy(main_payload, payload, payload_size);

    // Compute the last checksum
#if !defined(VALIDATE_CHECKSUM)
    for (uint32_t i = 0; i < payload_size; i += sizeof(uint32_t))
        checksum[2] += getbe32(&payload[i]);
#else
    checksum[2] = VALIDATE_CHECKSUM;
#endif

    // Scramble the beginning of the file
    uint32_t seed[2] = { checksum[2] + SEED_CONSTANT, seeds->main[2] };
    if (!bit_scrambler(main_payload, min(payload_size, 0x800), seed, 0x80, false))
        goto out;

    // Compute the other checksums
    for (uint32_t i = 0; i < (payload_size & ~3); i += sizeof(uint32_t)) {
        checksum[0] -= getbe32(&main_payload[i]);
        checksum[1] ^= ~getbe32(&main_payload[i]);
    }

    // Write the checksums
    setbe32(&main_payload[(size_t)main_payload_size + 4], checksum[0]);
    setbe32(&main_payload[(size_t)main_payload_size + 8], checksum[1]);
    setbe32(&main_payload[(size_t)main_payload_size + 12], checksum[2]);

    // Call the main scrambler
    if (!rotating_scrambler(main_payload, payload_size, seeds, checksum[2]))
        goto out;

    // Add the end of payload marker
    main_payload[payload_size] = 0xff;

    // From now on, we'll scramble the footer as well
    main_payload_size += E_FOOTER_SIZE;

    // Call first scrambler
    if (!fenced_scrambler(main_payload, main_payload_size, seeds, false))
        goto out;

    // Extra scrambling is applied to the end of the file
    seed[0] = SEED_CONSTANT;
    seed[1] = seeds->main[0];
    uint8_t* chunk = &main_payload[main_payload_size - min(main_payload_size, 0x800)];
    if (!bit_scrambler(chunk, min(main_payload_size, 0x800), seed, 0x100, false))
        goto out;

    // Populate the header data
    setbe32(buf, 0x02);
    setbe32(&buf[4], working_size);

    if (!write_file(buf, main_payload_size + E_HEADER_SIZE, path, true))
        goto out;

    r = true;

out:
    free(buf);
    return r;
}

static uint32_t unscramble(uint8_t* payload, uint32_t payload_size, seed_data* seeds,
                           uint32_t* required_size)
{
    uint32_t type = getbe32(payload);
    if (type != 2) {
        fprintf(stderr, "ERROR: Invalid type: 0x%08x\n", type);
        return 0;
    }
    *required_size = getbe32(&payload[4]);
    payload = &payload[E_HEADER_SIZE];
    payload_size -= E_HEADER_SIZE;

    // Revert the bit scrambling that was applied to the end of the file
    uint32_t seed[2] = { SEED_CONSTANT, seeds->main[0] };
    uint8_t* chunk = &payload[payload_size - min(payload_size, 0x800)];
    if (!bit_scrambler(chunk, min(payload_size, 0x800), seed, 0x100, true))
        return 0;

    // Now call the fenced scrambler on the whole payload
    if (!fenced_scrambler(payload, payload_size, seeds, true))
        return 0;

    // Read the descrambled checksums footer (16 bytes)
    uint32_t* footer = (uint32_t*)&payload[payload_size - E_FOOTER_SIZE];
    payload_size -= E_FOOTER_SIZE;
    if (footer[0] != 0) {
        fprintf(stderr, "ERROR: unexpected footer value: 0x%08x\n", footer[0]);
        return 0;
    }
    // The 3rd checksum is probably leftover from the compression algorithm used
#if defined(VALIDATE_CHECKSUM)
    printf("3rd checksum = 0x%04x\n", getbe32(&footer[3]));
#endif
    uint32_t checksum[3] = { 0 - getbe32(&footer[1]), getbe32(&footer[2]), getbe32(&footer[3]) };

    // Look for the bitstream_end marker and adjust our size
    for (; (payload_size > 0) && (payload[payload_size] != 0xff); payload_size--);
    if ((payload_size < sizeof(uint32_t)) || (payload[payload_size] != 0xff)) {
        fprintf(stderr, "ERROR: End marker was not found\n");
        return 0;
    }
    payload[payload_size] = 0x00;

    // Now call the rotating scrambler on the actual payload
    if (!rotating_scrambler(payload, payload_size, seeds, checksum[2]))
        return 0;

    // Validate the checksums
    for (uint32_t i = 0; i < (payload_size & ~3); i += sizeof(uint32_t)) {
        checksum[0] -= getbe32(&payload[i]);
        checksum[1] ^= ~getbe32(&payload[i]);
    }
    if ((checksum[0] != 0) || (checksum[1] != 0)) {
        fprintf(stderr, "ERROR: Descrambler 2 checksum mismatch\n");
        return 0;
    }

    // Zero 16 bytes from the end marker position
    for (uint32_t i = 0; i < E_FOOTER_SIZE; i++)
        payload[payload_size + i] = 0;

    // Finally revert the additional bit scrambling applied to the start of the file
    seed[0] = checksum[2] + SEED_CONSTANT;
    seed[1] = seeds->main[2];
    if (!bit_scrambler(payload, min(payload_size, 0x800), seed, 0x80, true))
        return 0;

    return payload_size;
}

int main(int argc, char** argv)
{
    seed_data seeds;
    char path[256];
    uint32_t src_size, dst_size;
    uint8_t *src = NULL, *dst = NULL;
    int r = -1;
    const char* app_name = basename(argv[0]);
    if ((argc < 2) || ((argc == 3) && (*argv[1] != '-'))) {
        printf("%s (c) 2019 VitaSmith\n\nUsage: %s [-GAME_ID] <file.e>\n\n"
            "Encode or decode a Gust .e file using the seeds for GAME_ID.\n", app_name, app_name);
        return 0;
    }

    // Populate the descrambling seeds from the JSON file
    snprintf(path, sizeof(path), "%s.json", app_name);
    JSON_Value* json_val = json_parse_file_with_comments(path);
    if (json_val == NULL) {
        fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
        goto out;
    }
    const char* seeds_id = (argc == 3) ? &argv[1][1] : json_object_get_string(json_object(json_val), "seeds_id");
    JSON_Array* seeds_array = json_object_get_array(json_object(json_val), "seeds");
    JSON_Object* seeds_entry = NULL;
    for (size_t i = 0; i < json_array_get_count(seeds_array); i++) {
        seeds_entry = json_array_get_object(seeds_array, i);
        if (strcmp(seeds_id, json_object_get_string(seeds_entry, "id")) == 0)
            break;
        seeds_entry = NULL;
    }
    if (seeds_entry == NULL) {
        fprintf(stderr, "ERROR: Can't find the seeds for \"%s\" in '%s'\n", seeds_id, path);
        json_value_free(json_val);
        goto out;
    }

    printf("Using the scrambling seeds for %s", json_object_get_string(seeds_entry, "name"));
    if (argc < 3)
        printf(" (edit '%s' to change)\n", path);
    else
        printf("\n");

    for (size_t i = 0; i < array_size(seeds.main); i++) {
        seeds.main[i] = (uint32_t)json_array_get_number(json_object_get_array(seeds_entry, "main"), i);
        seeds.table[i] = (uint32_t)json_array_get_number(json_object_get_array(seeds_entry, "table"), i);
        seeds.length[i] = (uint32_t)json_array_get_number(json_object_get_array(seeds_entry, "length"), i);
    }
    seeds.fence = (uint32_t)json_object_get_number(seeds_entry, "fence");
    json_value_free(json_val);

    // Read the source file
    src_size = read_file(argv[argc - 1], &src);
    if (src_size == 0) {
        fprintf(stderr, "ERROR: Can't read source xml\n");
        goto out;
    }

    char* e_pos = strstr(argv[argc - 1], ".e");
    if (e_pos == NULL) {
        // Compress and scramble a file
        dst_size = glaze(src, src_size, &dst);
        if (dst_size == 0)
            goto out;

#if defined(CREATE_EXTRA_FILES)
        snprintf(path, sizeof(path), "%s.glaze", argv[argc - 1]);
        write_file(dst, dst_size, path, false);
#endif

#if defined(VALIDATE_CHECKSUM)
        printf("UnGlaze: 0x%04x, src_size = 0x%04x\n", unglaze(dst, dst_size, src, src_size), src_size);
#endif

        // Scramble the Glaze compressed file
        snprintf(path, sizeof(path), "%s.e", argv[argc - 1]);

        // IMPORTANT: The Atelier executables allocate a working buffer of size 'working_size'
        // for the decoding operation which must be at least the size of the uncompressed data
        // or the size of the compressed stream plus the size of the bytecode table, whichever
        // is largest (because this buffer will be zeroed for the size of the compressed stream
        // plus the size of the bytecode table once decompression is complete).
        uint32_t working_size = max(src_size, dst_size + getbe32(&dst[2 * sizeof(uint32_t)]));
        if (!scramble(dst, dst_size, path, &seeds, working_size))
            goto out;

        r = 0;
    } else {
        // Decode a file
        if (((src_size % 4) != 0) || (src_size <= E_HEADER_SIZE + E_FOOTER_SIZE)) {
            fprintf(stderr, "ERROR: Invalid file size\n");
            goto out;
        }

        // Descramble the data
        uint32_t payload_size = unscramble(src, src_size, &seeds, &dst_size);
        if ((payload_size == 0) || (dst_size == 0))
            goto out;

#if defined(CREATE_EXTRA_FILES)
        snprintf(path, sizeof(path), "%s.glaze", argv[argc - 1]);
        write_file(&src[E_HEADER_SIZE], payload_size, path, false);
#endif

#if defined(VALIDATE_CHECKSUM)
        // "We can rebuild (it), we have the technology."
        snprintf(path, sizeof(path), "%s.rebuilt", argv[argc - 1]);
        scramble(&src[E_HEADER_SIZE], payload_size, path, &seeds, dst_size);
#endif

        // Uncompress descrambled data
        dst = malloc(dst_size);
        if (dst == NULL)
            goto out;
        if (unglaze(&src[E_HEADER_SIZE], payload_size, dst, dst_size) == 0) {
            fprintf(stderr, "ERROR: Can't decompress file\n");
            goto out;
        }

        *e_pos = 0;
        if (!write_file(dst, dst_size, argv[argc - 1], true))
            goto out;
        r = 0;
    }

    // What a wild ride it has been to get there...
    // Thank you Gust, for making the cracking of your "encryption"
    // even more interesting than playing your games! :)))

out:
    free(dst);
    free(src);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        getchar();
    }

    return r;
}
