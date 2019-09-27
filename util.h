/*
  Common code for Gust (Koei/Tecmo) PC games tools
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

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>

#pragma once

#if defined(_WIN32)
#include <windows.h>
#define ftell64 _ftelli64
#define fseek64 _fseeki64
#if !defined(S_ISDIR)
#define S_ISDIR(ST_MODE) (((ST_MODE) & _S_IFMT) == _S_IFDIR)
#endif
#define CREATE_DIR(path) CreateDirectoryA(path, NULL)
#define PATH_SEP '\\'
#else
#define ftell64 ftello64
#define fseek64 fseeko64
#define CREATE_DIR(path) (mkdir(path, 0755) == 0)
#define PATH_SEP '/'
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#if defined(_WIN32)
static __inline char* basename(const char* path)
{
    static char app_name[64];
    _splitpath_s(path, NULL, 0, NULL, 0, app_name, sizeof(app_name), NULL, 0);
    return app_name;
}
#endif

#if defined (_MSC_VER)
#include <stdlib.h>
#pragma intrinsic(_byteswap_ulong)
#define bswap_uint32 _byteswap_ulong
#else
#define bswap_uint32 __builtin_bswap32
#endif
#define getbe32(p) bswap_uint32(*(const uint32_t*)(const uint8_t*)(p))

bool create_path(char* path);
