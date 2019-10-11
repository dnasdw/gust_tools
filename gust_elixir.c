/*
  gust_elixir - Archive unpacker for Gust (Koei/Tecmo) .elixir[.gz] files
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "util.h"
#include "parson.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC
#include "miniz_tinfl.h"
#include "miniz_tdef.h"

#define EARC_MAGIC              0x45415243  // "EARC"
#define DEFAULT_CHUNK_SIZE      0x4000

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    uint32_t header_size;
    uint32_t table_size;
    uint32_t nb_files;
    uint32_t flags;             // Can be 0x0 or 0xA
} lxr_header;

typedef struct {
    uint32_t offset;
    uint32_t size;
    char     filename[0x30];
} lxr_entry;
#pragma pack(pop)

int main(int argc, char** argv)
{
    int r = -1;
    char path[256];
    uint8_t *buf = NULL, *zbuf = NULL;
    uint32_t zsize;
    FILE *file = NULL, *dst = NULL;
    JSON_Value* json = NULL;
    tdefl_compressor* compressor = NULL;

    if (argc != 2) {
        printf("%s %s (c) 2019 VitaSmith\n\n"
            "Usage: %s <elixir[.gz]> file>\n\n"
            "Extracts (file) or recreates (directory) a Gust .elixir archive.\n\n"
            "This application will also create a backup (.bak) of the original, when the target\n"
            "is being overwritten for the first time.\n",
            basename(argv[0]), GUST_TOOLS_VERSION_STR, basename(argv[0]));
        return 0;
    }

    if (is_directory(argv[1])) {
        snprintf(path, sizeof(path), "%s%celixir.json", argv[1], PATH_SEP);
        if (!is_file(path)) {
            fprintf(stderr, "ERROR: '%s' does not exist\n", path);
            goto out;
        }
        json = json_parse_file_with_comments(path);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        if (filename == NULL)
            goto out;
        printf("Creating '%s'...\n", filename);
        create_backup(filename);
        // Work with a temporary file if we're going to compress it
        if (json_object_get_boolean(json_object(json), "compressed"))
            snprintf(path, sizeof(path), "%s.tmp", filename);
        else
            strncpy(path, filename, sizeof(path));
        path[sizeof(path) - 1] = 0;
        file = fopen(path, "wb+");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
            goto out;
        }
        lxr_header hdr = { 0 };
        hdr.magic = EARC_MAGIC;
        hdr.version = (uint32_t)json_object_get_number(json_object(json), "version");
        hdr.nb_files = (uint32_t)json_object_get_number(json_object(json), "nb_files");
        hdr.flags = (uint32_t)json_object_get_number(json_object(json), "flags");
        hdr.header_size = (uint32_t)json_object_get_number(json_object(json), "header_size");
        hdr.table_size = (uint32_t)json_object_get_number(json_object(json), "table_size");
        if (fwrite(&hdr, sizeof(hdr), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write header\n");
            goto out;
        }
        if (hdr.nb_files * sizeof(lxr_entry) != hdr.table_size) {
            fprintf(stderr, "ERROR: Unexpected size for offset table\n");
            goto out;
        }
        JSON_Array* json_files_array = json_object_get_array(json_object(json), "files");
        if (json_array_get_count(json_files_array) != hdr.nb_files) {
            fprintf(stderr, "ERROR: number of files doesn't match header\n");
            goto out;
        }

        lxr_entry* table = (lxr_entry*)calloc(hdr.nb_files, sizeof(lxr_entry));
        // Allocate the space in file - we'll update it later on
        if (fwrite(table, sizeof(lxr_entry), hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't write header table\n");
            free(table);
            goto out;
        }
        printf("OFFSET   SIZE     NAME\n");
        for (uint32_t i = 0; i < hdr.nb_files; i++) {
            table[i].offset = ftell(file);
            snprintf(path, sizeof(path), "%s%c%s", argv[1], PATH_SEP, json_array_get_string(json_files_array, i));
            table[i].size = read_file(path, &buf);
            if (table[i].size == 0) {
                free(table);
                goto out;
            }
            strncpy(table[i].filename, json_array_get_string(json_files_array, i), sizeof(table[i].filename));
            printf("%08x %08x %s\n", table[i].offset, table[i].size, path);
            if (fwrite(buf, 1, table[i].size, file) != table[i].size) {
                fprintf(stderr, "ERROR: Can't add file data\n");
                free(table);
                goto out;
            }
            free(buf);
            buf = NULL;
        }
        hdr.payload_size = ftell(file) - hdr.header_size - hdr.table_size;
        fseek(file, 2 * sizeof(uint32_t), SEEK_SET);
        if (fwrite(&hdr.payload_size, sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't update file size\n");
            free(table);
            goto out;
        }
        fseek(file, hdr.header_size, SEEK_SET);
        if (fwrite(table, sizeof(lxr_entry), hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't write header table\n");
            free(table);
            goto out;
        }
        free(table);

        if (json_object_get_boolean(json_object(json), "compressed")) {
            printf("Compressing...\n");
            compressor = calloc(1, sizeof(tdefl_compressor));
            if (compressor == NULL)
                goto out;
            dst = fopen(filename, "wb");
            if (dst == NULL) {
                fprintf(stderr, "ERROR: Can't create compressed file\n");
                goto out;
            }
            fseek(file, 0, SEEK_SET);
            buf = malloc(DEFAULT_CHUNK_SIZE);
            zbuf = malloc(DEFAULT_CHUNK_SIZE);
            while (1) {
                size_t written = DEFAULT_CHUNK_SIZE;
                size_t read = fread(buf, 1, DEFAULT_CHUNK_SIZE, file);
                if (read == 0)
                    break;
                tdefl_status status = tdefl_init(compressor, NULL, NULL,
                    TDEFL_WRITE_ZLIB_HEADER | TDEFL_COMPUTE_ADLER32 | 256);
                if (status != TDEFL_STATUS_OKAY) {
                    fprintf(stderr, "ERROR: Can't init compressor\n");
                    goto out;
                }
                status = tdefl_compress(compressor, buf, &read, zbuf, &written, TDEFL_FINISH);
                if (status != TDEFL_STATUS_DONE) {
                    fprintf(stderr, "ERROR: Can't compress data\n");
                    goto out;
                }
                if (fwrite(&written, sizeof(uint32_t), 1, dst) != 1) {
                    fprintf(stderr, "ERROR: Can't write compressed stream size\n");
                    goto out;
                }
                if (fwrite(zbuf, 1, written, dst) != written) {
                    fprintf(stderr, "ERROR: Can't write compressed data\n");
                    goto out;
                }
            }
            uint32_t end_marker = 0;
            if (fwrite(&end_marker, sizeof(uint32_t), 1, dst) != 1) {
                fprintf(stderr, "ERROR: Can't write end marker\n");
                goto out;
            }
            fclose(file);
            file = NULL;
            snprintf(path, sizeof(path), "%s.tmp", filename);
            _unlink(path);
        }

        r = 0;
    } else {
        printf("Extracting '%s'...\n", argv[1]);
        char* elixir_pos = strstr(argv[1], ".elixir");
        if (elixir_pos == NULL) {
            fprintf(stderr, "ERROR: File should have a '.elixir[.gz]' extension\n");
            goto out;
        }
        char* gz_pos = strstr(argv[1], ".gz");

        file = fopen(argv[1], "rb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't open elixir file '%s'", argv[1]);
            goto out;
        }

        // Some elixir.gz files are actually uncompressed versions
        if (fread(&zsize, sizeof(zsize), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't read from elixir file '%s'", argv[1]);
            goto out;
        }
        if ((zsize == EARC_MAGIC) && (gz_pos != NULL))
            gz_pos = NULL;

        fseek(file, 0L, SEEK_END);
        size_t file_size = ftell(file);
        fseek(file, 0L, SEEK_SET);

        if (gz_pos != NULL) {
            file_size *= 2;
            buf = malloc(file_size);
            if (buf == NULL)
                goto out;
            size_t pos = 0;
            while (1) {
                uint32_t file_pos = (uint32_t)ftell(file);
                if (fread(&zsize, sizeof(uint32_t), 1, file) != 1) {
                    fprintf(stderr, "ERROR: Can't read compressed stream size at position %08x\n", file_pos);
                    goto out;
                }
                if (zsize == 0)
                    break;
                zbuf = malloc(zsize);
                if (zbuf == NULL)
                    goto out;
                if (fread(zbuf, 1, zsize, file) != zsize) {
                    fprintf(stderr, "ERROR: Can't read compressed stream at position %08x\n", file_pos);
                    goto out;
                }
                // Elixirs are inflated using a constant chunk size which simplifies overflow handling
                if (pos + DEFAULT_CHUNK_SIZE > file_size) {
                    file_size *= 2;
                    uint8_t* old_buf = buf;
                    buf = realloc(buf, file_size);
                    if (buf == NULL) {
                        fprintf(stderr, "ERROR: Can't increase buffer size\n");
                        buf = old_buf;
                        goto out;
                    }
                }
                size_t s = tinfl_decompress_mem_to_mem(&buf[pos], file_size - pos, zbuf, zsize,
                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32);
                if ((s == 0) || (s == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)) {
                    fprintf(stderr, "ERROR: Can't decompress stream at position %08x\n", file_pos);
                    goto out;
                }
                pos += s;
            } while (zsize != 0);
            file_size = pos;

//#define DECOMPRESS_ONLY
#ifdef DECOMPRESS_ONLY
            FILE* dst = NULL;
            *gz_pos = 0;
            dst = fopen(argv[1], "wb");
            if (dst == NULL) {
                fprintf(stderr, "ERROR: Can't create file '%s'\n", argv[1]);
                goto out;
            }
            if (fwrite(buf, 1, file_size, dst) != file_size) {
                fprintf(stderr, "ERROR: Can't write file '%s'\n", argv[1]);
                fclose(dst);
                goto out;
            }
            printf("%08x %s\n", (uint32_t)file_size, argv[1]);
            fclose(dst);
            r = 0;
            goto out;
#endif
        } else {
            buf = malloc(file_size);
            if (buf == NULL)
                goto out;
            if (fread(buf, 1, file_size, file) != file_size) {
                fprintf(stderr, "ERROR: Can't read uncompressed data");
                goto out;
            }
        }

        // Now that we have an uncompressed .elixir file, extract the files
        json = json_value_init_object();
        json_object_set_string(json_object(json), "name", argv[1]);
        json_object_set_boolean(json_object(json), "compressed", (gz_pos != NULL));

        *elixir_pos = 0;
        if (!create_path(argv[1]))
            goto out;

        lxr_header* hdr = (lxr_header*)buf;
        if (hdr->magic != EARC_MAGIC) {
            fprintf(stderr, "ERROR: Not an elixir file (bad magic)\n");
            goto out;
        }
        if (hdr->version != 1) {
            fprintf(stderr, "ERROR: Invalid elixir version (0x%08X)\n", hdr->version);
            goto out;
        }
        json_object_set_number(json_object(json), "version", hdr->version);
        json_object_set_number(json_object(json), "flags", hdr->flags);
        // If we find files with different additional files or name sizes
        // the following may become important to have stored
        json_object_set_number(json_object(json), "header_size", hdr->header_size);
        json_object_set_number(json_object(json), "table_size", hdr->table_size);

        if (sizeof(lxr_header) + hdr->nb_files * sizeof(lxr_entry) + hdr->payload_size != file_size) {
            fprintf(stderr, "ERROR: File size mismatch\n");
            goto out;
        }
        json_object_set_number(json_object(json), "nb_files", hdr->nb_files);

        JSON_Value* json_files_array = json_value_init_array();
        printf("OFFSET   SIZE     NAME\n");
        for (uint32_t i = 0; i < hdr->nb_files; i++) {
            lxr_entry* entry = (lxr_entry*)&buf[sizeof(lxr_header) + i * sizeof(lxr_entry)];
            assert(entry->offset + entry->size <= (uint32_t)file_size);
            // Ignore "dummy" entries
            if ((entry->size == 0) && (strcmp(entry->filename, "dummy") == 0))
                continue;
            json_array_append_string(json_array(json_files_array), entry->filename);
            snprintf(path, sizeof(path), "%s%c%s", argv[1], PATH_SEP, entry->filename);
            if (!write_file(&buf[entry->offset], entry->size, path, false))
                goto out;
            printf("%08x %08x %s\n", entry->offset, entry->size, path);
        }

        json_object_set_value(json_object(json), "files", json_files_array);
        snprintf(path, sizeof(path), "%s%celixir.json", argv[1], PATH_SEP);
        json_serialize_to_file_pretty(json, path);

        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(zbuf);
    free(compressor);
    if (file != NULL)
        fclose(file);
    if (dst != NULL)
        fclose(dst);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

#ifdef _CRTDBG_MAP_ALLOC
    _CrtDumpMemoryLeaks();
#endif
    return r;
}
