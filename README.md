# Gust Tools

[![Build status](https://img.shields.io/appveyor/ci/VitaSmith/gust-tools.svg?style=flat-square)](https://ci.appveyor.com/project/VitaSmith/gust-tools)
[![Github stats](https://img.shields.io/github/downloads/VitaSmith/gust_tools/total.svg?style=flat-square)](https://github.com/VitaSmith/gust_tools/releases)

A set of commandline utilities designed to work with Gust (Koei/Tecmo) PC game assets such as the ones from
[_Atelier series_](https://store.steampowered.com/search/?sort_by=Name_ASC&term=atelier&tags=122&category1=998),
[_Blue Reflection_](https://store.steampowered.com/app/658260/BLUE_REFLECTION__BLUE_REFLECTION/),
[_Toukiden series_](https://store.steampowered.com/search/?term=toukiden&category1=998), ...

Utilities
=========

* `gust_pak`: Unpack a Gust `.pak` archive.
* `gust_elixir`: Unpack a Gust `.elixir[.gz]` archive.
* `gust_g1t`: Unpack or repack a Gust `.g1t` texture archive.
* `gust_enc`: Encode or decode a Gust `.e` archive.

Notes
-----

`gust_pak` is designed to replace both `A17_Decrypt` and `A18_Decrypt`, as it detects "A17" (32-bit) and "A18" (64-bit) formats
automatically. It should therefore works with all of the Atelier PC ports (including _Atelier Sophie_) as well as _Blue Reflection_'s archives.

`gust_enc` only works on the games where for which the scrambling seeds are known. See `gust_enc.json` for details.

Building
========

If you have Visual Studio 2019 installed, just open the `.sln` file or run `build.cmd`.

Otherwise (Linux, MinGW) just issue `make`.

Usage
=====

On Windows, just drop the archive you want to unpack on top of the executable, and it will be extracted in the current directory.

Otherwise, just invoke `<gust_utility> <gust_archive`.

Modding games
=============

**IMPORTANT: YOU SHOULD BACK UP ALL GAME ARCHIVES AND FOLDERS BEFORE RUNNING THE UNPACKER**

Most Gust game executables are designed to use either packed assets, if a `.pak` archive is present, or the extracted assets, if
a matching directory bearing the same name as the `.pak` is found. For that to work, you must however make sure that the `.pak`
is not seen, as it has precedence over the directory.

For instance, if you want to alter character assets (textures, models, ...) for the game _Blue Reflection_:
* Go to `<GAME_DIR>\DATA\`and copy `gust_pak.exe` there.
* Drop `PACK00_02.pak` on top of `gust_pak.exe`. This will extract all the content into a `data\` subdirectory.
* Move the content from `data\x64\` to `x64\` (in this case, that should only be one folder named `character`). This is needed
  because in this case `<GAME_DIR>\DATA\x64` is the location where _Blue Reflection_ expects extracted game assets, not
  `<GAME_DIR>\DATA\data\x64`.
* Rename `PACK00_02.pak` to `PACK00_02.old` so that the game assets you just extracted are used.

Happy modding! :smile:

License
=======

[GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)

Thanks
======

* _Yuri Hime_/_Lily_/_shizukachan_ and everyone who helped with `A17_Decrypt`/`A18_Decrypt`.
* _Admiral Curtiss_ for [HyoutaTools](https://github.com/AdmiralCurtiss/HyoutaTools/) and _Semory_ for
  [Steven's Gas Machine](http://sticklove.com/xnalara.org/viewtopic.php?f=17&t=1001) (a.k.a. "xentax"), where we picked some
  inspiration on how to unpack the `.elixir` and `.g1t` formats.
* _Rich Geldreich_ and others for the [miniz](https://github.com/richgel999/miniz) inflate/deflate library.
* _Krzysztof Gabis_ for the [parson](http://kgabis.github.com/parson/) JSON parsing library.
* _Gust_, for making games that are interesting enough to make one want to crack their custom compression and encryption schemes. :grin:
