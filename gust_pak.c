/*
  gust_pak - PAK archive unpacker for Gust (Koei/Tecmo) PC games
  Copyright © 2019-2020 VitaSmith
  Copyright © 2018 Yuri Hime (shizukachan)

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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utf8.h"
#include "util.h"
#include "parson.h"

#pragma pack(push, 1)
typedef struct {
    uint32_t version;
    uint32_t nb_files;
    uint32_t header_size;
    uint32_t flags;
} pak_header;

typedef struct {
    char     filename[128];
    uint32_t size;
    uint8_t  key[20];
    uint32_t data_offset;
    uint32_t flags;
} pak_entry32;

typedef struct {
    char     filename[128];
    uint32_t size;
    uint8_t  key[0x20];
    uint32_t unknown0xa4;
    uint64_t data_offset;
    uint64_t flags;
} pak_entry64;
#pragma pack(pop)

static __inline void decode(uint8_t* a, uint8_t* k, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
        a[i] ^= k[i % 0x20];
}

static char* key_to_string(uint8_t* key)
{
    static char key_string[41];
    for (size_t i = 0; i < 20; i++) {
        key_string[2 * i] = ((key[i] >> 4) < 10) ? '0' + (key[i] >> 4) : 'a' + (key[i] >> 4) - 10;
        key_string[2 * i + 1] = ((key[i] & 0xf) < 10) ? '0' + (key[i] & 0xf) : 'a' + (key[i] & 0xf) - 10;
    }
    key_string[40] = 0;
    return key_string;
}

static uint8_t* string_to_key(const char* str)
{
    static uint8_t key[20];
    for (size_t i = 0; i < 20; i++) {
        key[i] = (str[2 * i] >= 'a') ? str[2 * i] - 'a' + 10 : str[2 * i] - '0';
        key[i] <<= 4;
        key[i] += (str[2 * i + 1] >= 'a') ? str[2 * i + 1] - 'a' + 10 : str[2 * i + 1] - '0';
    }
    return key;
}

// To handle either 32 or 64 bit PAK entries
#define entries32 ((pak_entry32*)entries64)
#define entry(i, m) (is_pak64 ? entries64[i].m :(entries32[i]).m)
#define set_entry(i, m, v) do {if (is_pak64) entries64[i].m = v; else (entries32[i]).m = (uint32_t)(v);} while(0)

int main_utf8(int argc, char** argv)
{
    int r = -1;
    FILE* file = NULL;
    uint8_t* buf = NULL;
    char path[256];
    pak_header hdr = { 0 };
    pak_entry64* entries64 = NULL;
    JSON_Value* json = NULL;
    bool is_pak64 = true;
    bool list_only = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'l');

    if ((argc != 2) && !list_only) {
        printf("%s %s (c) 2018-2020 Yuri Hime & VitaSmith\n\n"
            "Usage: %s [-l] <Gust PAK file>\n\n"
            "Extracts (.pak) or recreates (.json) a Gust .pak archive.\n\n",
            appname(argv[0]), GUST_TOOLS_VERSION_STR, appname(argv[0]));
        return 0;
    }

    if (is_directory(argv[argc - 1])) {
        fprintf(stderr, "ERROR: Directory packing is not supported.\n"
            "To recreate a .pak you need to use the corresponding .json file.\n");
    } else if (strstr(argv[argc - 1], ".json") != NULL) {
        if (list_only) {
            fprintf(stderr, "ERROR: Option -l is not supported when creating an archive\n");
            goto out;
        }
        json = json_parse_file_with_comments(argv[argc - 1]);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", argv[argc - 1]);
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        hdr.header_size = json_object_get_uint32(json_object(json), "header_size");
        if ((filename == NULL) || (hdr.header_size != sizeof(pak_header))) {
            fprintf(stderr, "ERROR: No filename/wrong header size\n");
            goto out;
        }
        hdr.version = json_object_get_uint32(json_object(json), "version");
        hdr.flags = json_object_get_uint32(json_object(json), "flags");
        hdr.nb_files = json_object_get_uint32(json_object(json), "nb_files");
        is_pak64 = json_object_get_boolean(json_object(json), "64-bit");
        printf("Creating '%s'...\n", filename);
        create_backup(filename);
        file = fopen_utf8(filename, "wb+");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't create file '%s'\n", filename);
            goto out;
        }
        if (fwrite(&hdr, sizeof(pak_header), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write PAK header\n");
            goto out;
        }
        entries64 = calloc(hdr.nb_files, sizeof(pak_entry64));
        if (entries64 == NULL) {
            fprintf(stderr, "ERROR: Can't allocate entries\n");
            goto out;
        }
        // Write a dummy table for now
        if (fwrite(entries64, is_pak64 ? sizeof(pak_entry64) : sizeof(pak_entry32),
            hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't write initial PAK table\n");
            goto out;
        }
        uint64_t file_data_offset = ftell64(file);

        JSON_Array* json_files_array = json_object_get_array(json_object(json), "files");
        printf("OFFSET    SIZE     NAME\n");
        for (uint32_t i = 0; i < hdr.nb_files; i++) {
            JSON_Object* file_entry = json_array_get_object(json_files_array, i);
            uint8_t* key = string_to_key(json_object_get_string(file_entry, "key"));
            filename = json_object_get_string(file_entry, "name");
            strncpy(entry(i, filename), filename, 127);
            strncpy(path, filename, sizeof(path) - 1);
            for (size_t n = 0; n < strlen(path); n++) {
                if (path[n] == '\\')
                    path[n] = PATH_SEP;
            }
            set_entry(i, size, read_file(&path[1], &buf));
            if (entry(i, size) == 0) {
                fprintf(stderr, "ERROR: Can't read from '%s'\n", path);
                goto out;
            }
            bool skip_encode = true;
            for (int j = 0; j < 20; j++) {
                entry(i, key)[j] = key[j];
                if (key[j] != 0)
                    skip_encode = false;
            }

            set_entry(i, data_offset, ftell64(file) - file_data_offset);
            uint64_t flags = json_object_get_uint64(file_entry, "flags");
            if (is_pak64)
                setbe64(&(entries64[i].flags), flags);
            else
                setbe32(&(entries32[i].flags), (uint32_t)flags);
            printf("%09" PRIx64 " %08x %s%c\n", entry(i, data_offset) + file_data_offset,
                entry(i, size), entry(i, filename), skip_encode ? '*' : ' ');
            if (!skip_encode) {
                decode((uint8_t*)entry(i, filename), entry(i, key), 128);
                decode(buf, entry(i, key), entry(i, size));
            }
            if (fwrite(buf, 1, entry(i, size), file) != entry(i, size)) {
                fprintf(stderr, "ERROR: Can't write data for '%s'\n", path);
                goto out;
            }
            free(buf);
            buf = NULL;
        }
        fseek64(file, sizeof(pak_header), SEEK_SET);
        if (fwrite(entries64, is_pak64 ? sizeof(pak_entry64) : sizeof(pak_entry32),
            hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't write PAK table\n");
            goto out;
        }
        r = 0;
    } else {
        printf("%s '%s'...\n", list_only ? "Listing" : "Extracting", basename(argv[argc - 1]));
        file = fopen_utf8(argv[argc - 1], "rb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't open PAK file '%s'", argv[argc - 1]);
            goto out;
        }

        if (fread(&hdr, sizeof(hdr), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't read hdr");
            goto out;
        }

        if ((hdr.version != 0x20000) || (hdr.header_size != sizeof(pak_header))) {
            fprintf(stderr, "ERROR: Signature doesn't match expected PAK file format.\n");
            goto out;
        }
        if (hdr.nb_files > 16384) {
            fprintf(stderr, "ERROR: Too many entries.\n");
            goto out;
        }

        entries64 = calloc(hdr.nb_files, sizeof(pak_entry64));
        if (entries64 == NULL) {
            fprintf(stderr, "ERROR: Can't allocate entries\n");
            goto out;
        }

        if (fread(entries64, sizeof(pak_entry64), hdr.nb_files, file) != hdr.nb_files) {
            fprintf(stderr, "ERROR: Can't read PAK hdr\n");
            goto out;
        }

        // Detect if we are dealing with 32 or 64-bit pak entries by checking
        // the data_offsets at the expected 32 and 64-bit struct location and
        // adding the absolute value of the difference with last data_offset.
        // The sum that is closest to zero tells us if we are dealing with a
        // 32 or 64-bit PAK archive.
        //uint64_t sum[2] = { 0, 0 };
        //uint32_t val[2], last[2] = { 0, 0 };
        //for (uint32_t i = 0; i < min(hdr.nb_files, 64); i++) {
        //    val[0] = ((pak_entry32*)entries64)[i].data_offset;
        //    val[1] = (uint32_t)(entries64[i].data_offset >> 32);
        //    for (int j = 0; j < 2; j++) {
        //        sum[j] += (val[j] > last[j]) ? val[j] - last[j] : last[j] - val[j];
        //        last[j] = val[j];
        //    }
        //}
        //is_pak64 = (sum[0] > sum[1]);
        printf("Detected %s PAK format\n\n", is_pak64 ? "A18/64-bit" : "A17/32-bit");

        // Store the data we'll need to reconstruct the archive to a JSON file
        json = json_value_init_object();
        json_object_set_string(json_object(json), "name", change_extension(basename(argv[argc - 1]), ".pak"));
        json_object_set_number(json_object(json), "version", hdr.version);
        json_object_set_number(json_object(json), "header_size", hdr.header_size);
        json_object_set_number(json_object(json), "flags", hdr.flags);
        json_object_set_number(json_object(json), "nb_files", hdr.nb_files);
        json_object_set_boolean(json_object(json), "64-bit", is_pak64);

        uint64_t file_data_offset = sizeof(pak_header) +
            (uint64_t)hdr.nb_files * (is_pak64 ? sizeof(pak_entry64) : sizeof(pak_entry32));
        JSON_Value* json_files_array = json_value_init_array();
        printf("OFFSET    SIZE     NAME\n");
        for (uint32_t i = 0; i < hdr.nb_files; i++) {
            int j;
            for (j = 0; (j < 20) && (entry(i, key)[j] == 0); j++);
            bool skip_decode = (j >= 20);
            if (!skip_decode)
                decode((uint8_t*)entry(i, filename), entry(i, key), 128);
            for (size_t n = 0; n < strlen(entry(i, filename)); n++) {
                if (entry(i, filename)[n] == '\\')
                    entry(i, filename)[n] = PATH_SEP;
            }
            printf("%09" PRIx64 " %08x %s%c\n", entry(i, data_offset) + file_data_offset,
                entry(i, size), entry(i, filename), skip_decode ? '*' : ' ');
            if (list_only)
                continue;
            JSON_Value* json_file = json_value_init_object();
            json_object_set_string(json_object(json_file), "name", entry(i, filename));
            json_object_set_string(json_object(json_file), "key", key_to_string(entry(i, key)));
            uint64_t flags = (is_pak64) ? getbe64(&entries64[i].flags) : getbe32(&entries32[i].flags);
            if (flags != 0)
                json_object_set_number(json_object(json_file), "flags", (double)flags);
            json_array_append_value(json_array(json_files_array), json_file);
            strcpy(path, &entry(i, filename)[1]);
            for (size_t n = strlen(path); n > 0; n--) {
                if (path[n] == PATH_SEP) {
                    path[n] = 0;
                    break;
                }
            }
            if (!create_path(path)) {
                fprintf(stderr, "ERROR: Can't create path '%s'\n", path);
                goto out;
            }
            fseek64(file, entry(i, data_offset) + file_data_offset, SEEK_SET);
            buf = malloc(entry(i, size));
            if (buf == NULL) {
                fprintf(stderr, "ERROR: Can't allocate entries\n");
                goto out;
            }
            if (fread(buf, 1, entry(i, size), file) != entry(i, size)) {
                fprintf(stderr, "ERROR: Can't read archive\n");
                goto out;
            }
            if (!skip_decode)
                decode(buf, entry(i, key), entry(i, size));
            if (!write_file(buf, entry(i, size), &entry(i, filename)[1], false))
                goto out;
            free(buf);
            buf = NULL;
        }

        if (!list_only) {
            json_object_set_value(json_object(json), "files", json_files_array);
            json_serialize_to_file_pretty(json, change_extension(argv[argc - 1], ".json"));
        }
        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(entries64);
    if (file != NULL)
        fclose(file);

    if (r != 0) {
        fflush(stdin);
        printf("\nPress any key to continue...");
        (void)getchar();
    }

    return r;
}

CALL_MAIN
