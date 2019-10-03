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

  Kind regards,

  -- VitaSmith, 2019-10-02

 */

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"

#define SEED_CONSTANT       0x3b9a73c9
#define SEED_INCREMENT      0x2f09
#define SEED_FENCE          0x1532

// Stupid sexy scrambler ("Feels like I'm reading nothing at all!")
bool descramble_chunk(uint8_t* chunk, uint32_t chunk_size, uint32_t seed[2], uint16_t slice_size)
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
        uint8_t p0, p1, b0, b1, v0, v1;
        for (uint32_t i = 0; i < min(table_size, chunk_size << 3); i += 2) {
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

bool descrambler1(uint8_t* buf, uint32_t buf_size)
{
    uint32_t chunk_size = min(buf_size, 0x800);

    // Extra scrambling is applied to the end of the file
    uint8_t* chunk = &buf[buf_size - chunk_size];

    // TODO: check this constant with other games
    uint32_t seed[2] = { SEED_CONSTANT, 0x6e45 };
    if (!descramble_chunk(chunk, chunk_size, seed, 0x100))
        return false;

    // TODO: check this constant with other games
    seed[1] = 0xc9af;
    for (uint32_t i = 0; i < buf_size; i += 2) {
        seed[1] = seed[0] * seed[1] + SEED_INCREMENT;
        uint32_t x = (seed[1] >> 16) & 0x7fff;
        uint16_t w = getbe16(&buf[i]);
        if (x % SEED_FENCE >= SEED_FENCE / 2)
            w ^= (uint16_t)x;
        w -= (uint16_t)x;
        buf[i] = (uint8_t)(w >> 8);
        buf[i + 1] = (uint8_t)w;
    }

    return true;
}

bool descrambler2(uint8_t* buf, uint32_t buf_size)
{
    // This part of the code uses a table with 3 seed values + 3 seed switches
    // These seeds are different for each game (these ones are from Atelier Sophie)
    uint32_t seed_table[3] = { 0xa9d9, 0xae8f, 0x89f5 };
    uint32_t seed_switch[3] = { 0x1d, 0x13, 0x0b };
    uint32_t seed[2];

    if ((buf_size % 4 != 0) || (buf_size < 4 * sizeof(uint32_t))) {
        fprintf(stderr, "ERROR: Invalid descrambler 2 buffer size 0x%04x\n", buf_size);
        return false;
    }

    buf_size -= sizeof(uint32_t);
    // Generate a seed from the last 32-bit word from the buffer
    seed[0] = getbe32(&buf[buf_size]) + SEED_CONSTANT;
    buf_size -= sizeof(uint32_t);
    uint32_t file_checksum[2] = { 0, 0 };
    file_checksum[0] = getbe32(&buf[buf_size]);
    buf_size -= sizeof(uint32_t);
    file_checksum[1] = getbe32(&buf[buf_size--]);

    // Look for the bitstream_end marker
    for ( ; (buf_size > 0) && (buf[buf_size] != 0xff); buf_size--);

    if ((buf_size < sizeof(uint32_t)) || (buf[buf_size] != 0xff)) {
        fprintf(stderr, "ERROR: Descrambler 2 end marker was not found\n");
        return false;
    }

    uint32_t seed_index = 0;
    uint32_t seed_switch_fudge = 0;
    uint32_t processed_for_this_seed = 0;
    seed[1] = seed_table[seed_index];
    for (uint32_t i = 0; i < buf_size; i++) {
        seed[1] = seed[0] * seed[1] + SEED_INCREMENT;
        buf[i] ^= seed[1] >> 16;
        if (++processed_for_this_seed >= seed_switch[seed_index] + seed_switch_fudge) {
            seed_table[seed_index++] = seed[1];
            if (seed_index >= array_size(seed_table)) {
                seed_index = 0;
                seed_switch_fudge++;
            }
            seed[1] = seed_table[seed_index];
            processed_for_this_seed = 0;
        }
    }

    buf[buf_size] = 0;
    buf_size &= ~3;
    uint32_t computed_checksum[2] = { 0, 0 };
    for (uint32_t i = 0; i < buf_size; i += sizeof(uint32_t)) {
        computed_checksum[0] ^= ~getbe32(&buf[i]);
        computed_checksum[1] -= getbe32(&buf[i]);
    }
    if ((computed_checksum[0] != file_checksum[0]) || (computed_checksum[1] != file_checksum[1])) {
        fprintf(stderr, "ERROR: Descrambler 2 checksum mismatch\n");
        return false;
    }

    // TODO: check this constant with other games
    seed[1] = 0x7525;
    // Now descramble some more
    descramble_chunk(buf, min(buf_size, 0x800), seed, 0x80);

    return true;
}

/*
  The following 3 functions deal with the compression algorithm used by Gust, which
  I am going to call 'Glaze', for "Gust Lempel–Ziv".
  After much research, I still haven't figured out where Gust derived that one from.
  All I can say is that it seems to be LZ based with code, dictionary and length tables.
 */

// Read code_len bits from bitstream and emit a bytecode
uint8_t get_code_byte(uint8_t** psrc, int* src_bit_pos, int code_len)
{
    uint8_t code = 0;

    while (code_len > -1) {
        code |= (uint8_t)(((1 << *src_bit_pos) & **psrc) >> *src_bit_pos) << code_len;
        code_len--;
        if (*src_bit_pos == 0) {
            *src_bit_pos = 7;
            (*psrc)++;
         } else {
            (*src_bit_pos)--;
        }
    }
    return code;
}

// Boy with extended open hand, looking at butterfly: "Is this Huffman encoding?"
uint8_t* build_code_table(uint8_t* bitstream, uint32_t bitstream_length, uint32_t* code_table_length)
{
    *code_table_length = getbe32(bitstream);
    uint8_t* code_table = malloc(*code_table_length);
    if (code_table == NULL)
        return NULL;
    uint8_t* code = code_table;
    uint8_t* bitstream_end = &bitstream[bitstream_length];
    uint8_t* code_end = &code_table[*code_table_length];

    int bit_pos = 7;    // bit being processed in source
    bitstream = &bitstream[sizeof(uint32_t)];
    do {
        if ((*bitstream & (1 << bit_pos)) == 0) {
            // Bit sequence starts with 0 -> get the length of code to read
            int code_len = 0;
            do {
                if (bit_pos-- == 0) {
                    bit_pos = 7;
                    bitstream++;
                }
            } while ((++code_len < 8) && ((*bitstream & (1 << bit_pos)) == 0));
            if (code_len < 8) {
                // Generate a code byte from the next code_len bits
                *code++ = get_code_byte(&bitstream, &bit_pos, code_len);
            } else {
                // Bit sequence is all zeroes -> emit code 0x00
                *code++ = 0;
            }
        } else {
            // Bit sequence starts with 1 -> emit code 0x01
            *code++ = 1;
            if (bit_pos-- == 0) {
                bit_pos = 7;
                bitstream++;
            }
        }
    } while ((bitstream < bitstream_end) && (code < code_end));

    return code_table;
}

// Uncompress a glaze compressed file
uint32_t unglaze(uint8_t* src, uint32_t src_length, uint8_t* dst, uint32_t dst_length)
{
    uint32_t dec_length = getbe32(src);
    src = &src[sizeof(uint32_t)];
    if (dec_length != dst_length) {
        fprintf(stderr, "ERROR: Glaze decompression size mismatch\n");
        return dec_length;
    }

    uint32_t bitstream_length = getbe32(src);
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

int main(int argc, char** argv)
{
    uint8_t *buf = NULL, *dec = NULL;
    int r = -1;
    const char* app_name = basename(argv[0]);
    if (argc != 2) {
        printf("%s (c) 2019 VitaSmith\n\nUsage: %s <file.e>\n\n"
            "Decrypt and decompress Gust .e file.\n", app_name, app_name);
        return 0;
    }

    // Don't bother checking for case or if these extensions are really at the bitstream_end
    char* e_pos = strstr(argv[1], ".e");
    if (e_pos == NULL) {
        fprintf(stderr, "ERROR: File should have a '.e' extension\n");
        return -1;
    }

    FILE* src_file = fopen(argv[1], "rb");
    if (src_file == NULL) {
        fprintf(stderr, "ERROR: Can't open file '%s'", argv[1]);
        return -1;
    }

    fseek(src_file, 0L, SEEK_END);
    uint32_t src_size = (uint32_t)ftell(src_file);
    fseek(src_file, 0L, SEEK_SET);

    buf = malloc(src_size);
    if (buf == NULL)
        goto out;
    if (fread(buf, 1, src_size, src_file) != src_size) {
        fprintf(stderr, "ERROR: Can't read file");
        goto out;
    }

    uint8_t* stream = buf;
    uint32_t type = getbe32(stream);
    if (type != 2) {
        fprintf(stderr, "ERROR: Invalid type: 0x%08x\n", type);
        goto out;
    }
    stream = &stream[sizeof(uint32_t)];
    uint32_t dec_size = getbe32(stream);
    stream = &stream[3 * sizeof(uint32_t)];
    src_size -= 4 * sizeof(uint32_t);

    dec = malloc(dec_size);
    if (dec == NULL)
        goto out;

    // Call first descrambler
    if (!descrambler1(stream, src_size))
        goto out;

    // Call second descrambler
    if (!descrambler2(stream, src_size))
        goto out;

    // Uncompress descrambled data
    if (unglaze(stream, src_size, dec, dec_size) != dec_size) {
        fprintf(stderr, "ERROR: Can't decompress file");
        goto out;
    }
    FILE* dst_file = NULL;
    char path[256];
    snprintf(path, sizeof(path), "%s.xml", argv[1]);
    dst_file = fopen(path, "wb");
    if (dst_file == NULL) {
        fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
        goto out;
    }
    if (fwrite(dec, 1, dec_size, dst_file) != dec_size) {
        fprintf(stderr, "ERROR: Can't write file '%s'\n", path);
        fclose(dst_file);
        goto out;
    }
    fclose(dst_file);

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
