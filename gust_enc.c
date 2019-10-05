/*
  gust_enc - Decoder for Gust (Koei/Tecmo) .e files
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
static uint8_t* build_code_table(uint8_t* bitstream, uint32_t bitstream_length, uint32_t* code_table_length)
{
    *code_table_length = getbe32(bitstream);
    uint8_t* code_table = malloc(*code_table_length);
    if (code_table == NULL)
        return NULL;
    getbits_ctx ctx = { 0 };
    ctx.buffer = &bitstream[sizeof(uint32_t)];
    ctx.size = bitstream_length - sizeof(uint32_t);

    for (uint32_t c = getbits(&ctx, 1), i = 0; i < *code_table_length; c = getbits(&ctx, 1), i++) {
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

// Uncompress a glaze compressed file
static uint32_t unglaze(uint8_t* src, uint32_t src_length, uint8_t* dst, uint32_t dst_length)
{
    uint32_t dec_length = getbe32(src);
    src = &src[sizeof(uint32_t)];
    if (dec_length != dst_length) {
        fprintf(stderr, "ERROR: Glaze decompression size mismatch\n");
        return dec_length;
    }

    uint32_t bitstream_length = getbe32(src);
    if (bitstream_length <= sizeof(uint32_t)) {
        fprintf(stderr, "ERROR: Glaze decompression bitstream is too small\n");
        return 0;
    }
    src = &src[sizeof(uint32_t)];
    uint32_t chk_length = bitstream_length + sizeof(uint32_t);
    if (chk_length >= src_length) {
        fprintf(stderr, "ERROR: Glaze decompression bitstream is too large\n");
        return 0;
    }

    uint32_t code_len;
    uint8_t* code_table = build_code_table(src, bitstream_length, &code_len);
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
            for (int i = l; i > 0; i--)
                *dst++ = *dict++;
            break;
        case 0x07:  // 1-byte code + 1 byte from length table
            // Copy l + 14 bytes from the source where l is provided by the (separate) length table
            for (int i = *len++ + 14; i > 0; i--)
                *dst++ = *dict++;
            break;
        }
    }

    free(code_table);
    return dec_length;
}

static bool rescramble(uint8_t* payload, uint32_t payload_size, char* path, seed_data* seeds,
                       uint32_t required_size, uint32_t payload_checksum)
{
    bool r = false;

    // Align the size (plus an extra byte for the end marker) to 16-bytes
    uint32_t main_payload_size = (payload_size + 1 + 0xf) & ~0xf;
    uint8_t* buf = calloc((size_t)main_payload_size + E_HEADER_SIZE + E_FOOTER_SIZE, 1);
    if (buf == NULL)
        return false;
    uint8_t* main_payload = &buf[E_HEADER_SIZE];
    memcpy(main_payload, payload, payload_size);

    // Scramble the beginning of the file
    uint32_t seed[2] = { payload_checksum + SEED_CONSTANT, seeds->main[2] };
    if (!bit_scrambler(main_payload, min(payload_size, 0x800), seed, 0x80, false))
        goto out;

    // Compute and write the footer checksums
    uint32_t checksum[2] = { 0, 0 };
    for (uint32_t i = 0; i < (payload_size & ~3); i += sizeof(uint32_t)) {
        checksum[0] -= getbe32(&main_payload[i]);
        checksum[1] ^= ~getbe32(&main_payload[i]);
    }
    setbe32(&main_payload[(size_t)main_payload_size + 4], checksum[0]);
    setbe32(&main_payload[(size_t)main_payload_size + 8], checksum[1]);
    setbe32(&main_payload[(size_t)main_payload_size + 12], payload_checksum);

    // Call the main scrambler
    if (!rotating_scrambler(main_payload, payload_size, seeds, payload_checksum))
        goto out;

    // Add the end of stream marker
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
    setbe32(&buf[4], required_size);

    if (!write_file(buf, main_payload_size + E_HEADER_SIZE, path))
        goto out;

    r = true;

out:
    free(buf);
    return r;
}

int main(int argc, char** argv)
{
    seed_data seeds;
    char path[256];
    uint8_t *buf = NULL, *dec = NULL;
    int r = -1;
    const char* app_name = basename(argv[0]);
    if ((argc < 2) || ((argc == 3) && (*argv[1] != '-'))) {
        printf("%s (c) 2019 VitaSmith\n\nUsage: %s [-GAME_ID] <file.e>\n\n"
            "Descramble and decompress a Gust .e file using the seeds for GAME_ID.\n", app_name, app_name);
        return 0;
    }

    // Populate the descrambling seeds from the JSON file
    snprintf(path, sizeof(path), "%s.json", app_name);
    JSON_Value* json_val = json_parse_file_with_comments(path);
    if (json_val == NULL) {
        fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
        return -1;
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
        return -1;
    }

    printf("Using the descrambling seeds for %s", json_object_get_string(seeds_entry, "name"));
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

    // Don't bother checking for case or if these extensions are really at the bitstream_end
    char* e_pos = strstr(argv[argc - 1], ".e");
    if (e_pos == NULL) {
        fprintf(stderr, "ERROR: File should have a '.e' extension\n");
        return -1;
    }

    FILE* src_file = fopen(argv[argc - 1], "rb");
    if (src_file == NULL) {
        fprintf(stderr, "ERROR: Can't open file '%s'", argv[argc - 1]);
        return -1;
    }

    fseek(src_file, 0L, SEEK_END);
    uint32_t stream_size = (uint32_t)ftell(src_file);
    fseek(src_file, 0L, SEEK_SET);

    if (((stream_size % 4) != 0) || (stream_size <= E_HEADER_SIZE + E_FOOTER_SIZE)) {
        fprintf(stderr, "ERROR: Invalid file size");
        goto out;
    }

    buf = malloc(stream_size);
    if (buf == NULL)
        goto out;
    if (fread(buf, 1, stream_size, src_file) != stream_size) {
        fprintf(stderr, "ERROR: Can't read file");
        goto out;
    }

    uint8_t* stream = buf;
    uint32_t type = getbe32(stream);
    if (type != 2) {
        fprintf(stderr, "ERROR: Invalid type: 0x%08x\n", type);
        goto out;
    }
    uint32_t dec_size = getbe32(&stream[4]);
    stream = &stream[E_HEADER_SIZE];
    stream_size -= E_HEADER_SIZE;

    dec = malloc(dec_size);
    if (dec == NULL)
        goto out;

    // Revert the bit scrambling that was applied to the end of the file
    uint32_t seed[2] = { SEED_CONSTANT, seeds.main[0] };
    uint8_t* chunk = &stream[stream_size - min(stream_size, 0x800)];
    if (!bit_scrambler(chunk, min(stream_size, 0x800), seed, 0x100, true))
        goto out;

    // Now call the fenced scrambler on the whole file
    if (!fenced_scrambler(stream, stream_size, &seeds, true))
        goto out;

    // Read the descrambled checksums footer (16 bytes)
    uint32_t* footer = (uint32_t*)&stream[stream_size - E_FOOTER_SIZE];
    stream_size -= E_FOOTER_SIZE;
    if (footer[0] != 0) {
        fprintf(stderr, "ERROR: unexpected footer value: 0x%08x\n", footer[0]);
        goto out;
    }
    // The 3rd checksum is probably leftover from the compression algorithm used
    uint32_t checksum[3] = { 0 - getbe32(&footer[1]), getbe32(&footer[2]), getbe32(&footer[3]) };

    // Look for the bitstream_end marker and adjust our size
    for (; (stream_size > 0) && (stream[stream_size] != 0xff); stream_size--);
    if ((stream_size < sizeof(uint32_t)) || (stream[stream_size] != 0xff)) {
        fprintf(stderr, "ERROR: End marker was not found\n");
        goto out;
    }
    stream[stream_size] = 0x00;

    // Now call the rotating scrambler on the actual payload
    if (!rotating_scrambler(stream, stream_size, &seeds, checksum[2]))
        goto out;

    uint32_t old_stream_size = stream_size;
    // Validate the checksums
    stream_size &= ~3;
    for (uint32_t i = 0; i < stream_size; i += sizeof(uint32_t)) {
        checksum[0] -= getbe32(&stream[i]);
        checksum[1] ^= ~getbe32(&stream[i]);
    }
    if ((checksum[0] != 0) || (checksum[1] != 0)) {
        fprintf(stderr, "ERROR: Descrambler 2 checksum mismatch\n");
        return false;
    }

    // Finally revert the additional bit scrambling applied to the start of the file
    seed[0] = checksum[2] + SEED_CONSTANT;
    seed[1] = seeds.main[2];
    if (!bit_scrambler(stream, min(stream_size, 0x800), seed, 0x80, true))
        goto out;

    // "We can rebuild (it), we have the technology."
    snprintf(path, sizeof(path), "%s.rebuilt", argv[argc - 1]);
    rescramble(stream, old_stream_size, path, &seeds, dec_size, checksum[2]);

    // Uncompress descrambled data
    if (unglaze(stream, stream_size, dec, dec_size) != dec_size) {
        fprintf(stderr, "ERROR: Can't decompress file");
        goto out;
    }

    snprintf(path, sizeof(path), "%s.xml", argv[argc - 1]);
    if (!write_file(dec, dec_size, path))
        goto out;

    // What a wild ride it has been to get there...
    // Thank you Gust, for making the cracking of your "encryption"
    // even more interesting than playing your games! :)))
    r = 0;

out:
    free(dec);
    free(buf);
    fclose(src_file);

    return r;
}
