/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - helpers specific to the linux port
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdio>

// resolve a path to a bundled resource (skins, help). relative paths are looked up next to the executable,
// then in <prefix>/share/aodix. 'out' must hold _MAX_PATH characters.
void adx_resolve_resource_path(const char* rel,char* out);

// "~/..." -> "$HOME/..." (plugin folders in the configuration may start with ~)
void adx_expand_path(const char* in,char* out,int out_size);

// "$HOME/..." -> "~/..." (for display in the configuration dialog)
void adx_compact_path(const char* in,char* out,int out_size);

// project files: ".adxl" is the linux variant of ".adx" - the same content, gzip compressed (an .adx consists mostly of
// empty fixed-size tables). Loading recognizes compressed files by their content, whatever the name; saving compresses
// when the name ends with ".adxl".
bool  adx_project_is_compressed_name(const char* path);

// open a project for reading; a compressed file is unpacked into an anonymous temporary file (seekable, removed on fclose)
FILE* adx_project_open_read(const char* path);

// open a project for writing: the file itself, or a temporary file for ".adxl" (written by adx_project_close_write)
FILE* adx_project_open_write(const char* path);

// close a file from adx_project_open_write; for ".adxl" the data is compressed into the file (replaced atomically).
// returns false if the file could not be written
bool  adx_project_close_write(FILE* pfile,const char* path);
