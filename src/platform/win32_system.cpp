/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer: files, paths, critical sections, file choosers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "win32_internal.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>

#include <string>
#include <vector>
#include <algorithm>

bool builtin_file_dialog(HWND owner,const char* title,char* path,int path_size,const std::vector<std::pair<std::string,std::string>>& filters,bool save,bool folder,const char* default_ext);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// directory enumeration
struct FindHandle
{
	std::vector<WIN32_FIND_DATA>	entries;
	size_t							next;
};

// windows (ntfs) returns directory entries sorted by name; aodix builds its plugin menus in that order
void* FindFirstFile(LPCSTR pattern,WIN32_FIND_DATA* data)
{
	std::string p(pattern?pattern:"");
	for(char& c:p)
		if(c=='\\')
			c='/';

	// strip the wildcard component ("dir/*.*")
	size_t const slash=p.find_last_of('/');
	std::string dir=(slash==std::string::npos)?".":p.substr(0,slash);
	if(dir.empty())
		dir="/";

	DIR* d=opendir(dir.c_str());
	if(!d)
		return INVALID_HANDLE_VALUE;

	FindHandle* h=new FindHandle();
	h->next=0;

	while(struct dirent* e=readdir(d))
	{
		WIN32_FIND_DATA fd;
		memset(&fd,0,sizeof(fd));
		strncpy(fd.cFileName,e->d_name,MAX_PATH-1);
		std::string const full=dir+"/"+e->d_name;
		struct stat st;
		if(stat(full.c_str(),&st)==0 && S_ISDIR(st.st_mode))
			fd.dwFileAttributes=FILE_ATTRIBUTE_DIRECTORY;
		h->entries.push_back(fd);
	}
	closedir(d);

	std::sort(h->entries.begin(),h->entries.end(),[](const WIN32_FIND_DATA& a,const WIN32_FIND_DATA& b){return strcasecmp(a.cFileName,b.cFileName)<0;});

	if(h->entries.empty())
	{
		delete h;
		return INVALID_HANDLE_VALUE;
	}

	*data=h->entries[h->next++];
	return h;
}

BOOL FindNextFile(void* hfind,WIN32_FIND_DATA* data)
{
	if(!hfind || hfind==INVALID_HANDLE_VALUE)
		return FALSE;
	FindHandle* h=(FindHandle*)hfind;
	if(h->next>=h->entries.size())
		return FALSE;
	*data=h->entries[h->next++];
	return TRUE;
}

BOOL FindClose(void* hfind)
{
	if(!hfind || hfind==INVALID_HANDLE_VALUE)
		return FALSE;
	delete (FindHandle*)hfind;
	return TRUE;
}

short GetFileTitle(LPCSTR file,LPSTR title,WORD max)
{
	if(!file || !title || max==0)
		return -1;

	// accept both windows (old projects) and unix separators
	const char* p=file;
	for(const char* s=file;*s;s++)
		if(*s=='/' || *s=='\\')
			p=s+1;

	strncpy(title,p,max-1);
	title[max-1]=0;
	return 0;
}

DWORD GetModuleFileName(HMODULE,LPSTR filename,DWORD size)
{
	ssize_t const n=readlink("/proc/self/exe",filename,size-1);
	if(n<=0)
	{
		filename[0]=0;
		return 0;
	}
	filename[n]=0;
	return (DWORD)n;
}

BOOL CreateDirectory(LPCSTR path,void*)
{
	return mkdir(path,0755)==0;
}

HRESULT CoInitialize(LPVOID)
{
	return 0;
}

void CoUninitialize(void)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// critical sections
void InitializeCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&cs->mutex,&a);
	pthread_mutexattr_destroy(&a);
}

void DeleteCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutex_destroy(&cs->mutex);
}

void EnterCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutex_lock(&cs->mutex);
}

void LeaveCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutex_unlock(&cs->mutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// paths
const char* adxplat::exe_dir(void)
{
	static char dir[PATH_MAX]={0};
	if(!dir[0])
	{
		ssize_t const n=readlink("/proc/self/exe",dir,sizeof(dir)-1);
		if(n>0)
		{
			dir[n]=0;
			char* s=strrchr(dir,'/');
			if(s)
				*s=0;
		}
		else
			strcpy(dir,".");
	}
	return dir;
}

static void mkdir_p(const std::string& path)
{
	std::string cur;
	for(size_t i=0;i<path.size();i++)
	{
		cur+=path[i];
		if(path[i]=='/' && cur.size()>1)
			mkdir(cur.c_str(),0755);
	}
	mkdir(path.c_str(),0755);
}

const char* adxplat::config_dir(void)
{
	static std::string dir;
	if(dir.empty())
	{
		const char* xdg=getenv("XDG_CONFIG_HOME");
		const char* home=getenv("HOME");
		if(xdg && xdg[0])
			dir=std::string(xdg)+"/aodix";
		else if(home && home[0])
			dir=std::string(home)+"/.config/aodix";
		else
			dir="/tmp/aodix";
		mkdir_p(dir);
	}
	return dir.c_str();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// external programs
static std::string shell_quote(const std::string& s)
{
	std::string o="'";
	for(char c:s)
	{
		if(c=='\'')
			o+="'\\''";
		else
			o+=c;
	}
	o+="'";
	return o;
}

static bool have_program(const char* name)
{
	const char* path=getenv("PATH");
	if(!path)
		return false;
	std::string p(path);
	size_t start=0;
	while(start<=p.size())
	{
		size_t const end=p.find(':',start);
		std::string const dir=p.substr(start,end==std::string::npos?std::string::npos:end-start);
		if(!dir.empty())
		{
			std::string const full=dir+"/"+name;
			if(access(full.c_str(),X_OK)==0)
				return true;
		}
		if(end==std::string::npos)
			break;
		start=end+1;
	}
	return false;
}

void adxplat::open_url(const char* url)
{
	if(!url)
		return;
	std::string const cmd="xdg-open "+shell_quote(url)+" >/dev/null 2>&1 &";
	if(system(cmd.c_str())!=0)
		fprintf(stderr,"aodix: could not open '%s'\n",url);
}

// run a command and capture its first output line; returns the exit status (-1 on failure)
static int run_capture(const std::string& cmd,std::string& out)
{
	out.clear();
	FILE* p=popen(cmd.c_str(),"r");
	if(!p)
		return -1;
	char buf[4096];
	while(fgets(buf,sizeof(buf),p))
		out+=buf;
	int const st=pclose(p);
	while(!out.empty() && (out.back()=='\n' || out.back()=='\r'))
		out.pop_back();
	if(WIFEXITED(st))
		return WEXITSTATUS(st);
	return -1;
}

// parse a windows filter string "Desc\0*.a;*.b\0Desc2\0*.c\0\0"
static std::vector<std::pair<std::string,std::string>> parse_filter(const char* filter)
{
	std::vector<std::pair<std::string,std::string>> r;
	if(!filter)
		return r;
	const char* p=filter;
	while(*p)
	{
		std::string const desc=p;
		p+=desc.size()+1;
		if(!*p)
			break;
		std::string const pat=p;
		p+=pat.size()+1;
		r.push_back({desc,pat});
	}
	return r;
}

static std::string zenity_patterns(const std::string& pat)
{
	std::string o;
	size_t s=0;
	while(s<=pat.size())
	{
		size_t const e=pat.find(';',s);
		std::string const one=pat.substr(s,e==std::string::npos?std::string::npos:e-s);
		if(!one.empty())
		{
			if(!o.empty())
				o+=' ';
			o+=one;
			// also accept upper case extensions
			std::string up=one;
			for(char& c:up) if(c>='a' && c<='z') c-=32;
			if(up!=one)
			{
				o+=' ';
				o+=up;
			}
		}
		if(e==std::string::npos)
			break;
		s=e+1;
	}
	return o;
}

static bool g_external_choosers_disabled=false;

bool adxplat::choose_file(HWND owner,const char* title,char* path,int path_size,const char* filter,bool save,const char* default_ext,const char* initial_dir)
{
	auto filters=parse_filter(filter);

	// initial file name / directory
	std::string start;
	std::string name=path?path:"";
	if(name.find('*')!=std::string::npos)
		name.clear();
	if(initial_dir && initial_dir[0])
		start=std::string(initial_dir)+"/"+name;
	else if(!name.empty() && name[0]=='/')
		start=name;
	else
	{
		char cwd[PATH_MAX];
		if(getcwd(cwd,sizeof(cwd)))
			start=std::string(cwd)+"/"+name;
	}

	if(!g_external_choosers_disabled && !getenv("AODIX_BUILTIN_FILE_DIALOG"))
	{
		std::string cmd;
		if(have_program("zenity"))
		{
			cmd="zenity --file-selection --title="+shell_quote(title?title:"");
			if(save)
				cmd+=" --save --confirm-overwrite";
			if(!start.empty())
				cmd+=" --filename="+shell_quote(start);
			for(auto& f:filters)
				cmd+=" --file-filter="+shell_quote(f.first+" | "+zenity_patterns(f.second));
			cmd+=" 2>/dev/null";
		}
		else if(have_program("kdialog"))
		{
			std::string kf;
			for(auto& f:filters)
			{
				if(!kf.empty())
					kf+="\n";
				std::string pats=f.second;
				for(char& c:pats) if(c==';') c=' ';
				kf+=pats+"|"+f.first;
			}
			cmd=std::string("kdialog ")+(save?"--getsavefilename ":"--getopenfilename ")+shell_quote(start.empty()?".":start)+" "+shell_quote(kf)+" --title "+shell_quote(title?title:"")+" 2>/dev/null";
		}

		if(!cmd.empty())
		{
			// keep repainting the application while the external dialog is open is not possible
			// (the call blocks), so flush everything first
			flush_all_surfaces();
			std::string out;
			int const st=run_capture(cmd,out);
			if(st==0 && !out.empty())
			{
				// append default extension when saving
				if(save && default_ext && default_ext[0])
				{
					size_t const slash=out.find_last_of('/');
					std::string const base=out.substr(slash==std::string::npos?0:slash+1);
					if(base.find('.')==std::string::npos)
						out+=std::string(".")+default_ext;
				}
				strncpy(path,out.c_str(),path_size-1);
				path[path_size-1]=0;
				return true;
			}
			if(st==1 || st==0)
				return false;	// cancelled
			// any other status: fall back to the built-in dialog
		}
	}

	if(path && name.empty() && !start.empty())
	{
		strncpy(path,start.c_str(),path_size-1);
		path[path_size-1]=0;
	}
	else if(path && !start.empty())
	{
		strncpy(path,start.c_str(),path_size-1);
		path[path_size-1]=0;
	}
	return builtin_file_dialog(owner,title,path,path_size,filters,save,false,default_ext);
}

bool adxplat::choose_folder(HWND owner,const char* title,char* path,int path_size)
{
	if(!g_external_choosers_disabled && !getenv("AODIX_BUILTIN_FILE_DIALOG"))
	{
		std::string cmd;
		std::string const start=(path && path[0])?path:(getenv("HOME")?getenv("HOME"):"/");
		if(have_program("zenity"))
			cmd="zenity --file-selection --directory --title="+shell_quote(title?title:"")+" --filename="+shell_quote(start+"/")+" 2>/dev/null";
		else if(have_program("kdialog"))
			cmd="kdialog --getexistingdirectory "+shell_quote(start)+" --title "+shell_quote(title?title:"")+" 2>/dev/null";

		if(!cmd.empty())
		{
			flush_all_surfaces();
			std::string out;
			int const st=run_capture(cmd,out);
			if(st==0 && !out.empty())
			{
				strncpy(path,out.c_str(),path_size-1);
				path[path_size-1]=0;
				return true;
			}
			if(st==1 || st==0)
				return false;
		}
	}

	std::vector<std::pair<std::string,std::string>> none;
	return builtin_file_dialog(owner,title,path,path_size,none,false,true,nullptr);
}
