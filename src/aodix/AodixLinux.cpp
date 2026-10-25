/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - helpers specific to the linux port
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <zlib.h>

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// compressed project files (.adxl)
bool adx_project_is_compressed_name(const char* path)
{
	size_t const n=strlen(path);
	return n>=5 && strcasecmp(path+n-5,".adxl")==0;
}

FILE* adx_project_open_read(const char* path)
{
	FILE* pfile=fopen(path,"rb");
	if(pfile==NULL)
		return NULL;

	// gzip magic?
	unsigned char magic[2]={0,0};
	size_t const got=fread(magic,1,2,pfile);
	fclose(pfile);
	if(got<2 || magic[0]!=0x1F || magic[1]!=0x8B)
		return fopen(path,"rb");

	gzFile gz=gzopen(path,"rb");
	if(gz==NULL)
		return NULL;
	FILE* tmp=tmpfile();
	if(tmp==NULL)
	{
		gzclose(gz);
		return NULL;
	}
	std::vector<char> buf(1<<16);
	int n;
	bool ok=true;
	while((n=gzread(gz,buf.data(),(unsigned)buf.size()))>0)
	{
		if(fwrite(buf.data(),1,(size_t)n,tmp)!=(size_t)n)
		{
			ok=false;
			break;
		}
	}
	if(n<0)
		ok=false;
	gzclose(gz);
	if(!ok)
	{
		fprintf(stderr,"aodix: '%s': compressed project could not be read\n",path);
		fclose(tmp);
		return NULL;
	}
	rewind(tmp);
	return tmp;
}

FILE* adx_project_open_write(const char* path)
{
	if(adx_project_is_compressed_name(path))
		return tmpfile();
	return fopen(path,"wb");
}

bool adx_project_close_write(FILE* pfile,const char* path)
{
	if(!adx_project_is_compressed_name(path))
		return fclose(pfile)==0;

	// compress into "<name>.tmp", then replace the file (an existing project is not lost if writing fails)
	bool ok=fflush(pfile)==0;
	std::string const tmp_name=std::string(path)+".tmp";
	gzFile gz=ok?gzopen(tmp_name.c_str(),"wb6"):NULL;
	if(gz!=NULL)
	{
		rewind(pfile);
		std::vector<char> buf(1<<16);
		size_t n;
		while(ok && (n=fread(buf.data(),1,buf.size(),pfile))>0)
			ok=gzwrite(gz,buf.data(),(unsigned)n)==(int)n;
		ok=(gzclose(gz)==Z_OK) && ok;
		if(ok)
			ok=rename(tmp_name.c_str(),path)==0;
		if(!ok)
			unlink(tmp_name.c_str());
	}
	else
		ok=false;
	fclose(pfile);
	if(!ok)
		fprintf(stderr,"aodix: '%s': project could not be written\n",path);
	return ok;
}
