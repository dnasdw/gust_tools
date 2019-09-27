/*
  DDS definitions
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

#pragma once

#define DDS_MAGIC                   0x20534444  // "DDS "

#define DDS_FOURCC                  0x00000004  // DDPF_FOURCC
#define DDS_RGB                     0x00000040  // DDPF_RGB
#define DDS_RGBA                    0x00000041  // DDPF_RGB | DDPF_ALPHAPIXELS
#define DDS_LUMINANCE               0x00020000  // DDPF_LUMINANCE
#define DDS_LUMINANCEA              0x00020001  // DDPF_LUMINANCE | DDPF_ALPHAPIXELS
#define DDS_ALPHA                   0x00000002  // DDPF_ALPHA
#define DDS_PAL8                    0x00000020  // DDPF_PALETTEINDEXED8

#define DDS_HEADER_FLAGS_TEXTURE    0x00001007  // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
#define DDS_HEADER_FLAGS_MIPMAP     0x00020000  // DDSD_MIPMAPCOUNT
#define DDS_HEADER_FLAGS_VOLUME     0x00800000  // DDSD_DEPTH
#define DDS_HEADER_FLAGS_PITCH      0x00000008  // DDSD_PITCH
#define DDS_HEADER_FLAGS_LINEARSIZE 0x00080000  // DDSD_LINEARSIZE

#define DDS_HEIGHT                  0x00000002  // DDSD_HEIGHT
#define DDS_WIDTH                   0x00000004  // DDSD_WIDTH

#define DDS_SURFACE_FLAGS_TEXTURE   0x00001000  // DDSCAPS_TEXTURE
#define DDS_SURFACE_FLAGS_MIPMAP    0x00400008  // DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
#define DDS_SURFACE_FLAGS_CUBEMAP   0x00000008  // DDSCAPS_COMPLEX

#define DDS_CUBEMAP_POSITIVEX       0x00000600  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX
#define DDS_CUBEMAP_NEGATIVEX       0x00000a00  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEX
#define DDS_CUBEMAP_POSITIVEY       0x00001200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEY
#define DDS_CUBEMAP_NEGATIVEY       0x00002200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEY
#define DDS_CUBEMAP_POSITIVEZ       0x00004200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEZ
#define DDS_CUBEMAP_NEGATIVEZ       0x00008200  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEZ

#define DDS_CUBEMAP_ALLFACES (DDS_CUBEMAP_POSITIVEX | DDS_CUBEMAP_NEGATIVEX |\
                              DDS_CUBEMAP_POSITIVEY | DDS_CUBEMAP_NEGATIVEY |\
                              DDS_CUBEMAP_POSITIVEZ | DDS_CUBEMAP_NEGATIVEZ)

#define DDS_CUBEMAP                 0x00000200  // DDSCAPS2_CUBEMAP

#define DDS_FLAGS_VOLUME            0x00200000  // DDSCAPS2_VOLUME

// Make sure DXT1, DXT3 and DXT5 stay defined to 1, 3 & 5 respectively
#define DDS_FORMAT_DXT1     1
#define DDS_FORMAT_ABGR     2
#define DDS_FORMAT_DXT3     3
#define DDS_FORMAT_RGBA     4
#define DDS_FORMAT_DXT5     5

#pragma pack(push, 1)

typedef struct
{
    uint32_t size;
    uint32_t flags;
    char     fourCC[4];
    uint32_t RGBBitCount;
    uint32_t RBitMask;
    uint32_t GBitMask;
    uint32_t BBitMask;
    uint32_t ABitMask;
} DDS_PIXELFORMAT;

typedef struct
{
    uint32_t        size;
    uint32_t        flags;
    uint32_t        height;
    uint32_t        width;
    uint32_t        pitchOrLinearSize;
    uint32_t        depth;
    uint32_t        mipMapCount;
    uint32_t        reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        caps;
    uint32_t        caps2;
    uint32_t        caps3;
    uint32_t        caps4;
    uint32_t        reserved2;
} DDS_HEADER;

typedef struct
{
    uint32_t        dxgiFormat;
    uint32_t        resourceDimension;
    uint32_t        miscFlag;
    uint32_t        arraySize;
    uint32_t        reserved;
} DDS_HEADER_DXT10;

#pragma pack(pop)
