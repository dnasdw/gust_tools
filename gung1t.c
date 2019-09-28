/*
  Gung1t - DDS texture unpacker for Gust (Koei/Tecmo) .g1t files
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
#include "dds.h"

#define G1TG_MAGIC              0x47315447  // "G1GT"

#pragma pack(push, 1)
typedef struct {
    uint32_t    magic;
    char        version[4];
    uint32_t    total_size;
    uint32_t    header_size;
    uint32_t    nb_textures;
    uint32_t    unknown;        // Usually 0xA
    uint32_t    extra_size;     // Always 0
} g1t_header;

// This is followed by uint32_t flag_table[nb_textures]

typedef struct {
    uint8_t     discard : 4;
    uint8_t     mipmaps : 4;
    uint8_t     type;
    uint8_t     dx : 4;
    uint8_t     dy : 4;
    uint8_t     unused;         // Always 0x00
    uint32_t    flags;          // 0x10211000 or 0x00211000
} g1t_tex_header;

#define G1T_TEX_EXTRA_FLAG      0x10000000

typedef struct {
    uint32_t    size;
    uint64_t    extra_flags;
} g1t_tex_extra;
#pragma pack(pop)

static size_t write_dds_header(FILE* fd, int format, uint32_t width, uint32_t height, uint32_t mipmaps)
{
    if ((fd == NULL) || (width == 0) || (height == 0))
        return 0;
    if ((format < DDS_FORMAT_DXT1) && (format > DDS_FORMAT_BC7))
        return 0;

    DDS_HEADER header = { 0 };
    header.size = 124;
    header.flags = DDS_HEADER_FLAGS_TEXTURE;
    header.height = height;
    header.width = width;
    header.ddspf.size = 32;
    header.ddspf.flags = DDS_FOURCC;
    header.ddspf.fourCC = get_fourCC(format);
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
    uint8_t* buf = NULL;
    uint32_t magic;
    int r = -1;
    const char* app_name = basename(argv[0]);
    if (argc != 2) {
        printf("%s (c) 2019 VitaSmith\n\nUsage: %s <file.g1t>\n\n"
            "Dumps G1T textures to the current directory.\n",
            app_name, app_name);
        return 0;
    }

    // Don't bother checking for case or if these extensions are really at the end
    char* g1t_pos = strstr(argv[1], ".g1t");
    if (g1t_pos == NULL) {
        fprintf(stderr, "ERROR: File should have a '.g1t' extension\n");
        return -1;
    }

    FILE* src = fopen(argv[1], "rb");
    if (src == NULL) {
        fprintf(stderr, "ERROR: Can't open file '%s'", argv[1]);
        return -1;
    }

    if (fread(&magic, sizeof(magic), 1, src) != 1) {
        fprintf(stderr, "ERROR: Can't read from '%s'", argv[1]);
        goto out;
    }
    if (magic != G1TG_MAGIC) {
        fprintf(stderr, "ERROR: Not a G1T file (bad magic)");
        goto out;
    }
    fseek(src, 0L, SEEK_END);
    uint32_t g1t_size = (uint32_t)ftell(src);
    fseek(src, 0L, SEEK_SET);

    buf = malloc(g1t_size);
    if (buf == NULL)
        goto out;
    if (fread(buf, 1, g1t_size, src) != g1t_size) {
        fprintf(stderr, "ERROR: Can't read file");
        goto out;
    }

    g1t_header* hdr = (g1t_header*)buf;
    if (hdr->total_size != g1t_size) {
        fprintf(stderr, "ERROR: File size mismatch\n");
        goto out;
    }
    if ((hdr->version[0] != '0') || (hdr->version[2] != '0') ||
        ((hdr->version[1] != '5') && (hdr->version[1] != '6')) ||
        (hdr->version[3] != '0')) {
        fprintf(stderr, "ERROR: Unsupported G1T version %c%c.%c%x\n",
            hdr->version[1], hdr->version[2], hdr->version[0], hdr->version[0]);
        goto out;
    }

    g1t_pos[0] = 0;
    if (!create_path(argv[1]))
        goto out;

//    uint32_t* flag_table = (uint32_t*)&buf[sizeof(hdr)];
    uint32_t* offset_table = (uint32_t*)&buf[hdr->header_size];

    char path[256];
    printf("OFFSET   SIZE     NAME\n");
    for (uint32_t i = 0; i < hdr->nb_textures; i++) {
        uint32_t pos = hdr->header_size + offset_table[i];
        g1t_tex_header* tex = (g1t_tex_header*) &buf[pos];
        uint32_t width = 1 << tex->dx;
        uint32_t height = 1 << tex->dy;
        uint32_t texture_format, bits_per_pixel;
        switch (tex->type) {
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
        snprintf(path, sizeof(path), "%s%c%03d.dds", argv[1], PATH_SEP, i);
        printf("%08x %08x %s (%dx%d) [%d]\n", hdr->header_size + offset_table[i],
            ((i + 1 == hdr->nb_textures) ? g1t_size : offset_table[i + 1]) - offset_table[i],
            path, width, height, tex->mipmaps);
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
        pos += sizeof(g1t_tex_header);
        if (tex->flags & G1T_TEX_EXTRA_FLAG) {
            uint32_t size = ((uint32_t*)buf)[pos/4];
            assert(pos + size < g1t_size);
            pos += size;
        }
        if (fwrite(&buf[pos], texture_size, 1, dst) != 1) {
            fprintf(stderr, "ERROR: Can't write DDS data\n");
            fclose(dst);
            continue;
        }
        fclose(dst);
    }

    r = 0;

out:
    free(buf);
    fclose(src);
    return r;
}