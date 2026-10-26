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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main window placement (stored in physical pixels, so it fits after changing the ui scaling)
static int geom_get(const unsigned char* g,int i)
{
	return (int16_t)(uint16_t)(g[i*2] | (g[i*2+1]<<8));
}

static void geom_set(unsigned char* g,int i,int v)
{
	v=v<-32768?-32768:(v>32767?32767:v);
	g[i*2]=(unsigned char)(v & 0xFF);
	g[i*2+1]=(unsigned char)((v>>8) & 0xFF);
}

bool CAodixCore::window_placement_restore(int* x,int* y,int* w,int* h)
{
	// returns true for a normal (not maximized) window; x/y/w/h get the stored geometry in any case (if known)
	if(cfg.win_state!=1 && cfg.win_state!=2)
		return false;
	int px=geom_get(cfg.win_geom,0);
	int py=geom_get(cfg.win_geom,1);
	int pw=geom_get(cfg.win_geom,2);
	int ph=geom_get(cfg.win_geom,3);
	if(pw<200 || ph<150)
		return false;

	// keep the window on the screen (e.g. after removing a monitor)
	int sw=0,sh=0;
	adxplat::screen_size_physical(&sw,&sh);
	if(sw>0 && sh>0)
	{
		if(pw>sw) pw=sw;
		if(ph>sh) ph=sh;
		if(px>sw-100 || py>sh-100 || px+pw<100 || py<-50)
		{
			px=(sw-pw)/2;
			py=(sh-ph)/2;
		}
	}

	*x=adxplat::ui_scale_to_logical(px);
	*y=adxplat::ui_scale_to_logical(py);
	*w=adxplat::ui_scale_to_logical(pw);
	*h=adxplat::ui_scale_to_logical(ph);
	return cfg.win_state==1;
}

void CAodixCore::window_placement_save(HWND hwnd)
{
	if(cfg.fullscreen)
		return;
	int x,y,w,h;
	bool maximized=false;
	if(!adxplat::get_window_placement(hwnd,&x,&y,&w,&h,&maximized))
	{
		// never seen in normal state: keep the old geometry, remember the maximized state only
		if(cfg.win_state==1 || cfg.win_state==2)
			cfg.win_state=2;
		return;
	}
	geom_set(cfg.win_geom,0,x);
	geom_set(cfg.win_geom,1,y);
	geom_set(cfg.win_geom,2,w);
	geom_set(cfg.win_geom,3,h);
	cfg.win_state=maximized?2:1;
}
