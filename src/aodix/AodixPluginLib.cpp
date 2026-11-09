/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - plugin library: scanning the plugin folders, the plugins.cfg cache and the library menu
//
// scanning every plugin at each start is slow, above all with yabridge (every windows plugin starts wine). the scan
// result is written to plugins.cfg next to Aodix.cfg and read at the next start instead. "Rescan Plugins" in the
// library menu (or changed plugin folders in the configuration, or a missing plugins.cfg) scans again.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <strings.h>

namespace
{

// one plugin of the library
struct LibEntry
{
	int			section;	// 0 = vst3, 1 = clap, 2 = lv2, 3 = ladspa
	std::string	menu;		// sub menus, separated by '/' (folders, bundles with several plugins)
	std::string	name;
	int32_t		id;
	std::string	path;
};

const char* const g_section_name[4]={"vst3","clap","lv2","ladspa"};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// scanning

// one plugin folder (recursive). mode: 0 = vst3, 1 = ladspa, 2 = clap
void scan_folder(std::vector<LibEntry>& out,int section,const char* folder,std::string const& menu,int mode)
{
	char findpath[_MAX_PATH];
	snprintf(findpath,sizeof(findpath),"%s/*.*",folder);

	WIN32_FIND_DATA ffd;
	void* const hfind=FindFirstFile(findpath,&ffd);
	if(hfind==INVALID_HANDLE_VALUE)
		return;

	do
	{
		if(ffd.cFileName[0]=='.')
			continue;

		bool const is_dir=(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)!=0;
		bool candidate=false;
		if(mode==1)
			candidate=!is_dir && adx_plugin_is_ladspa(ffd.cFileName);
		else if(mode==2)
			candidate=adx_plugin_is_clap(ffd.cFileName);
		else
			candidate=adx_plugin_is_candidate(ffd.cFileName,is_dir);

		char path[_MAX_PATH];
		snprintf(path,sizeof(path),"%s/%s",folder,ffd.cFileName);

		if(candidate)
		{
			// the plugin classes inside the bundle / library
			std::vector<AdxPluginDesc> descs;
			adx_plugin_scan(path,descs);

			// bundles with several plugins get a sub menu (the file name without extension)
			std::string sub=menu;
			if(descs.size()>1)
			{
				std::string label=ffd.cFileName;
				size_t const dot=label.find_last_of('.');
				if(dot!=std::string::npos)
					label.resize(dot);
				sub+=(sub.empty()?"":"/")+label;
			}
			for(AdxPluginDesc const& d:descs)
				out.push_back({section,sub,d.name,d.uniqueID,path});
		}
		else if(is_dir)
		{
			scan_folder(out,section,path,menu+(menu.empty()?"":"/")+ffd.cFileName,mode);
		}
	}
	while(FindNextFile(hfind,&ffd));

	FindClose(hfind);
}

void scan_all(ADX_CONFIG const& cfg,std::vector<LibEntry>& out)
{
	// vst3 folders of the configuration
	for(int pi=0;pi<MAX_VST_FOLDERS;pi++)
	{
		if(cfg.vst_path[pi][0]!=0)
		{
			char folder[_MAX_PATH];
			adx_expand_path(cfg.vst_path[pi],folder,_MAX_PATH);
			scan_folder(out,0,folder,"",0);
		}
	}

	// clap folders of the configuration
	for(int pi=0;pi<MAX_CLAP_FOLDERS;pi++)
	{
		if(cfg.clap_path[pi][0]!=0)
		{
			char folder[_MAX_PATH];
			adx_expand_path(cfg.clap_path[pi],folder,_MAX_PATH);
			scan_folder(out,1,folder,"",2);
		}
	}

	// lv2 (lilv knows the places), sorted by name; bundles with several plugins get a sub menu
	std::vector<AdxPluginDesc> lv2;
	adx_lv2_list(lv2);
	std::stable_sort(lv2.begin(),lv2.end(),[](const AdxPluginDesc& a,const AdxPluginDesc& b){return strcasecmp(a.name.c_str(),b.name.c_str())<0;});
	std::vector<std::string> done;
	for(size_t i=0;i<lv2.size();i++)
	{
		std::string const& bundle=lv2[i].path;
		if(std::find(done.begin(),done.end(),bundle)!=done.end())
			continue;
		done.push_back(bundle);

		std::vector<size_t> members;
		for(size_t j=i;j<lv2.size();j++)
			if(lv2[j].path==bundle)
				members.push_back(j);

		std::string sub;
		if(members.size()>1)
		{
			sub=bundle.substr(bundle.find_last_of('/')+1);
			if(sub.size()>4 && sub.compare(sub.size()-4,4,".lv2")==0)
				sub.resize(sub.size()-4);
		}
		for(size_t m:members)
			out.push_back({2,sub,lv2[m].name,lv2[m].uniqueID,lv2[m].path});
	}

	// ladspa folders (LADSPA_PATH or the standard ones)
	std::vector<std::string> ladspa_dirs;
	adx_ladspa_paths(ladspa_dirs);
	for(std::string const& d:ladspa_dirs)
		scan_folder(out,3,d.c_str(),"",1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// plugins.cfg: a text file, one plugin per line, fields separated by tabs

std::string lib_file_path(void)
{
	return std::string(adxplat::config_dir())+"/plugins.cfg";
}

std::string clean(std::string s)
{
	for(char& c:s)
		if(c=='\t' || c=='\n' || c=='\r')
			c=' ';
	return s;
}

bool write_lib_file(std::vector<LibEntry> const& lib)
{
	std::string const fn=lib_file_path();
	std::string const tmp=fn+".tmp";
	FILE* f=fopen(tmp.c_str(),"w");
	if(!f)
		return false;
	fprintf(f,"# Aodix plugin library: the result of the plugin scan, read at start instead of scanning again.\n");
	fprintf(f,"# Rescan: \"Rescan Plugins\" at the end of the plugin library menu, or delete this file.\n");
	fprintf(f,"# One plugin per line, fields separated by tabs:\n");
	fprintf(f,"# format <tab> menu (sub menus separated by /) <tab> name <tab> unique id <tab> plugin path\n");
	for(LibEntry const& e:lib)
		fprintf(f,"%s\t%s\t%s\t%d\t%s\n",g_section_name[e.section],clean(e.menu).c_str(),clean(e.name).c_str(),(int)e.id,clean(e.path).c_str());
	bool const ok=(fclose(f)==0);
	if(!ok || rename(tmp.c_str(),fn.c_str())!=0)
	{
		remove(tmp.c_str());
		return false;
	}
	return true;
}

bool read_lib_file(std::vector<LibEntry>& lib)
{
	FILE* f=fopen(lib_file_path().c_str(),"r");
	if(!f)
		return false;
	std::string line;
	char buf[4096];
	int line_nr=0;
	while(fgets(buf,sizeof(buf),f))
	{
		line=buf;
		line_nr++;
		while(!line.empty() && (line.back()=='\n' || line.back()=='\r'))
			line.pop_back();
		if(line.empty() || line[0]=='#')
			continue;

		std::vector<std::string> fld;
		size_t start=0;
		for(;;)
		{
			size_t const tab=line.find('\t',start);
			fld.push_back(line.substr(start,tab==std::string::npos?std::string::npos:tab-start));
			if(tab==std::string::npos)
				break;
			start=tab+1;
		}
		int section=-1;
		for(int s=0;s<4;s++)
			if(fld.size()==5 && fld[0]==g_section_name[s])
				section=s;
		if(section<0 || fld[2].empty() || fld[4].empty())
		{
			fprintf(stderr,"aodix: %s:%d: not a plugin entry, ignored\n",lib_file_path().c_str(),line_nr);
			continue;
		}
		lib.push_back({section,fld[1],fld[2],(int32_t)strtol(fld[3].c_str(),nullptr,10),fld[4]});
	}
	fclose(f);
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// menu: items in the order of the scan, sub menus where an entry's menu path says so (32 items per column)

struct MenuNode
{
	std::string				label;
	std::vector<MenuNode>	subs;
	std::vector<int>		order;	// >= 0: entry index, < 0: -(sub menu index + 1)
};

void tree_insert(MenuNode& root,std::string const& menu,int entry)
{
	MenuNode* node=&root;
	size_t start=0;
	while(start<menu.size())
	{
		size_t const sl=menu.find('/',start);
		std::string const seg=menu.substr(start,sl==std::string::npos?std::string::npos:sl-start);
		start=(sl==std::string::npos)?menu.size():sl+1;
		if(seg.empty())
			continue;
		size_t k=0;
		for(;k<node->subs.size();k++)
			if(node->subs[k].label==seg)
				break;
		if(k==node->subs.size())
		{
			node->subs.push_back({seg,{},{}});
			node->order.push_back(-int(k)-1);
		}
		node=&node->subs[k];
	}
	node->order.push_back(entry);
}

}	// namespace

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_vst_build_lib(bool const rescan)
{
	// plugin list: from plugins.cfg, or scan (and write plugins.cfg)
	std::vector<LibEntry> lib;
	if(rescan || !read_lib_file(lib))
	{
		SetCursor(hcursor_wait);
		fprintf(stderr,"aodix: scanning plugins ...\n");
		lib.clear();
		scan_all(cfg,lib);
		if(!write_lib_file(lib))
			fprintf(stderr,"aodix: could not write %s\n",lib_file_path().c_str());
		fprintf(stderr,"aodix: %d plugins found, list saved in %s\n",(int)lib.size(),lib_file_path().c_str());
		SetCursor(hcursor_arro);
	}

	// new menu
	if(hmenu_vst_lib!=NULL)
		DestroyMenu(hmenu_vst_lib);
	hmenu_vst_lib=CreatePopupMenu();
	vst_lib.num_dlls=0;

	MenuNode roots[4];
	for(size_t i=0;i<lib.size();i++)
		tree_insert(roots[lib[i].section],lib[i].menu,int(i));

	// fills a menu with a node (recursive); returns the number of items
	std::function<int(HMENU,MenuNode&)> fill=[&](HMENU hmenu,MenuNode& node)->int
	{
		int count=0;
		for(int o:node.order)
		{
			UINT const brk=(count>0 && (count&0x1F)==0)?MF_MENUBARBREAK:0;
			if(o<0)
			{
				MenuNode& sub=node.subs[size_t(-o-1)];
				HMENU const hsub=CreatePopupMenu();
				if(fill(hsub,sub)>0)
				{
					AppendMenu(hmenu,MFT_STRING | MF_POPUP | brk,(UINT_PTR)hsub,sub.label.c_str());
					count++;
				}
				else
					DestroyMenu(hsub);
			}
			else if(vst_lib.num_dlls<MAX_VST_LIB_ENTRIES)
			{
				LibEntry const& e=lib[size_t(o)];
				AppendMenu(hmenu,MFT_STRING | brk,IDM_VST_LIB+vst_lib.num_dlls,e.name.c_str());
				snprintf(vst_lib.dll_path[vst_lib.num_dlls],_MAX_PATH,"%s",e.path.c_str());
				vst_lib.dll_id[vst_lib.num_dlls]=e.id;
				vst_lib.num_dlls++;
				count++;
			}
		}
		return count;
	};

	// vst3 at the top, clap, lv2 and ladspa in sub menus (like before: a separator after the vst3 plugins)
	int const num_vst3=fill(hmenu_vst_lib,roots[0]);
	const char* const labels[4]={"","CLAP","LV2","LADSPA"};
	bool separated=false;
	for(int s=1;s<4;s++)
	{
		HMENU const hsub=CreatePopupMenu();
		if(fill(hsub,roots[s])>0)
		{
			if(num_vst3>0 && !separated)
				AppendMenu(hmenu_vst_lib,MF_SEPARATOR,0,NULL);
			separated=true;
			AppendMenu(hmenu_vst_lib,MFT_STRING | MF_POPUP,(UINT_PTR)hsub,labels[s]);
		}
		else
			DestroyMenu(hsub);
	}

	// rescan
	if(num_vst3>0 || separated)
		AppendMenu(hmenu_vst_lib,MF_SEPARATOR,0,NULL);
	AppendMenu(hmenu_vst_lib,MFT_STRING,IDM_VST_RESCAN,"Rescan Plugins");
}
