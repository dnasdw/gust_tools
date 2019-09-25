/*
  A18_Decrypt - Archive unpacker for Gust (Koei/Tecmo) PC games 
  Copyright © 2019 VitaSmith
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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

typedef struct {
    uint32_t unknown1;
    uint32_t nb_entries;
    uint32_t unknown2;
    uint32_t unknown3;
} pak_header;

typedef struct {
    char     filename[128];
    uint32_t length;
    uint8_t  key[20];
    uint64_t data_offset;
    uint64_t dummy;
} pak_entry64;

static pak_header header;
static pak_entry64* entries = NULL;

uint8_t blank_key[20] = { 0 };

bool create_path(char* path)
{
    bool result = true;
    DWORD attr = GetFileAttributesA(path);
    if (attr == 0xFFFFFFFF) {
        // Directory doesn't exist, create it
        size_t pos = 0;
        for (size_t n = strlen(path); n > 0; n--) {
            if (path[n] == '\\') {
                pos = n;
                break;
            }
        }
        if (pos > 0) {
            // Create parent dirs
            path[pos] = 0;
            char* new_path = (char*)malloc(sizeof(char) * (strlen(path) + 1));
            if (new_path == NULL) {
                fprintf(stderr, "ERROR: Can't allocate path\n");
                return false;
            }
            strcpy(new_path, path);
            result = create_path(new_path);
            free(new_path);
            path[pos] = '\\';
        }
        // Create node:
        result = result && CreateDirectoryA((LPCSTR)path, NULL);
    } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // Object already exists, but is not a dir
        SetLastError(ERROR_FILE_EXISTS);
    }

    return result;
}

void __inline decode(uint8_t* a, uint8_t* k, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
        a[i] ^= k[i % 20];
}

int main(int argc, char** argv)
{
    char app_name[64];
    if (argc != 2) {
        _splitpath_s(argv[0], NULL, 0, NULL, 0, app_name, sizeof(app_name), NULL, 0);
        printf("A18_decrypt (c) 2018-2019 Yuri Hime & VitaSmith\n\nUsage: %s <Gust PAK file>\n\n"
            "Dumps the Gust PAK format archive to the current directory.\n"
            "If unpacked to the game directory, you can remove the .pak file\n"
            "and it will use the unpacked assets. Have fun, modders!\n", app_name);
        return 0;
    }
    FILE* src = fopen(argv[1], "rb");
    if (src == NULL) {
        fprintf(stderr, "Can't open PAK file");
        return -1;
    }
 
    if (fread(&header, sizeof(header), 1, src) == 0) {
        fprintf(stderr, "Can't read header");
        return -1;
    }

    if ((header.unknown1 != 0x20000) || (header.unknown2 != 0x10) || (header.unknown3 != 0x0D)) {
        fprintf(stderr, "WARNING: Signature doesn't match expected PAK file format.\n");
    }

    if (header.nb_entries > 16384) {
        fprintf(stderr, "WARNING: More than 16384 entries, is this a supported archive?\n");
    }
    entries = (pak_entry64*)malloc(sizeof(pak_entry64) * header.nb_entries);
    if (entries == NULL) {
        fprintf(stderr, "ERROR: Can't allocate entries\n");
        return -1;
    }

    fread(entries, sizeof(pak_entry64), header.nb_entries, src);
    int64_t file_data_offset = _ftelli64(src);
    puts("OFFSET    SIZE     NAME");
    char path[MAX_PATH];
    uint8_t* buf;
    bool skip_decode;
    for (uint32_t i = 0; i < header.nb_entries; i++) {
        skip_decode = true;
        for (int j = 0; j < 20; j++)
            if (entries[i].key[j] != blank_key[j])
                skip_decode = false;
        if (!skip_decode)
            decode((uint8_t*)entries[i].filename, entries[i].key, 128);
        printf("%09I64x %08x %s\n", entries[i].data_offset + file_data_offset, entries[i].length, entries[i].filename);
        strcpy(path, (char*)entries[i].filename + 1);
        for (size_t n = strlen(path); n > 0; n--) {
            if (path[n] == '\\') {
                path[n] = 0;
                break;
            }
        }
        if (create_path(path) == false) {
            fprintf(stderr, "Can't create path %s\n", path);
            continue;
        }
        FILE* dst = NULL;
        dst = fopen((char*)(entries[i].filename + 1), "wb");
        if (dst == NULL) {
            fprintf(stderr, "Can't open file %s\n", entries[i].filename + 1);
            continue;
        }
        _fseeki64(src, entries[i].data_offset + file_data_offset, SEEK_SET);
        buf = (unsigned char*)malloc(sizeof(unsigned char) * entries[i].length);
        fread(buf, 1, entries[i].length, src);
        if (!skip_decode)
            decode(buf, entries[i].key, entries[i].length);
        fwrite(buf, 1, entries[i].length, dst);
        free(buf);
        fclose(dst);
    }
    fclose(src);
    free(entries);
    return 0;
}
