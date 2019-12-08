/*
  gust_g1t - DDS texture unpacker for Gust (Koei/Tecmo) .g1t files
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
#include "dds.h"

#define G1TG_MAGIC              0x47315447  // "G1GT"
#define G1T_TEX_EXTRA_FLAG      0x10000000
#define JSON_VERSION            1
#define json_object_get_uint32  (uint32_t)json_object_get_number

#pragma pack(push, 1)
typedef struct {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    total_size;
    uint32_t    header_size;
    uint32_t    nb_textures;
    uint32_t    flags;          // Usually 0xA
    uint32_t    extra_size;     // Always 0
} g1t_header;

// This is followed by uint32_t extra_flags[nb_textures]

typedef struct {
    uint8_t     zero : 4;       // Always 0x0
    uint8_t     mipmaps : 4;
    uint8_t     type;
    uint8_t     dx : 4;
    uint8_t     dy : 4;
    uint8_t     unused;         // Always 0x00
    uint32_t    flags;          // 0x10211000 or 0x00211000
} g1t_tex_header;

// May be followed by extra_data[]

#pragma pack(pop)

static size_t write_dds_header(FILE* fd, int format, uint32_t width, uint32_t height, uint32_t mipmaps)
{
    if ((fd == NULL) || (width == 0) || (height == 0))
        return 0;

    DDS_HEADER header = { 0 };
    header.size = 124;
    header.flags = DDS_HEADER_FLAGS_TEXTURE;
    header.height = height;
    header.width = width;
    header.ddspf.size = 32;
    if (format == DDS_FORMAT_RGBA || format == DDS_FORMAT_ABGR) {
        header.ddspf.flags = DDS_RGBA;
        header.ddspf.RGBBitCount = 32;
        // We're actually saving ARGB to keep VS and PhotoShop happy
        header.ddspf.RBitMask = 0x00ff0000;
        header.ddspf.GBitMask = 0x0000ff00;
        header.ddspf.BBitMask = 0x000000ff;
        header.ddspf.ABitMask = 0xff000000;
    } else {
        header.ddspf.flags = DDS_FOURCC;
        header.ddspf.fourCC = get_fourCC(format);
    }
    header.caps = DDS_SURFACE_FLAGS_TEXTURE;
    if (mipmaps != 0) {
        header.mipMapCount = mipmaps;
        header.flags |= DDS_HEADER_FLAGS_MIPMAP;
        header.caps |= DDS_SURFACE_FLAGS_MIPMAP;
    }
    size_t r = fwrite(&header, sizeof(DDS_HEADER), 1, fd);
    if (r != 1)
        return r;
    if (format == DDS_FORMAT_BC7) {
        DDS_HEADER_DXT10 header10 = { 0 };
        header10.dxgiFormat = DXGI_FORMAT_BC7_UNORM;
        header10.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
        header10.miscFlags2 = DDS_ALPHA_MODE_STRAIGHT;
        r = fwrite(&header10, sizeof(DDS_HEADER_DXT10), 1, fd);
    }
    return r;
}

int main(int argc, char** argv)
{
    int r = -1;
    FILE *file = NULL;
    uint8_t* buf = NULL;
    uint32_t* offset_table = NULL;
    uint32_t magic;
    char path[256];
    JSON_Value* json = NULL;
    bool list_only = (argc == 3) && (argv[1][0] == '-') && (argv[1][1] == 'l');

    if ((argc != 2) && !list_only) {
        printf("%s %s (c) 2019 VitaSmith\n\n"
            "Usage: %s [-l] <file or directory>\n\n"
            "Extracts (file) or recreates (directory) a Gust .g1t texture archive.\n\n"
            "Note: A backup (.bak) of the original is automatically created, when the target\n"
            "is being overwritten for the first time.\n",
            appname(argv[0]), GUST_TOOLS_VERSION_STR, appname(argv[0]));
        return 0;
    }

    if (is_directory(argv[argc - 1])) {
        if (list_only) {
            fprintf(stderr, "ERROR: Option -l is not supported when creating an archive\n");
            goto out;
        }
        snprintf(path, sizeof(path), "%s%cg1t.json", argv[argc - 1], PATH_SEP);
        if (!is_file(path)) {
            fprintf(stderr, "ERROR: '%s' does not exist\n", path);
            goto out;
        }
        json = json_parse_file_with_comments(path);
        if (json == NULL) {
            fprintf(stderr, "ERROR: Can't parse JSON data from '%s'\n", path);
            goto out;
        }
        const uint32_t json_version = json_object_get_uint32(json_object(json), "json_version");
        if (json_version != JSON_VERSION) {
            fprintf(stderr, "ERROR: This utility is not compatible with the JSON file provided.\n"
                "You need to (re)extract the '.g1t' using this application.\n");
            goto out;
        }
        const char* filename = json_object_get_string(json_object(json), "name");
        const char* version = json_object_get_string(json_object(json), "version");
        if ((filename == NULL) || (version == NULL))
            goto out;
        printf("Creating '%s'...\n", filename);
        create_backup(filename);
        file = fopen(filename, "wb+");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't create file '%s'\n", filename);
            goto out;
        }
        g1t_header hdr = { 0 };
        hdr.magic = G1TG_MAGIC;
        hdr.version = getbe32(version);
        hdr.total_size = 0;  // To be rewritten when we're done
        hdr.nb_textures = json_object_get_uint32(json_object(json), "nb_textures");
        hdr.flags = json_object_get_uint32(json_object(json), "flags");
        hdr.extra_size = json_object_get_uint32(json_object(json), "extra_size");
        hdr.header_size = sizeof(hdr) + hdr.nb_textures * sizeof(uint32_t);
        if (fwrite(&hdr, sizeof(hdr), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write header\n");
            goto out;
        }

        JSON_Array* extra_flags_array = json_object_get_array(json_object(json), "extra_flags");
        if (json_array_get_count(extra_flags_array) != hdr.nb_textures) {
            fprintf(stderr, "ERROR: number of extra flags doesn't match number of textures\n");
            goto out;
        }
        for (uint32_t i = 0; i < hdr.nb_textures; i++) {
            uint32_t extra_flag = (uint32_t)json_array_get_number(extra_flags_array, i);
            if (fwrite(&extra_flag, sizeof(uint32_t), 1, file) != 1) {
                fprintf(stderr, "ERROR: Can't write extra flags\n");
                goto out;
            }
        }

        offset_table = calloc(hdr.nb_textures, sizeof(uint32_t));
        offset_table[0] = hdr.nb_textures * sizeof(uint32_t);
        if (fwrite(offset_table, hdr.nb_textures * sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't write texture offsets\n");
            goto out;
        }

        JSON_Array* textures_array = json_object_get_array(json_object(json), "textures");
        if (json_array_get_count(textures_array) != hdr.nb_textures) {
            fprintf(stderr, "ERROR: number of textures in array doesn't match\n");
            goto out;
        }

        printf("OFFSET   SIZE     NAME");
        for (size_t i = 0; i < strlen(basename(argv[argc - 1])); i++)
            putchar(' ');
        printf("     DIMENSIONS MIPMAPS\n");
        for (uint32_t i = 0; i < hdr.nb_textures; i++) {
            offset_table[i] = ftell(file) - hdr.header_size;
            JSON_Object* texture_entry = json_array_get_object(textures_array, i);
            g1t_tex_header tex = { 0 };
            tex.type = (uint8_t)json_object_get_number(texture_entry, "type");
            tex.flags = json_object_get_uint32(texture_entry, "flags");
            // Read the DDS file
            snprintf(path, sizeof(path), "%s%c%s", basename(argv[argc - 1]), PATH_SEP,
                json_object_get_string(texture_entry, "name"));
            uint32_t dds_size = read_file(path, &buf);
            if (dds_size <= sizeof(DDS_HEADER)) {
                fprintf(stderr, "ERROR: '%s' is too small\n", path);
                goto out;
            }
            if (*((uint32_t*)buf) != DDS_MAGIC) {
                fprintf(stderr, "ERROR: '%s' is not a DDS file\n", path);
                goto out;
            }
            DDS_HEADER* dds_header = (DDS_HEADER*)&buf[sizeof(uint32_t)];
            dds_size -= sizeof(uint32_t) + sizeof(DDS_HEADER);
            // We may have a DX10 additional hdr
            if (dds_header->ddspf.fourCC == get_fourCC(DDS_FORMAT_DX10))
                dds_size -= sizeof(DDS_HEADER_DXT10);
            tex.mipmaps = (uint8_t)dds_header->mipMapCount;
            // Are both width and height a power of two?
            bool po2_sizes = (popcount(dds_header->width) == 1) && (popcount(dds_header->height) == 1);
            if (po2_sizes) {
                tex.dx = (uint8_t)find_msb(dds_header->width);
                tex.dy = (uint8_t)find_msb(dds_header->height);
            }
            // Write texture header
            if (fwrite(&tex, sizeof(tex), 1, file) != 1) {
                fprintf(stderr, "ERROR: Can't write texture header\n");
                goto out;
            }
            // Write extra data
            if (tex.flags & G1T_TEX_EXTRA_FLAG) {
                JSON_Array* extra_data_array = json_object_get_array(texture_entry, "extra_data");
                uint32_t extra_data_size = (uint32_t)(json_array_get_count(extra_data_array) + 1) * sizeof(uint32_t);
                if (!po2_sizes && extra_data_size < 4 * sizeof(uint32_t)) {
                    fprintf(stderr, "ERROR: Non power-of-two width or height is missing from extra data\n");
                    goto out;
                }
                if (fwrite(&extra_data_size, sizeof(uint32_t), 1, file) != 1) {
                    fprintf(stderr, "ERROR: Can't write extra data size\n");
                    goto out;
                }
                for (size_t j = 0; j < json_array_get_count(extra_data_array); j++) {
                    uint32_t extra_data = (uint32_t)json_array_get_number(extra_data_array, j);
                    if ((j == 2) && (!po2_sizes) && (extra_data != dds_header->width)) {
                        fprintf(stderr, "ERROR: DDS width and extra data width don't match\n");
                        goto out;
                    }
                    if ((j == 3) && (!po2_sizes) && (extra_data != dds_header->height)) {
                        fprintf(stderr, "ERROR: DDS height and extra data height don't match\n");
                        goto out;
                    }
                    if (fwrite(&extra_data, sizeof(uint32_t), 1, file) != 1) {
                        fprintf(stderr, "ERROR: Can't write extra data\n");
                        goto out;
                    }
                }
            }
            // Convert ARGB back to RGBA or ABGR
            if ((tex.type == 0x00) || (tex.type == 0x01)) {
                if (dds_size % sizeof(uint32_t) != 0) {
                    fprintf(stderr, "ERROR: ARGB texture size should be a multiple of 32 bits\n");
                    goto out;
                }
                if ((dds_header->ddspf.flags != DDS_RGBA) || (dds_header->ddspf.RGBBitCount != 32) ||
                    (dds_header->ddspf.RBitMask != 0x00ff0000) || (dds_header->ddspf.GBitMask != 0x0000ff00) ||
                    (dds_header->ddspf.BBitMask != 0x000000ff) || (dds_header->ddspf.ABitMask != 0xff000000)) {
                    fprintf(stderr, "ERROR: '%s' is not a supported ARGB texture we support\n", path);
                    goto out;
                }
                uint32_t* p = (uint32_t*)&dds_header[1];
                for (uint32_t j = 0; j < dds_size / 4; j++) {
                    uint32_t w = getbe32(&p[j]);
                    uint32_t a = w >> 24;
                    w = (w << 8) | a;
                    if (tex.type == 0x01)
                        setbe32(&p[j], w);
                    else
                        setle32(&p[j], w);
                }
            }
            // Write texture
            if (fwrite(&dds_header[1], 1, dds_size, file) != dds_size) {
                fprintf(stderr, "ERROR: Can't write texture data\n");
                goto out;
            }
            char dims[16];
            snprintf(dims, sizeof(dims), "%dx%d", dds_header->width, dds_header->height);
            printf("%08x %08x %s %-10s %d\n", hdr.header_size + offset_table[i],
                (uint32_t)ftell(file) - offset_table[i] - hdr.header_size, path,
                dims, dds_header->mipMapCount);
            free(buf);
            buf = NULL;
        }
        // Update total size
        uint32_t total_size = ftell(file);
        fseek(file, 2 * sizeof(uint32_t), SEEK_SET);
        if (fwrite(&total_size, sizeof(uint32_t), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't update total size\n");
            goto out;
        }
        // Update offset table
        fseek(file, sizeof(hdr) + hdr.nb_textures * sizeof(uint32_t), SEEK_SET);
        if (fwrite(offset_table, sizeof(uint32_t), hdr.nb_textures, file) != hdr.nb_textures) {
            fprintf(stderr, "ERROR: Can't update texture offsets\n");
            goto out;
        }
        r = 0;
    } else {
        printf("%s '%s'...\n", list_only ? "Listing" : "Extracting", basename(argv[argc - 1]));
        char* g1t_pos = strstr(argv[argc - 1], ".g1t");
        if (g1t_pos == NULL) {
            fprintf(stderr, "ERROR: File should have a '.g1t' extension\n");
            goto out;
        }

        file = fopen(argv[argc - 1], "rb");
        if (file == NULL) {
            fprintf(stderr, "ERROR: Can't open file '%s'", argv[argc - 1]);
            goto out;
        }

        if (fread(&magic, sizeof(magic), 1, file) != 1) {
            fprintf(stderr, "ERROR: Can't read from '%s'", argv[argc - 1]);
            goto out;
        }
        if (magic != G1TG_MAGIC) {
            fprintf(stderr, "ERROR: Not a G1T file (bad magic)");
            goto out;
        }
        fseek(file, 0L, SEEK_END);
        uint32_t g1t_size = (uint32_t)ftell(file);
        fseek(file, 0L, SEEK_SET);

        buf = malloc(g1t_size);
        if (buf == NULL)
            goto out;
        if (fread(buf, 1, g1t_size, file) != g1t_size) {
            fprintf(stderr, "ERROR: Can't read file");
            goto out;
        }

        g1t_header* hdr = (g1t_header*)buf;
        if (hdr->total_size != g1t_size) {
            fprintf(stderr, "ERROR: File size mismatch\n");
            goto out;
        }
        char version[5];
        setbe32(version, hdr->version);
        version[4] = 0;
        if (hdr->version >> 16 != 0x3030)
            fprintf(stderr, "WARNING: Potentially unsupported G1T version %s\n", version);
        if (hdr->extra_size != 0) {
            fprintf(stderr, "ERROR: Can't handle G1T files with extra content\n");
            goto out;
        }

        uint32_t* x_offset_table = (uint32_t*)&buf[hdr->header_size];

        // Keep the information required to recreate the archive in a JSON file
        json = json_value_init_object();
        json_object_set_number(json_object(json), "json_version", JSON_VERSION);
        json_object_set_string(json_object(json), "name", basename(argv[argc - 1]));
        json_object_set_string(json_object(json), "version", version);
        json_object_set_number(json_object(json), "nb_textures", hdr->nb_textures);
        json_object_set_number(json_object(json), "flags", hdr->flags);
        json_object_set_number(json_object(json), "extra_size", hdr->extra_size);

        g1t_pos[0] = 0;
        if (!list_only && !create_path(argv[argc - 1]))
            goto out;

        JSON_Value* json_flags_array = json_value_init_array();
        JSON_Value* json_textures_array = json_value_init_array();

        printf("OFFSET   SIZE     NAME");
        for (size_t i = 0; i < strlen(basename(argv[argc - 1])); i++)
            putchar(' ');
        printf("     DIMENSIONS MIPMAPS\n");
        for (uint32_t i = 0; i < hdr->nb_textures; i++) {
            // There's an array of flags after the hdr
            json_array_append_number(json_array(json_flags_array), getle32(&buf[(uint32_t)sizeof(g1t_header) + 4 * i]));
            uint32_t pos = hdr->header_size + x_offset_table[i];
            g1t_tex_header* tex = (g1t_tex_header*)&buf[pos];
            pos += sizeof(g1t_tex_header);
            uint32_t width = 1 << tex->dx;
            uint32_t height = 1 << tex->dy;
            uint32_t extra_size = (tex->flags & G1T_TEX_EXTRA_FLAG) ? getle32(&buf[pos]) : 0;
            // Non power-of-two width and height may be provided in the extra data
            if (extra_size >= 0x14) {
                if (width == 1)
                    width = getle32(&buf[pos + 0x0c]);
                if (height == 1)
                    height = getle32(&buf[pos + 0x10]);
            }

            JSON_Value* json_texture = json_value_init_object();
            snprintf(path, sizeof(path), "%03d.dds", i);
            json_object_set_string(json_object(json_texture), "name", path);
            json_object_set_number(json_object(json_texture), "type", tex->type);
            json_object_set_number(json_object(json_texture), "flags", tex->flags);
            uint32_t texture_format, bits_per_pixel;
            switch (tex->type) {
            case 0x00: texture_format = DDS_FORMAT_ABGR; bits_per_pixel = 32; break;
            case 0x01: texture_format = DDS_FORMAT_RGBA; bits_per_pixel = 32; break;
            case 0x06: texture_format = DDS_FORMAT_DXT1; bits_per_pixel = 4; break;
            case 0x08: texture_format = DDS_FORMAT_DXT5; bits_per_pixel = 8; break;
            case 0x59: texture_format = DDS_FORMAT_DXT1; bits_per_pixel = 4; break;
            case 0x5B: texture_format = DDS_FORMAT_DXT5; bits_per_pixel = 8; break;
            case 0x5F: texture_format = DDS_FORMAT_BC7; bits_per_pixel = 8; break;
            default:
                fprintf(stderr, "ERROR: Unsupported texture type (0x%02X)\n", tex->type);
                continue;
            }
            uint32_t highest_mipmap_size = (width * height * bits_per_pixel) / 8;
            uint32_t texture_size = highest_mipmap_size;
            for (int j = 0; j < tex->mipmaps - 1; j++)
                texture_size += highest_mipmap_size / (4 << (j * 2));
            snprintf(path, sizeof(path), "%s%c%03d.dds", basename(argv[argc - 1]), PATH_SEP, i);
            char dims[16];
            snprintf(dims, sizeof(dims), "%dx%d", width, height);
            printf("%08x %08x %s %-10s %d\n", hdr->header_size + x_offset_table[i],
                ((i + 1 == hdr->nb_textures) ? g1t_size : x_offset_table[i + 1]) - x_offset_table[i],
                path, dims, tex->mipmaps);
            if (list_only)
                continue;
            FILE* dst = fopen(path, "wb");
            if (dst == NULL) {
                fprintf(stderr, "ERROR: Can't create file '%s'\n", path);
                continue;
            }
            uint32_t dds_magic = DDS_MAGIC;
            if (fwrite(&dds_magic, sizeof(dds_magic), 1, dst) != 1) {
                fprintf(stderr, "ERROR: Can't write magic\n");
                fclose(dst);
                continue;
            }
            if (write_dds_header(dst, texture_format, width, height, tex->mipmaps) != 1) {
                fprintf(stderr, "ERROR: Can't write DDS header\n");
                fclose(dst);
                continue;
            }
            if (tex->flags & G1T_TEX_EXTRA_FLAG) {
                assert(pos + extra_size < g1t_size);
                if ((extra_size < 8) || (extra_size % 4 != 0)) {
                    fprintf(stderr, "ERROR: Can't handle extra_data of size 0x%08x\n", extra_size);
                } else {
                    JSON_Value* json_extra_array_val = json_value_init_array();
                    JSON_Array* json_extra_array_obj = json_array(json_extra_array_val);
                    for (uint32_t j = 4; j < extra_size; j += 4)
                        json_array_append_number(json_extra_array_obj, getle32(&buf[pos + j]));
                    json_object_set_value(json_object(json_texture), "extra_data", json_extra_array_val);
                }
                pos += extra_size;
            }
            if ((texture_format == DDS_FORMAT_RGBA) || (texture_format == DDS_FORMAT_ABGR)) {
                // So, what's the point of being able to specify a fucking
                // mask for RGBA, if tools like Visual Studio or PhotoShop
                // can't be bothered to actually honor it and enforce ARGB?
                for (uint32_t j = 0; j < texture_size; j += 4) {
                    uint32_t w = (texture_format == DDS_FORMAT_RGBA) ?
                        getbe32(&buf[pos + j]) : getle32(&buf[pos + j]);
                    uint32_t a = (w & 0xff) << 24;
                    setbe32(&buf[pos + j], a | (w >> 8));
                }
            }
            if (fwrite(&buf[pos], texture_size, 1, dst) != 1) {
                fprintf(stderr, "ERROR: Can't write DDS data\n");
                fclose(dst);
                continue;
            }
            fclose(dst);
            json_array_append_value(json_array(json_textures_array), json_texture);
        }

        json_object_set_value(json_object(json), "extra_flags", json_flags_array);
        json_object_set_value(json_object(json), "textures", json_textures_array);
        snprintf(path, sizeof(path), "%s%cg1t.json", argv[argc - 1], PATH_SEP);
        if (!list_only)
            json_serialize_to_file_pretty(json, path);

        r = 0;
    }

out:
    json_value_free(json);
    free(buf);
    free(offset_table);
    if (file != NULL)
        fclose(file);

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
