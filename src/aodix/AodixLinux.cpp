/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - helpers specific to the linux port
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <sys/stat.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void adx_resolve_resource_path(const char* rel,char* out)
{
	// absolute path given
	if(rel[0]=='/')
	{
		snprintf(out,_MAX_PATH,"%s",rel);
		return;
	}

	char const* const exe=adxplat::exe_dir();

	char candidates[4][_MAX_PATH];
	snprintf(candidates[0],_MAX_PATH,"%s/%s",exe,rel);
	snprintf(candidates[1],_MAX_PATH,"%s/../share/aodix/%s",exe,rel);
	snprintf(candidates[2],_MAX_PATH,"/usr/local/share/aodix/%s",rel);
	snprintf(candidates[3],_MAX_PATH,"/usr/share/aodix/%s",rel);

	for(int c=0;c<4;c++)
	{
		struct stat st;
		if(stat(candidates[c],&st)==0)
		{
			snprintf(out,_MAX_PATH,"%s",candidates[c]);
			return;
		}
	}

	// default: next to the executable
	snprintf(out,_MAX_PATH,"%s",candidates[0]);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void adx_expand_path(const char* in,char* out,int out_size)
{
	const char* home=getenv("HOME");
	if(in[0]=='~' && (in[1]=='/' || in[1]==0) && home!=NULL)
		snprintf(out,out_size,"%s%s",home,in+1);
	else
		snprintf(out,out_size,"%s",in);
}

void adx_compact_path(const char* in,char* out,int out_size)
{
	const char* home=getenv("HOME");
	size_t const n=home?strlen(home):0;
	if(n>1 && strncmp(in,home,n)==0 && (in[n]=='/' || in[n]==0))
		snprintf(out,out_size,"~%s",in+n);
	else
		snprintf(out,out_size,"%s",in);
}
