/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - color themes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

ADX_THEME gl_adx_theme;

static struct ThemeInit { ThemeInit() { adx_theme_defaults(&gl_adx_theme); } } s_theme_init;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void adx_theme_defaults(ADX_THEME* t)
{
	// values as hard coded in the original (0x00BBGGRR)
	t->note_hue[0]=0x804040;
	t->note_hue[1]=0x804080;
	t->note_hue[2]=0x404080;
	t->note_hue[3]=0x408080;
	t->note_hue[4]=0x408040;
	t->note_hue[5]=0x808040;
	t->note_back_add=0x202020;
	t->note_plain=0x806060;
	t->note_text=0x808080;
	t->pianoroll_frame=0x000000;
	t->pattern=0x608060;		t->pattern_text=0xFFFFFF;
	t->jump=0x404040;			t->jump_text=0xFFFFFF;
	t->midi=0x606080;			t->midi_text=0xFFFFFF;
	t->automation=0x404040;		t->automation_text=0xFFFFFF;
	t->tempo=0x404040;			t->tempo_text=0xFFFFFF;
	t->ui=adxplat::default_ui_colors();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
COLORREF adx_color_add(COLORREF c,COLORREF a)
{
	COLORREF r=0;
	for(int s=0;s<24;s+=8)
	{
		int v=int((c>>s)&0xFF)+int((a>>s)&0xFF);
		if(v>255) v=255;
		r|=COLORREF(v)<<s;
	}
	return r;
}

COLORREF adx_color_sub(COLORREF c,COLORREF a)
{
	COLORREF r=0;
	for(int s=0;s<24;s+=8)
	{
		int v=int((c>>s)&0xFF)-int((a>>s)&0xFF);
		if(v<0) v=0;
		r|=COLORREF(v)<<s;
	}
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
COLORREF adx_theme_note_color(int hue)
{
	// 6 segments of 64 steps between the anchors; with the classic anchors this gives exactly the original formula
	if(hue<0) hue=0;
	if(hue>383) hue=383;
	int const seg=hue>>6;
	int const t=hue&63;
	COLORREF const a=gl_adx_theme.note_hue[seg];
	COLORREF const b=gl_adx_theme.note_hue[(seg+1)%6];
	COLORREF r=0;
	for(int s=0;s<24;s+=8)
	{
		int const ca=int((a>>s)&0xFF);
		int const cb=int((b>>s)&0xFF);
		r|=COLORREF(ca+(cb-ca)*t/64)<<s;
	}
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ThemeKey { const char* name; COLORREF* value; };

static int theme_keys(ADX_THEME* t,ThemeKey* k)
{
	int n=0;
	static char hue_names[6][16];
	for(int h=0;h<6;h++)
	{
		snprintf(hue_names[h],16,"note_hue_%d",h+1);
		k[n++]={hue_names[h],&t->note_hue[h]};
	}
	k[n++]={"note_back_add",&t->note_back_add};
	k[n++]={"note_plain",&t->note_plain};
	k[n++]={"note_text",&t->note_text};
	k[n++]={"pianoroll_frame",&t->pianoroll_frame};
	k[n++]={"pattern",&t->pattern};
	k[n++]={"pattern_text",&t->pattern_text};
	k[n++]={"jump",&t->jump};
	k[n++]={"jump_text",&t->jump_text};
	k[n++]={"midi",&t->midi};
	k[n++]={"midi_text",&t->midi_text};
	k[n++]={"automation",&t->automation};
	k[n++]={"automation_text",&t->automation_text};
	k[n++]={"tempo",&t->tempo};
	k[n++]={"tempo_text",&t->tempo_text};
	k[n++]={"menu_face",&t->ui.face};
	k[n++]={"menu_text",&t->ui.text};
	k[n++]={"menu_text_disabled",&t->ui.graytext};
	k[n++]={"menu_selection",&t->ui.sel};
	k[n++]={"menu_selection_text",&t->ui.seltext};
	k[n++]={"menu_edge_light",&t->ui.hilight};
	k[n++]={"menu_edge_face",&t->ui.light};
	k[n++]={"menu_edge_shadow",&t->ui.shadow};
	k[n++]={"menu_edge_dark",&t->ui.dkshadow};
	k[n++]={"field_background",&t->ui.window};
	return n;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool adx_theme_load(const char* cfg_path)
{
	ADX_THEME t;
	adx_theme_defaults(&t);
	bool ok=true;

	if(cfg_path && cfg_path[0])
	{
		char expanded[_MAX_PATH],path[_MAX_PATH];
		adx_expand_path(cfg_path,expanded,_MAX_PATH);
		adx_resolve_resource_path(expanded,path);

		FILE* f=fopen(path,"r");
		if(!f)
		{
			fprintf(stderr,"aodix: color theme '%s' not found, using the classic colors\n",path);
			ok=false;
		}
		else
		{
			ThemeKey keys[64];
			int const nkeys=theme_keys(&t,keys);
			char line[512];
			int line_nr=0;
			while(fgets(line,sizeof(line),f))
			{
				line_nr++;

				// skip blanks, comments ('#' or ';' at the start of the line) and section headers
				char* p=line;
				while(isspace((unsigned char)*p)) p++;
				if(*p==0 || *p=='#' || *p==';' || *p=='[')
					continue;

				// key = #RRGGBB
				char* eq=strchr(p,'=');
				if(!eq)
				{
					fprintf(stderr,"aodix: %s:%d: expected 'key = #RRGGBB'\n",path,line_nr);
					continue;
				}
				char* ke=eq;
				while(ke>p && isspace((unsigned char)ke[-1])) ke--;
				*ke=0;
				char* v=eq+1;
				while(isspace((unsigned char)*v)) v++;
				if(*v=='#') v++;
				char* end=nullptr;
				unsigned long const rgb=strtoul(v,&end,16);
				if(end-v!=6)
				{
					fprintf(stderr,"aodix: %s:%d: '%s' needs a color like #RRGGBB\n",path,line_nr,p);
					continue;
				}

				// file has rgb, colorref is bgr
				COLORREF const c=COLORREF(((rgb>>16)&0xFF)|(rgb&0xFF00)|((rgb&0xFF)<<16));
				int k=0;
				for(;k<nkeys;k++)
					if(strcasecmp(keys[k].name,p)==0)
						break;
				if(k<nkeys)
					*keys[k].value=c;
				else
					fprintf(stderr,"aodix: %s:%d: unknown color '%s'\n",path,line_nr,p);
			}
			fclose(f);
		}
	}

	gl_adx_theme=t;
	adxplat::set_ui_colors(t.ui);
	return ok;
}
