# Gust Tools

Utilities designed to work with Gust (Koei/Tecmo) PC game assets such as the ones from
[Atelier series](https://store.steampowered.com/search/?sort_by=Name_ASC&term=atelier&tags=122&category1=998),
[Blue Reflection](https://store.steampowered.com/app/658260/BLUE_REFLECTION__BLUE_REFLECTION/), ...

Usage
=====

**IMPORTANT: BACK UP ALL GAME ARCHIVES AND FOLDERS BEFORE RUNNING THE UNPACKER**

The unpacker extracts files to the current directory so if files/directories already exist, they **will be overwritten**.
Furthermore, if files or directories already exist, it will be **difficult to tell original from unpacked files**.

To unpack an archive to the current directory, just use the command `Pak_decrypt PACK####.pak` where `PACK####.pak` is the archive you wish to unpack.

Alternatively, if running on Windows, you can simply copy the executable to your game directory and drop a `.pak` file onto it.

Afterwards, you should rename `PACK####.pak` to `PACK####.bak` to get the game to use the unpacked files (which you can mod).
You must rename or delete the original archive, else the game will still use assets from it instead of the unpacked files.

Notes
-----

`Pak_decrypt` is designed to replace both `A17_Decrypt` and `A18_Decrypt`, as it detects "A17" (32-bit) and "A18" (64-bit) formats
automatically. It should therefore works with all of the Atelier PC ports (including Atelier Sophie) as well as Blue Reflection's archives.

You should also be able to compile and run `Pak_decrypt` on Linux (provided you are using a Little Endian machine).

Performance
===========

The performance of the decryption could be improved by vectorizing it; in the game engines, a SSE2 `xor` routine is used instead of the bytewise solution used here.

License
=======

[GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)