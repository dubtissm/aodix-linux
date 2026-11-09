/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - configuration file
//
// the configuration (~/.config/aodix/Aodix.cfg) is a text file with one "name = value" entry per line, so it can be
// read and edited by hand. entries that are missing keep their defaults, unknown entries are reported and ignored.
// the binary file of older versions (the ADX_CONFIG structure as it was in memory) is read once, kept as
// Aodix.cfg.old and replaced by the text file.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <strings.h>

namespace
{

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window geometry is kept as int16 little endian in ADX_CONFIG.win_geom (see AodixLinux.cpp)
int geom_get(const unsigned char* g,int i)
{
	return (int16_t)(uint16_t)(g[i*2] | (g[i*2+1]<<8));
}

void geom_set(unsigned char* g,int i,int v)
{
	v=v<-32768?-32768:(v>32767?32767:v);
	g[i*2]=(unsigned char)(v & 0xFF);
	g[i*2+1]=(unsigned char)((v>>8) & 0xFF);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// value parsing
bool parse_bool(const std::string& v,bool* out)
{
	const char* s=v.c_str();
	if(!strcasecmp(s,"yes") || !strcasecmp(s,"true") || !strcasecmp(s,"on") || !strcmp(s,"1"))		{ *out=true; return true; }
	if(!strcasecmp(s,"no") || !strcasecmp(s,"false") || !strcasecmp(s,"off") || !strcmp(s,"0"))	{ *out=false; return true; }
	return false;
}

bool parse_int(const std::string& v,int* out)
{
	if(v.empty())
		return false;
	char* end=NULL;
	long const n=strtol(v.c_str(),&end,10);
	if(end==NULL || *end!=0)
		return false;
	*out=(int)n;
	return true;
}

void set_text(char* dst,size_t size,const std::string& v)
{
	snprintf(dst,size,"%s",v.c_str());
}

std::string trim(const std::string& s)
{
	size_t b=0,e=s.size();
	while(b<e && isspace((unsigned char)s[b])) b++;
	while(e>b && isspace((unsigned char)s[e-1])) e--;
	return s.substr(b,e-b);
}

// "name_<n>" with n in 1..count: returns n-1, otherwise -1
int numbered(const std::string& key,const char* name,int count)
{
	size_t const l=strlen(name);
	if(key.size()<=l+1 || key.compare(0,l,name)!=0 || key[l]!='_')
		return -1;
	int n=0;
	if(!parse_int(key.substr(l+1),&n) || n<1 || n>count)
		return -1;
	return n-1;
}

const char* yes_no(int v)
{
	return v?"yes":"no";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// configuration file of older versions: the ADX_CONFIG structure, as written by fwrite
void read_binary(ADX_CONFIG& cfg,FILE* pfile,const char* cfg_fil)
{
	// configuration files of older versions end before the clap paths: those keep their defaults
	size_t const got=fread(&cfg,1,sizeof(ADX_CONFIG),pfile);
	if(got<offsetof(ADX_CONFIG,clap_path))
		fprintf(stderr,"aodix: configuration file '%s' is incomplete\n",cfg_fil);

	// file of an older version (it may even contain the structure padding that now overlaps the first clap path)
	if(got<offsetof(ADX_CONFIG,theme_path))
	{
		for(int pi=0;pi<MAX_CLAP_FOLDERS;pi++)
			cfg.clap_path[pi][0]=0;
		strcpy(cfg.clap_path[0],"~/.clap");
		strcpy(cfg.clap_path[1],"/usr/lib/clap");
		strcpy(cfg.clap_path[2],"/usr/local/lib/clap");

		// vst paths in the home folder are shown as ~/... from now on
		for(int pi=0;pi<MAX_VST_FOLDERS;pi++)
		{
			char compact[_MAX_PATH];
			adx_compact_path(cfg.vst_path[pi],compact,_MAX_PATH);
			strcpy(cfg.vst_path[pi],compact);
		}
	}

	// file without theme path (before 4.2.1.6)
	if(got<offsetof(ADX_CONFIG,project_path))
		strcpy(cfg.theme_path,"Themes/Classic.theme");

	// file without project path (before 4.2.1.7)
	if(got<sizeof(ADX_CONFIG) || cfg.project_path[0]==0)
		adx_default_project_path(cfg.project_path,_MAX_PATH);

	// strings of the binary structure may lack their terminating zero
	cfg.user_name[sizeof(cfg.user_name)-1]=0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// one "name = value" entry of the text file; returns false for an unknown name or a bad value (*bad_value tells which)
bool read_entry(ADX_CONFIG& cfg,const std::string& key,const std::string& v,bool* bad_value)
{
	bool b=false;
	int n=0,i=-1;
	*bad_value=false;

#define ADX_BOOL(name,field)	if(key==name) { if(parse_bool(v,&b)) field=b?1:0; else *bad_value=true; return !*bad_value; }
#define ADX_TEXT(name,field)	if(key==name) { set_text(field,sizeof(field),v); return true; }

	// user interface
	ADX_TEXT("user_name",cfg.user_name)
	if(key=="keyboard_layout")
	{
		if(!strcasecmp(v.c_str(),"qwerty"))			cfg.keyboard_layout=0;
		else if(!strcasecmp(v.c_str(),"azerty"))	cfg.keyboard_layout=1;
		else if(!strcasecmp(v.c_str(),"qwertz"))	cfg.keyboard_layout=2;
		else										*bad_value=true;
		return !*bad_value;
	}
	if(key=="ui_scale")
	{
		std::string s=v;
		if(!s.empty() && s.back()=='%')
			s.pop_back();
		if(s=="100")		cfg.ui_scale=0;
		else if(s=="150")	cfg.ui_scale=1;
		else if(s=="200")	cfg.ui_scale=2;
		else				*bad_value=true;
		return !*bad_value;
	}
	ADX_BOOL("fullscreen",cfg.fullscreen)
	ADX_TEXT("skin_path",cfg.skin_path)
	ADX_TEXT("theme_path",cfg.theme_path)
	ADX_TEXT("project_path",cfg.project_path)
	ADX_BOOL("note_colors",cfg.note_colors)
	if(key=="event_flash_ms")
	{
		if(parse_int(v,&n) && n>=0)
			cfg.flash_time=(unsigned char)adx_flash_time_stored(n);
		else
			*bad_value=true;
		return !*bad_value;
	}
	ADX_BOOL("instance_autolink",cfg.instance_autolink)
	ADX_BOOL("stop_wrap",cfg.stop_wrap)
	ADX_BOOL("live_recording",cfg.rec_live)

	// main window
	if(key=="window_state")
	{
		if(!strcasecmp(v.c_str(),"default"))			cfg.win_state=0;
		else if(!strcasecmp(v.c_str(),"normal"))		cfg.win_state=1;
		else if(!strcasecmp(v.c_str(),"maximized"))		cfg.win_state=2;
		else											*bad_value=true;
		return !*bad_value;
	}
	const char* const geom_keys[4]={"window_x","window_y","window_width","window_height"};
	for(int g=0;g<4;g++)
	{
		if(key==geom_keys[g])
		{
			if(parse_int(v,&n))
				geom_set(cfg.win_geom,g,n);
			else
				*bad_value=true;
			return !*bad_value;
		}
	}

	// audio
	if(key=="audio_driver")
	{
		if(!strcasecmp(v.c_str(),"jack"))			cfg.audio_driver_id=0;
		else if(!strcasecmp(v.c_str(),"none"))		cfg.audio_driver_id=-1;
		else										*bad_value=true;
		return !*bad_value;
	}
	if(key=="audio_sample_rate")
	{
		if(parse_int(v,&n) && n>0)
			cfg.audio_driver_sample_rate=n;
		else
			*bad_value=true;
		return !*bad_value;
	}
	if(key=="audio_ports")
	{
		if(parse_int(v,&n) && (n==2 || n==4 || n==8 || n==16 || n==32))
			cfg.audio_num_ports=(unsigned char)n;
		else
			*bad_value=true;
		return !*bad_value;
	}
	if(key=="pipewire_buffer_size" || key=="pipewire_sample_rate")
	{
		bool const buf=(key=="pipewire_buffer_size");
		int stored=-1;
		if(!strcasecmp(v.c_str(),"default"))
			stored=1;
		else if(parse_int(v,&n) && n>0)
			stored=buf?audio_pw_buffer_stored(n):audio_pw_rate_stored(n);
		if(stored<0)
		{
			*bad_value=true;
			return false;
		}
		if(buf)
			cfg.pw_buffer_size=(unsigned char)stored;
		else
			cfg.pw_sample_rate=(unsigned char)stored;
		return true;
	}
	if(key=="dsp_threads")
	{
		if(!strcasecmp(v.c_str(),"auto"))
			cfg.dsp_threads=0;
		else if(parse_int(v,&n) && n>=1 && n<=64)
			cfg.dsp_threads=(unsigned char)n;
		else
			*bad_value=true;
		return !*bad_value;
	}
	if(key=="audio_input_ports" || key=="audio_output_ports")
	{
		// jack port of every aodix input / output, in order (port numbers counted from 1)
		bool const in=(key=="audio_input_ports");
		int* pins=in?cfg.audio_input_pin:cfg.audio_output_pin;
		int const num=in?NUM_DSP_INPUTS:NUM_DSP_OUTPUTS;
		const char* s=v.c_str();
		for(int p=0;p<num && *s;p++)
		{
			char* end=NULL;
			long const port=strtol(s,&end,10);
			if(end==s || port<1 || port>32)
			{
				*bad_value=true;
				return false;
			}
			pins[p]=(int)port-1;
			s=end;
			while(*s==' ' || *s=='\t' || *s==',')
				s++;
		}
		return true;
	}

	// midi input
	ADX_BOOL("midi_input_open",cfg.midi_in_dv_open)
	ADX_BOOL("midi_input_override_channel",cfg.midi_in_ch_rout)
	ADX_BOOL("midi_input_override_velocity",cfg.midi_in_vl_rout)

	// plugin folders and recent files
	if((i=numbered(key,"vst3_path",MAX_VST_FOLDERS))>=0)		{ set_text(cfg.vst_path[i],_MAX_PATH,v); return true; }
	if((i=numbered(key,"clap_path",MAX_CLAP_FOLDERS))>=0)		{ set_text(cfg.clap_path[i],_MAX_PATH,v); return true; }
	if((i=numbered(key,"recent_file",NUM_RECENT_FILES))>=0)		{ set_text(cfg.recent_file[i],_MAX_PATH,v); return true; }

#undef ADX_BOOL
#undef ADX_TEXT

	return false;
}

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::config_read(void)
{
	// init reserved fields
	for(int r=0;r<_MAX_PATH;r++)
		cfg.reserved[r]=0;

	// init skin path (relative paths are resolved against the program directory)
	strcpy(cfg.skin_path, "Skins/Blue");

	// init config vst path(s)
	for(int pi=0;pi<MAX_VST_FOLDERS;pi++)
		cfg.vst_path[pi][0]=0;

	// linux port: default to the standard vst3 and clap locations
	strcpy(cfg.vst_path[0],"~/.vst3");
	strcpy(cfg.vst_path[1],"/usr/lib/vst3");
	strcpy(cfg.vst_path[2],"/usr/local/lib/vst3");
	for(int pi=0;pi<MAX_CLAP_FOLDERS;pi++)
		cfg.clap_path[pi][0]=0;
	strcpy(cfg.clap_path[0],"~/.clap");
	strcpy(cfg.clap_path[1],"/usr/lib/clap");
	strcpy(cfg.clap_path[2],"/usr/local/lib/clap");
	strcpy(cfg.theme_path,"Themes/Classic.theme");
	adx_default_project_path(cfg.project_path,_MAX_PATH);

	// init recent file paths
	for(int rf=0;rf<NUM_RECENT_FILES;rf++)
		cfg.recent_file[rf][0]=0;

	// init user gui config values
	cfg.instance_autolink=0;
	cfg.rec_live=1;
	cfg.stop_wrap=0;
	cfg.keyboard_layout=0;
	cfg.fullscreen = 0;

	// init config audio driver (linux port: 0 = jack)
	cfg.audio_driver_id=0;
	cfg.audio_driver_sample_rate=44100.0;

	// init midi in config
	cfg.midi_in_ch_rout=0;
	cfg.midi_in_vl_rout=0;
	cfg.midi_in_dv_open=1;

	// init display settings
	cfg.note_colors=0;

	// linux port: jack ports (in and out)
	cfg.audio_num_ports=2;

	// linux port: no ui scaling (100%)
	cfg.ui_scale=0;

	// linux port: automatic number of dsp threads
	cfg.dsp_threads=0;

	// linux port: pipewire defaults (256 samples, 48000 hz), window placement unknown, default flash time
	cfg.pw_buffer_size=0;
	cfg.pw_sample_rate=0;
	cfg.win_state=0;
	memset(cfg.win_geom,0,sizeof(cfg.win_geom));
	cfg.flash_time=0;

	// init reserved fields 2
	for(int r=0;r<_countof(cfg.reserved2);r++)
		cfg.reserved2[r]=0;

	// init user registration
	sprintf(cfg.user_name,"Aodix User");
	memset(cfg.user_reserved,0,32);

	// reset asio input pins
	for(int i=0;i<NUM_DSP_INPUTS;i++)
		cfg.audio_input_pin[i]=i;

	// reset asio output pins
	for(int o=0;o<NUM_DSP_OUTPUTS;o++)
		cfg.audio_output_pin[o]=o;

	// open config file
	FILE* pfile=fopen(cfg_fil,"rb");

	// write default config is not existing cfg file
	if(pfile==NULL)
	{
		config_write();
		return;
	}

	// the binary file of older versions starts with the (zeroed) reserved field, a text file never contains zeros
	unsigned char head[64];
	size_t const head_len=fread(head,1,sizeof(head),pfile);
	rewind(pfile);
	if(head_len>0 && memchr(head,0,head_len)!=NULL)
	{
		read_binary(cfg,pfile,cfg_fil);
		fclose(pfile);

		// keep the old file, write the text file
		char old_fil[_MAX_PATH+8];
		snprintf(old_fil,sizeof(old_fil),"%s.old",cfg_fil);
		if(rename(cfg_fil,old_fil)==0)
			fprintf(stderr,"aodix: configuration converted to text, the old file is kept as '%s'\n",old_fil);
		config_write();
	}
	else
	{
		// text file: name = value
		char line[4096];
		int line_num=0;
		while(fgets(line,sizeof(line),pfile)!=NULL)
		{
			line_num++;
			std::string const l=trim(line);
			if(l.empty() || l[0]=='#' || l[0]==';')
				continue;
			size_t const eq=l.find('=');
			if(eq==std::string::npos)
			{
				fprintf(stderr,"aodix: %s:%d: line without '=' ignored\n",cfg_fil,line_num);
				continue;
			}
			std::string const key=trim(l.substr(0,eq));
			std::string const value=trim(l.substr(eq+1));
			bool bad_value=false;
			if(!read_entry(cfg,key,value,&bad_value))
			{
				if(bad_value)
					fprintf(stderr,"aodix: %s:%d: invalid value '%s' for '%s', the default is kept\n",cfg_fil,line_num,value.c_str(),key.c_str());
				else
					fprintf(stderr,"aodix: %s:%d: unknown entry '%s' ignored\n",cfg_fil,line_num,key.c_str());
			}
		}
		fclose(pfile);
	}

	// linux port: windows style skin path from an older config
	for(char* pc=cfg.skin_path;*pc;pc++)
		if(*pc=='\\')
			*pc='/';
	if(cfg.project_path[0]==0)
		adx_default_project_path(cfg.project_path,_MAX_PATH);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::config_write(void)
{
	// written to a temporary file first: an interrupted write never destroys the configuration
	std::string const tmp=std::string(cfg_fil)+".tmp";
	FILE* pf=fopen(tmp.c_str(),"w");
	if(pf==NULL)
	{
		fprintf(stderr,"aodix: configuration file '%s' could not be written\n",cfg_fil);
		return;
	}

	static const char* const layouts[3]={"qwerty","azerty","qwertz"};
	static const char* const scales[3]={"100","150","200"};
	static const char* const win_states[3]={"default","normal","maximized"};

	fprintf(pf,"# Aodix configuration\n");
	fprintf(pf,"# one entry per line: name = value. lines starting with # are comments. missing entries keep their defaults.\n");
	fprintf(pf,"# Aodix writes this file when the configuration dialog is closed with OK and when it exits:\n");
	fprintf(pf,"# edit it while Aodix is not running.\n");

	fprintf(pf,"\n# user interface\n");
	fprintf(pf,"user_name = %s\n",cfg.user_name);
	fprintf(pf,"# qwerty, azerty or qwertz\n");
	fprintf(pf,"keyboard_layout = %s\n",layouts[(cfg.keyboard_layout>=0 && cfg.keyboard_layout<3)?cfg.keyboard_layout:0]);
	fprintf(pf,"# 100, 150 or 200 (percent)\n");
	fprintf(pf,"ui_scale = %s\n",scales[cfg.ui_scale<3?cfg.ui_scale:0]);
	fprintf(pf,"fullscreen = %s\n",yes_no(cfg.fullscreen));
	fprintf(pf,"# folders and files relative to the program folder or absolute (~ = home folder)\n");
	fprintf(pf,"skin_path = %s\n",cfg.skin_path);
	fprintf(pf,"# empty: the colors of the skin\n");
	fprintf(pf,"theme_path = %s\n",cfg.theme_path);
	fprintf(pf,"project_path = %s\n",cfg.project_path);
	fprintf(pf,"note_colors = %s\n",yes_no(cfg.note_colors));
	fprintf(pf,"# flash of events that start playing: 0 (off), 100, 200, 300, 400, 600, 800 or 1000\n");
	fprintf(pf,"event_flash_ms = %d\n",gui_flash_ms());
	fprintf(pf,"instance_autolink = %s\n",yes_no(cfg.instance_autolink));
	fprintf(pf,"stop_wrap = %s\n",yes_no(cfg.stop_wrap));
	fprintf(pf,"live_recording = %s\n",yes_no(cfg.rec_live));

	fprintf(pf,"\n# main window (written by aodix): default (maximized at the first start), normal or maximized;\n");
	fprintf(pf,"# position and size of the normal window in screen pixels\n");
	fprintf(pf,"window_state = %s\n",win_states[cfg.win_state<3?cfg.win_state:0]);
	fprintf(pf,"window_x = %d\n",geom_get(cfg.win_geom,0));
	fprintf(pf,"window_y = %d\n",geom_get(cfg.win_geom,1));
	fprintf(pf,"window_width = %d\n",geom_get(cfg.win_geom,2));
	fprintf(pf,"window_height = %d\n",geom_get(cfg.win_geom,3));

	fprintf(pf,"\n# audio: jack or none\n");
	fprintf(pf,"audio_driver = %s\n",cfg.audio_driver_id<0?"none":"jack");
	fprintf(pf,"# sample rate of the jack server (written by aodix)\n");
	fprintf(pf,"audio_sample_rate = %.0f\n",cfg.audio_driver_sample_rate);
	fprintf(pf,"# jack ports in and out: 2, 4, 8, 16 or 32\n");
	fprintf(pf,"audio_ports = %d\n",(int)cfg.audio_num_ports);
	fprintf(pf,"# requested from pipewire: default, 64, 128, 256, 512, 1024 or 2048 samples / default, 44100, 48000, 88200 or 96000 hz\n");
	int const frames=audio_pw_buffer_frames(cfg.pw_buffer_size);
	int const hz=audio_pw_rate_hz(cfg.pw_sample_rate);
	if(frames)	fprintf(pf,"pipewire_buffer_size = %d\n",frames);
	else		fprintf(pf,"pipewire_buffer_size = default\n");
	if(hz)		fprintf(pf,"pipewire_sample_rate = %d\n",hz);
	else		fprintf(pf,"pipewire_sample_rate = default\n");
	fprintf(pf,"# auto, or the number of dsp threads (1 = no parallel processing)\n");
	if(cfg.dsp_threads)	fprintf(pf,"dsp_threads = %d\n",(int)cfg.dsp_threads);
	else				fprintf(pf,"dsp_threads = auto\n");
	fprintf(pf,"# jack port (1..32) of every aodix input / output, in order\n");
	fprintf(pf,"audio_input_ports =");
	for(int i=0;i<NUM_DSP_INPUTS;i++)
		fprintf(pf," %d",cfg.audio_input_pin[i]+1);
	fprintf(pf,"\naudio_output_ports =");
	for(int o=0;o<NUM_DSP_OUTPUTS;o++)
		fprintf(pf," %d",cfg.audio_output_pin[o]+1);
	fprintf(pf,"\n");

	fprintf(pf,"\n# midi input\n");
	fprintf(pf,"midi_input_open = %s\n",yes_no(cfg.midi_in_dv_open));
	fprintf(pf,"midi_input_override_channel = %s\n",yes_no(cfg.midi_in_ch_rout));
	fprintf(pf,"midi_input_override_velocity = %s\n",yes_no(cfg.midi_in_vl_rout));

	fprintf(pf,"\n# plugin folders (searched with their sub folders; lv2 and ladspa use their standard locations)\n");
	for(int i=0;i<MAX_VST_FOLDERS;i++)
		fprintf(pf,"vst3_path_%d = %s\n",i+1,cfg.vst_path[i]);
	for(int i=0;i<MAX_CLAP_FOLDERS;i++)
		fprintf(pf,"clap_path_%d = %s\n",i+1,cfg.clap_path[i]);

	fprintf(pf,"\n# recently opened projects\n");
	for(int i=0;i<NUM_RECENT_FILES;i++)
		fprintf(pf,"recent_file_%d = %s\n",i+1,cfg.recent_file[i]);

	bool const ok=(fflush(pf)==0) && !ferror(pf);
	fclose(pf);
	if(!ok || rename(tmp.c_str(),cfg_fil)!=0)
	{
		fprintf(stderr,"aodix: configuration file '%s' could not be written\n",cfg_fil);
		remove(tmp.c_str());
	}
}
