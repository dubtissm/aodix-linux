/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - helpers specific to the linux port
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

// resolve a path to a bundled resource (skins, help). relative paths are looked up next to the executable,
// then in <prefix>/share/aodix. 'out' must hold _MAX_PATH characters.
void adx_resolve_resource_path(const char* rel,char* out);

// "~/..." -> "$HOME/..." (plugin folders in the configuration may start with ~)
void adx_expand_path(const char* in,char* out,int out_size);

// "$HOME/..." -> "~/..." (for display in the configuration dialog)
void adx_compact_path(const char* in,char* out,int out_size);
