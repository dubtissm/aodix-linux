/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer: GDI (software rendering into 32-bit surfaces)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "win32_internal.h"
#include "font_terminal.h"
#include "font_ui.h"

#include <algorithm>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// stock objects
static GdiPen*		g_stock_black_pen=nullptr;
static GdiBrush*	g_stock_white_brush=nullptr;
static GdiFont*		g_stock_fonts[4]={nullptr,nullptr,nullptr,nullptr};
static GdiBrush*	g_sys_brushes[32]={nullptr};

static void init_stock(void)
{
	if(g_stock_black_pen)
		return;

	g_stock_black_pen=new GdiPen();
	g_stock_black_pen->stock=true;
	g_stock_black_pen->color=0;

	g_stock_white_brush=new GdiBrush();
	g_stock_white_brush->stock=true;
	g_stock_white_brush->color=0x00FFFFFF;

	for(int k=0;k<4;k++)
	{
		g_stock_fonts[k]=new GdiFont();
		g_stock_fonts[k]->stock=true;
		g_stock_fonts[k]->kind=k;
	}
}

GdiFont* stock_font(int kind)
{
	init_stock();
	return g_stock_fonts[kind];
}

DWORD GetSysColor(int index)
{
	if(index==COLOR_BTNFACE)
		return CLR_FACE;
	return CLR_FACE;
}

HBRUSH GetSysColorBrush(int index)
{
	index&=31;
	if(!g_sys_brushes[index])
	{
		g_sys_brushes[index]=new GdiBrush();
		g_sys_brushes[index]->stock=true;
		g_sys_brushes[index]->color=GetSysColor(index);
	}
	return g_sys_brushes[index];
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// device contexts
static DeviceContext* new_dc(void)
{
	init_stock();
	DeviceContext* dc=new DeviceContext();
	dc->bitmap=nullptr;
	dc->default_bitmap=nullptr;
	dc->window=nullptr;
	dc->is_memory=false;
	dc->ox=0;
	dc->oy=0;
	dc->base_clip.left=dc->base_clip.top=0;
	dc->base_clip.right=dc->base_clip.bottom=0;
	dc->has_user_clip=false;
	dc->text_color=0x00000000;
	dc->bk_color=0x00FFFFFF;
	dc->bk_mode=OPAQUE;
	dc->pen=g_stock_black_pen;
	dc->brush=g_stock_white_brush;
	dc->font=g_stock_fonts[FONT_UI];
	dc->cur_x=0;
	dc->cur_y=0;
	return dc;
}

static DeviceContext* memory_dc(void)
{
	DeviceContext* dc=new_dc();
	dc->is_memory=true;
	dc->default_bitmap=new GdiBitmap();
	dc->default_bitmap->stock=true;
	dc->default_bitmap->w=1;
	dc->default_bitmap->h=1;
	dc->default_bitmap->stride=1;
	dc->default_bitmap->px=(uint32_t*)calloc(1,4);
	dc->default_bitmap->own_pixels=true;
	dc->bitmap=dc->default_bitmap;
	dc->base_clip.right=1;
	dc->base_clip.bottom=1;
	return dc;
}

// window dc creation lives here so that window code doesn't need gdi internals
DeviceContext* create_window_dc(Win32Window* w)
{
	DeviceContext* dc=new_dc();
	Win32Window* top=top_level_of(w);
	dc->window=w;
	dc->bitmap=top?top->surface:nullptr;

	int ox=0,oy=0;
	client_origin_in_surface(w,&ox,&oy);
	dc->ox=ox;
	dc->oy=oy;

	// clip to client area (and to all parents' client areas)
	RECT c;
	c.left=ox;
	c.top=oy;
	c.right=ox+(w->parent?(w->rect.right-w->rect.left):w->rect.right);
	c.bottom=oy+(w->parent?(w->rect.bottom-w->rect.top):w->rect.bottom);

	for(Win32Window* p=w->parent;p;p=p->parent)
	{
		int px=0,py=0;
		client_origin_in_surface(p,&px,&py);
		int const pw=p->parent?(p->rect.right-p->rect.left):p->rect.right;
		int const ph=p->parent?(p->rect.bottom-p->rect.top):p->rect.bottom;
		c.left=std::max<LONG>(c.left,px);
		c.top=std::max<LONG>(c.top,py);
		c.right=std::min<LONG>(c.right,px+pw);
		c.bottom=std::min<LONG>(c.bottom,py+ph);
	}

	dc->base_clip=c;
	if(w->font)
		dc->font=w->font;
	return dc;
}

// dc covering the whole top level surface (used for the menu bar / non client drawing)
DeviceContext* create_surface_dc(Win32Window* top)
{
	DeviceContext* dc=new_dc();
	dc->window=top;
	dc->bitmap=top->surface;
	dc->base_clip.left=0;
	dc->base_clip.top=0;
	dc->base_clip.right=top->surface?top->surface->w:0;
	dc->base_clip.bottom=top->surface?top->surface->h:0;
	return dc;
}

void destroy_dc(DeviceContext* dc)
{
	if(!dc)
		return;
	if(dc->default_bitmap)
		delete dc->default_bitmap;
	delete dc;
}

RECT dc_clip(DeviceContext* dc)
{
	RECT r=dc->base_clip;

	if(dc->bitmap)
	{
		r.left=std::max<LONG>(r.left,0);
		r.top=std::max<LONG>(r.top,0);
		r.right=std::min<LONG>(r.right,dc->bitmap->w);
		r.bottom=std::min<LONG>(r.bottom,dc->bitmap->h);
	}
	else
	{
		r.right=r.left;
		r.bottom=r.top;
	}

	if(dc->has_user_clip)
	{
		r.left=std::max<LONG>(r.left,dc->user_clip.left+dc->ox);
		r.top=std::max<LONG>(r.top,dc->user_clip.top+dc->oy);
		r.right=std::min<LONG>(r.right,dc->user_clip.right+dc->ox);
		r.bottom=std::min<LONG>(r.bottom,dc->user_clip.bottom+dc->oy);
	}

	return r;
}

void dc_mark_dirty(DeviceContext* dc,int x1,int y1,int x2,int y2)
{
	if(!dc->window)
		return;

	Win32Window* top=top_level_of(dc->window);
	if(!top || x2<=x1 || y2<=y1)
		return;

	if(!top->has_dirty)
	{
		top->dirty.left=x1;
		top->dirty.top=y1;
		top->dirty.right=x2;
		top->dirty.bottom=y2;
		top->has_dirty=true;
	}
	else
	{
		top->dirty.left=std::min<LONG>(top->dirty.left,x1);
		top->dirty.top=std::min<LONG>(top->dirty.top,y1);
		top->dirty.right=std::max<LONG>(top->dirty.right,x2);
		top->dirty.bottom=std::max<LONG>(top->dirty.bottom,y2);
	}
}

HDC CreateCompatibleDC(HDC)
{
	return memory_dc();
}

BOOL DeleteDC(HDC hdc)
{
	destroy_dc(hdc);
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bitmaps
static GdiBitmap* new_bitmap(int w,int h)
{
	GdiBitmap* b=new GdiBitmap();
	b->w=std::max(w,1);
	b->h=std::max(h,1);
	b->stride=b->w;
	b->px=(uint32_t*)calloc((size_t)b->w*b->h,4);
	b->own_pixels=true;
	return b;
}

HBITMAP CreateCompatibleBitmap(HDC,int w,int h)
{
	return new_bitmap(w,h);
}

static uint32_t rd32(const unsigned char* p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t rd16(const unsigned char* p){return p[0]|(p[1]<<8);}

// windows bmp loader (1/4/8/24/32 bit uncompressed, as used by aodix skins)
static GdiBitmap* load_bmp_file(const char* path)
{
	FILE* f=fopen(path,"rb");
	if(!f)
		return nullptr;

	fseek(f,0,SEEK_END);
	long const size=ftell(f);
	fseek(f,0,SEEK_SET);

	std::vector<unsigned char> d(size>0?size:0);
	if(size<54 || fread(d.data(),1,size,f)!=(size_t)size)
	{
		fclose(f);
		return nullptr;
	}
	fclose(f);

	if(d[0]!='B' || d[1]!='M')
		return nullptr;

	uint32_t const data_ofs=rd32(&d[10]);
	uint32_t const hdr_size=rd32(&d[14]);
	int const w=(int)rd32(&d[18]);
	int h=(int)rd32(&d[22]);
	int const bpp=rd16(&d[28]);
	uint32_t const compression=rd32(&d[30]);
	uint32_t num_colors=rd32(&d[46]);

	if(compression!=0 && compression!=3)
		return nullptr;

	bool const bottom_up=h>0;
	if(h<0)
		h=-h;

	if(w<=0 || h<=0 || w>16384 || h>16384)
		return nullptr;

	// palette
	uint32_t pal[256];
	memset(pal,0,sizeof(pal));
	if(bpp<=8)
	{
		if(num_colors==0)
			num_colors=1u<<bpp;
		size_t const pal_ofs=14+hdr_size;
		for(uint32_t i=0;i<num_colors && i<256 && pal_ofs+i*4+3<d.size();i++)
			pal[i]=(d[pal_ofs+i*4+2]<<16)|(d[pal_ofs+i*4+1]<<8)|d[pal_ofs+i*4+0];
	}

	GdiBitmap* b=new_bitmap(w,h);
	size_t const row_bytes=((size_t)w*bpp+31)/32*4;

	for(int y=0;y<h;y++)
	{
		size_t const ofs=data_ofs+(size_t)(bottom_up?(h-1-y):y)*row_bytes;
		if(ofs+row_bytes>d.size())
			break;

		const unsigned char* s=&d[ofs];
		uint32_t* o=b->px+(size_t)y*b->stride;

		for(int x=0;x<w;x++)
		{
			uint32_t p=0;
			switch(bpp)
			{
			case 1:		p=pal[(s[x>>3]>>(7-(x&7)))&1];			break;
			case 4:		p=pal[(s[x>>1]>>((x&1)?0:4))&15];		break;
			case 8:		p=pal[s[x]];							break;
			case 16:	{uint16_t v=rd16(s+x*2); p=(((v>>10)&31)*255/31)<<16|(((v>>5)&31)*255/31)<<8|((v&31)*255/31);} break;
			case 24:	p=(s[x*3+2]<<16)|(s[x*3+1]<<8)|s[x*3];	break;
			case 32:	p=(s[x*4+2]<<16)|(s[x*4+1]<<8)|s[x*4];	break;
			}
			o[x]=p;
		}
	}

	return b;
}

void* LoadImage(HINSTANCE,LPCSTR name,UINT type,int,int,UINT flags)
{
	if(type!=IMAGE_BITMAP || !(flags & LR_LOADFROMFILE) || !name)
		return nullptr;

	// accept windows style separators in paths
	std::string path(name);
	std::replace(path.begin(),path.end(),'\\','/');

	GdiBitmap* b=load_bmp_file(path.c_str());
	if(!b)
		fprintf(stderr,"aodix: could not load bitmap '%s'\n",path.c_str());
	return b;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// object selection
HGDIOBJ SelectObject(HDC hdc,HGDIOBJ obj)
{
	if(!hdc || !obj)
		return nullptr;

	switch(obj->type)
	{
	case GDI_BITMAP:
		{
			if(!hdc->is_memory)
				return nullptr;
			GdiBitmap* old=hdc->bitmap;
			hdc->bitmap=(GdiBitmap*)obj;
			hdc->base_clip.left=0;
			hdc->base_clip.top=0;
			hdc->base_clip.right=hdc->bitmap->w;
			hdc->base_clip.bottom=hdc->bitmap->h;
			return old;
		}
	case GDI_PEN:
		{
			GdiPen* old=hdc->pen;
			hdc->pen=(GdiPen*)obj;
			return old;
		}
	case GDI_BRUSH:
		{
			GdiBrush* old=hdc->brush;
			hdc->brush=(GdiBrush*)obj;
			return old;
		}
	case GDI_FONT:
		{
			GdiFont* old=hdc->font;
			hdc->font=(GdiFont*)obj;
			return old;
		}
	case GDI_REGION:
		SelectClipRgn(hdc,(HRGN)obj);
		return nullptr;
	}

	return nullptr;
}

BOOL DeleteObject(HGDIOBJ obj)
{
	if(!obj || obj->stock)
		return FALSE;
	delete obj;
	return TRUE;
}

HBRUSH CreateSolidBrush(COLORREF color)
{
	GdiBrush* b=new GdiBrush();
	b->color=color&0x00FFFFFF;
	return b;
}

HPEN CreatePen(int style,int width,COLORREF color)
{
	GdiPen* p=new GdiPen();
	p->style=style;
	p->width=std::max(width,1);
	p->color=color&0x00FFFFFF;
	return p;
}

HFONT CreateFont(int h,int,int,int,int weight,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR face)
{
	GdiFont* f=new GdiFont();
	if(face && strcasecmp(face,"Terminal")==0)
		f->kind=FONT_TERMINAL;
	else
		f->kind=(weight>=FW_BOLD)?FONT_UI_BOLD:FONT_UI;
	(void)h;
	return f;
}

HRGN CreateRectRgn(int x1,int y1,int x2,int y2)
{
	GdiRegion* r=new GdiRegion();
	r->r.left=std::min(x1,x2);
	r->r.top=std::min(y1,y2);
	r->r.right=std::max(x1,x2);
	r->r.bottom=std::max(y1,y2);
	return r;
}

int SelectClipRgn(HDC hdc,HRGN rgn)
{
	if(!hdc)
		return 0;

	if(!rgn)
	{
		hdc->has_user_clip=false;
		return 1;
	}

	hdc->has_user_clip=true;
	hdc->user_clip=rgn->r;
	return 2;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// raster operations
static inline bool clip_rect(DeviceContext* dc,int& x1,int& y1,int& x2,int& y2)
{
	RECT const c=dc_clip(dc);
	if(x1<c.left) x1=c.left;
	if(y1<c.top) y1=c.top;
	if(x2>c.right) x2=c.right;
	if(y2>c.bottom) y2=c.bottom;
	return x1<x2 && y1<y2;
}

BOOL BitBlt(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,DWORD rop)
{
	if(!dst || !src || !dst->bitmap || !src->bitmap || w<=0 || h<=0)
		return FALSE;

	(void)rop; // aodix only uses SRCCOPY

	// destination in surface coordinates
	int dx1=x+dst->ox;
	int dy1=y+dst->oy;
	int dx2=dx1+w;
	int dy2=dy1+h;

	// source in surface coordinates
	int const ssx=sx+src->ox;
	int const ssy=sy+src->oy;

	// clip against source bounds (pixels outside the source are left untouched)
	RECT const sc=dc_clip(src);
	int const offx=ssx-dx1;
	int const offy=ssy-dy1;
	dx1=std::max<int>(dx1,sc.left-offx);
	dy1=std::max<int>(dy1,sc.top-offy);
	dx2=std::min<int>(dx2,sc.right-offx);
	dy2=std::min<int>(dy2,sc.bottom-offy);

	if(!clip_rect(dst,dx1,dy1,dx2,dy2))
		return TRUE;

	GdiBitmap* const db=dst->bitmap;
	GdiBitmap* const sb=src->bitmap;
	int const cw=dx2-dx1;

	if(db==sb && dy1+offy<dy1)
	{
		for(int yy=dy1;yy<dy2;yy++)
			memmove(db->px+(size_t)yy*db->stride+dx1,sb->px+(size_t)(yy+offy)*sb->stride+dx1+offx,cw*4);
	}
	else if(db==sb)
	{
		for(int yy=dy2-1;yy>=dy1;yy--)
			memmove(db->px+(size_t)yy*db->stride+dx1,sb->px+(size_t)(yy+offy)*sb->stride+dx1+offx,cw*4);
	}
	else
	{
		for(int yy=dy1;yy<dy2;yy++)
			memcpy(db->px+(size_t)yy*db->stride+dx1,sb->px+(size_t)(yy+offy)*sb->stride+dx1+offx,cw*4);
	}

	dc_mark_dirty(dst,dx1,dy1,dx2,dy2);
	return TRUE;
}

BOOL AlphaBlend(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,int sw,int sh,BLENDFUNCTION bf)
{
	if(!dst || !src || !dst->bitmap || !src->bitmap || w<=0 || h<=0 || sw<=0 || sh<=0)
		return FALSE;

	int dx1=x+dst->ox;
	int dy1=y+dst->oy;
	int dx2=dx1+w;
	int dy2=dy1+h;

	if(!clip_rect(dst,dx1,dy1,dx2,dy2))
		return TRUE;

	GdiBitmap* const db=dst->bitmap;
	GdiBitmap* const sb=src->bitmap;
	int const a=bf.SourceConstantAlpha;
	int const ia=255-a;
	int const ox=x+dst->ox;
	int const oy=y+dst->oy;

	for(int yy=dy1;yy<dy2;yy++)
	{
		int const syy=sy+src->oy+((yy-oy)*sh)/h;
		if(syy<0 || syy>=sb->h)
			continue;

		uint32_t* d=db->px+(size_t)yy*db->stride;
		const uint32_t* s=sb->px+(size_t)syy*sb->stride;

		for(int xx=dx1;xx<dx2;xx++)
		{
			int const sxx=sx+src->ox+((xx-ox)*sw)/w;
			if(sxx<0 || sxx>=sb->w)
				continue;

			uint32_t const sp=s[sxx];
			uint32_t const dp=d[xx];
			uint32_t const r=(((sp>>16)&0xFF)*a+((dp>>16)&0xFF)*ia)/255;
			uint32_t const g=(((sp>>8)&0xFF)*a+((dp>>8)&0xFF)*ia)/255;
			uint32_t const b=((sp&0xFF)*a+(dp&0xFF)*ia)/255;
			d[xx]=(r<<16)|(g<<8)|b;
		}
	}

	dc_mark_dirty(dst,dx1,dy1,dx2,dy2);
	return TRUE;
}

COLORREF GetPixel(HDC hdc,int x,int y)
{
	if(!hdc || !hdc->bitmap)
		return 0xFFFFFFFF;

	int const sx=x+hdc->ox;
	int const sy=y+hdc->oy;
	if(sx<0 || sy<0 || sx>=hdc->bitmap->w || sy>=hdc->bitmap->h)
		return 0xFFFFFFFF;

	return px_to_cr(hdc->bitmap->px[(size_t)sy*hdc->bitmap->stride+sx]);
}

COLORREF SetPixel(HDC hdc,int x,int y,COLORREF color)
{
	if(!hdc || !hdc->bitmap)
		return 0xFFFFFFFF;

	int x1=x+hdc->ox,y1=y+hdc->oy,x2=x1+1,y2=y1+1;
	if(clip_rect(hdc,x1,y1,x2,y2))
	{
		hdc->bitmap->px[(size_t)y1*hdc->bitmap->stride+x1]=cr_to_px(color);
		dc_mark_dirty(hdc,x1,y1,x2,y2);
	}
	return color;
}

// fill in surface coordinates (clipped)
static void fill_surface(DeviceContext* dc,int x1,int y1,int x2,int y2,uint32_t p)
{
	if(!dc->bitmap || !clip_rect(dc,x1,y1,x2,y2))
		return;

	GdiBitmap* const b=dc->bitmap;
	for(int yy=y1;yy<y2;yy++)
	{
		uint32_t* d=b->px+(size_t)yy*b->stride;
		for(int xx=x1;xx<x2;xx++)
			d[xx]=p;
	}

	dc_mark_dirty(dc,x1,y1,x2,y2);
}

void fill_rect(HDC hdc,int x,int y,int w,int h,COLORREF color)
{
	if(!hdc || w<=0 || h<=0)
		return;
	fill_surface(hdc,x+hdc->ox,y+hdc->oy,x+w+hdc->ox,y+h+hdc->oy,cr_to_px(color));
}

int FillRect(HDC hdc,const RECT* r,HBRUSH brush)
{
	if(!hdc || !r)
		return 0;

	COLORREF const color=brush?brush->color:GetSysColor(COLOR_BTNFACE);
	if(brush && brush->null_brush)
		return 1;

	fill_rect(hdc,r->left,r->top,r->right-r->left,r->bottom-r->top,color);
	return 1;
}

BOOL InvertRect(HDC hdc,const RECT* r)
{
	if(!hdc || !r || !hdc->bitmap)
		return FALSE;

	int x1=r->left+hdc->ox,y1=r->top+hdc->oy,x2=r->right+hdc->ox,y2=r->bottom+hdc->oy;
	if(!clip_rect(hdc,x1,y1,x2,y2))
		return TRUE;

	GdiBitmap* const b=hdc->bitmap;
	for(int yy=y1;yy<y2;yy++)
	{
		uint32_t* d=b->px+(size_t)yy*b->stride;
		for(int xx=x1;xx<x2;xx++)
			d[xx]^=0x00FFFFFF;
	}

	dc_mark_dirty(hdc,x1,y1,x2,y2);
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// lines and polygons
static inline void plot(DeviceContext* dc,const RECT& c,int x,int y,uint32_t p)
{
	if(x>=c.left && x<c.right && y>=c.top && y<c.bottom)
		dc->bitmap->px[(size_t)y*dc->bitmap->stride+x]=p;
}

// bresenham line in surface coordinates; the end point is not drawn (like GDI)
static void line_surface(DeviceContext* dc,int x0,int y0,int x1,int y1,uint32_t p,bool include_last)
{
	if(!dc->bitmap)
		return;

	RECT const c=dc_clip(dc);
	int const dx=abs(x1-x0),sx=x0<x1?1:-1;
	int const dy=-abs(y1-y0),sy=y0<y1?1:-1;
	int err=dx+dy;
	int x=x0,y=y0;

	for(;;)
	{
		if(x==x1 && y==y1)
		{
			if(include_last)
				plot(dc,c,x,y,p);
			break;
		}
		plot(dc,c,x,y,p);
		int const e2=2*err;
		if(e2>=dy){err+=dy;x+=sx;}
		if(e2<=dx){err+=dx;y+=sy;}
	}

	dc_mark_dirty(dc,std::max<int>(c.left,std::min(x0,x1)),std::max<int>(c.top,std::min(y0,y1)),std::min<int>(c.right,std::max(x0,x1)+1),std::min<int>(c.bottom,std::max(y0,y1)+1));
}

BOOL MoveToEx(HDC hdc,int x,int y,POINT* old)
{
	if(!hdc)
		return FALSE;
	if(old)
	{
		old->x=hdc->cur_x;
		old->y=hdc->cur_y;
	}
	hdc->cur_x=x;
	hdc->cur_y=y;
	return TRUE;
}

BOOL LineTo(HDC hdc,int x,int y)
{
	if(!hdc)
		return FALSE;

	if(hdc->pen && !hdc->pen->null_pen)
		line_surface(hdc,hdc->cur_x+hdc->ox,hdc->cur_y+hdc->oy,x+hdc->ox,y+hdc->oy,cr_to_px(hdc->pen->color),false);

	hdc->cur_x=x;
	hdc->cur_y=y;
	return TRUE;
}

BOOL Polygon(HDC hdc,const POINT* pts,int count)
{
	if(!hdc || !hdc->bitmap || count<3)
		return FALSE;

	// scanline fill (alternate mode) with the current brush
	if(hdc->brush && !hdc->brush->null_brush)
	{
		int miny=pts[0].y,maxy=pts[0].y;
		for(int i=1;i<count;i++)
		{
			miny=std::min<int>(miny,pts[i].y);
			maxy=std::max<int>(maxy,pts[i].y);
		}

		uint32_t const p=cr_to_px(hdc->brush->color);
		std::vector<double> xs;

		for(int y=miny;y<maxy;y++)
		{
			double const fy=y+0.5;
			xs.clear();
			for(int i=0,j=count-1;i<count;j=i++)
			{
				double const yi=pts[i].y,yj=pts[j].y;
				if((yi<=fy && fy<yj) || (yj<=fy && fy<yi))
					xs.push_back(pts[i].x+(fy-yi)*(pts[j].x-pts[i].x)/(yj-yi));
			}
			std::sort(xs.begin(),xs.end());
			for(size_t k=0;k+1<xs.size();k+=2)
			{
				int const xa=(int)ceil(xs[k]-0.5);
				int const xb=(int)ceil(xs[k+1]-0.5);
				fill_surface(hdc,xa+hdc->ox,y+hdc->oy,xb+hdc->ox,y+1+hdc->oy,p);
			}
		}
	}

	// outline with the current pen
	if(hdc->pen && !hdc->pen->null_pen)
	{
		uint32_t const p=cr_to_px(hdc->pen->color);
		for(int i=0;i<count;i++)
		{
			const POINT& a=pts[i];
			const POINT& b=pts[(i+1)%count];
			line_surface(hdc,a.x+hdc->ox,a.y+hdc->oy,b.x+hdc->ox,b.y+hdc->oy,p,true);
		}
	}

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// text
COLORREF SetTextColor(HDC hdc,COLORREF color)
{
	if(!hdc)
		return 0;
	COLORREF const old=hdc->text_color;
	hdc->text_color=color&0x00FFFFFF;
	return old;
}

COLORREF SetBkColor(HDC hdc,COLORREF color)
{
	if(!hdc)
		return 0;
	COLORREF const old=hdc->bk_color;
	hdc->bk_color=color&0x00FFFFFF;
	return old;
}

int SetBkMode(HDC hdc,int mode)
{
	if(!hdc)
		return 0;
	int const old=hdc->bk_mode;
	hdc->bk_mode=mode;
	return old;
}

// decode one character: utf-8 when valid, otherwise single byte (windows-1252/latin-1)
static int next_char(const unsigned char* s,int remaining,int* cp)
{
	unsigned char const c=s[0];
	if(c<0x80 || remaining<2)
	{
		*cp=c;
		return 1;
	}
	if((c&0xE0)==0xC0 && (s[1]&0xC0)==0x80)
	{
		*cp=((c&0x1F)<<6)|(s[1]&0x3F);
		return 2;
	}
	if((c&0xF0)==0xE0 && remaining>=3 && (s[1]&0xC0)==0x80 && (s[2]&0xC0)==0x80)
	{
		*cp=((c&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F);
		return 3;
	}
	if((c&0xF8)==0xF0 && remaining>=4 && (s[1]&0xC0)==0x80 && (s[2]&0xC0)==0x80 && (s[3]&0xC0)==0x80)
	{
		*cp='?';
		return 4;
	}
	*cp=c;
	return 1;
}

static const ADX_UI_GLYPH* ui_glyph(int kind,int cp,const unsigned char** data)
{
	if(cp>255 || cp<0)
		cp='?';
	if(kind==FONT_UI_BOLD)
	{
		*data=adx_ui_font_bold_data;
		return &adx_ui_font_bold_glyphs[cp];
	}
	if(kind==FONT_UI_SMALL)
	{
		*data=adx_ui_font_small_data;
		return &adx_ui_font_small_glyphs[cp];
	}
	*data=adx_ui_font_regular_data;
	return &adx_ui_font_regular_glyphs[cp];
}

int font_height(int kind)
{
	if(kind==FONT_TERMINAL)
		return 8;
	if(kind==FONT_UI_SMALL)
		return adx_ui_font_small_height;
	return kind==FONT_UI_BOLD?adx_ui_font_bold_height:adx_ui_font_regular_height;
}

int font_ascent(int kind)
{
	if(kind==FONT_TERMINAL)
		return 7;
	if(kind==FONT_UI_SMALL)
		return adx_ui_font_small_ascent;
	return kind==FONT_UI_BOLD?adx_ui_font_bold_ascent:adx_ui_font_regular_ascent;
}

int text_width(int kind,const char* text,int count)
{
	if(!text)
		return 0;
	if(count<0)
		count=(int)strlen(text);

	if(kind==FONT_TERMINAL)
		return count*6;

	int w=0;
	const unsigned char* s=(const unsigned char*)text;
	for(int i=0;i<count;)
	{
		int cp=0;
		i+=next_char(s+i,count-i,&cp);
		const unsigned char* data;
		w+=ui_glyph(kind,cp,&data)->adv;
	}
	return w;
}

// draw text with its top-left corner at (x,y) logical coordinates, foreground only
void draw_text_surface(DeviceContext* dc,int x,int y,const char* text,int count,int kind,COLORREF color)
{
	if(!dc || !dc->bitmap || !text)
		return;

	RECT const c=dc_clip(dc);
	uint32_t const p=cr_to_px(color);
	int const fr=(p>>16)&0xFF,fg=(p>>8)&0xFF,fb=p&0xFF;
	GdiBitmap* const b=dc->bitmap;
	int px=x+dc->ox;
	int const py=y+dc->oy;
	int minx=px,maxx=px;

	if(kind==FONT_TERMINAL)
	{
		for(int i=0;i<count;i++)
		{
			unsigned char const ch=(unsigned char)text[i];
			const unsigned char* g=adx_terminal_font_6x8[ch&0x7F];
			for(int gy=0;gy<8;gy++)
			{
				int const yy=py+gy;
				if(yy<c.top || yy>=c.bottom)
					continue;
				for(int gx=0;gx<6;gx++)
				{
					int const xx=px+gx;
					if(xx>=c.left && xx<c.right && (g[gy]>>(5-gx))&1)
						b->px[(size_t)yy*b->stride+xx]=p;
				}
			}
			px+=6;
		}
		maxx=px;
		dc_mark_dirty(dc,std::max<int>(minx,c.left),std::max<int>(py,c.top),std::min<int>(maxx,c.right),std::min<int>(py+8,c.bottom));
		return;
	}

	const unsigned char* s=(const unsigned char*)text;
	for(int i=0;i<count;)
	{
		int cp=0;
		i+=next_char(s+i,count-i,&cp);
		const unsigned char* data;
		const ADX_UI_GLYPH* g=ui_glyph(kind,cp,&data);

		for(int gy=0;gy<g->h;gy++)
		{
			int const yy=py+g->y+gy;
			if(yy<c.top || yy>=c.bottom)
				continue;
			uint32_t* d=b->px+(size_t)yy*b->stride;
			const unsigned char* a=data+g->ofs+gy*g->w;
			for(int gx=0;gx<g->w;gx++)
			{
				int const xx=px+g->x+gx;
				int const al=a[gx];
				if(al==0 || xx<c.left || xx>=c.right)
					continue;
				if(al==255)
				{
					d[xx]=p;
					continue;
				}
				uint32_t const dp=d[xx];
				int const ia=255-al;
				d[xx]=(((fr*al+((dp>>16)&0xFF)*ia)/255)<<16)|(((fg*al+((dp>>8)&0xFF)*ia)/255)<<8)|((fb*al+(dp&0xFF)*ia)/255);
			}
		}
		px+=g->adv;
	}
	maxx=px+2;
	dc_mark_dirty(dc,std::max<int>(minx,c.left),std::max<int>(py,c.top),std::min<int>(maxx,c.right),std::min<int>(py+font_height(kind),c.bottom));
}

BOOL ExtTextOut(HDC hdc,int x,int y,UINT options,const RECT* r,LPCSTR text,UINT count,const int*)
{
	if(!hdc)
		return FALSE;

	if((options & ETO_OPAQUE) && r)
		fill_rect(hdc,r->left,r->top,r->right-r->left,r->bottom-r->top,hdc->bk_color);

	if(text && count>0)
		TextOut(hdc,x,y,text,(int)count);

	return TRUE;
}

BOOL TextOut(HDC hdc,int x,int y,LPCSTR text,int count)
{
	if(!hdc || !text || count<=0)
		return FALSE;

	int const kind=hdc->font?hdc->font->kind:FONT_UI;

	// windows draws exactly 'count' characters (including embedded nulls as blank cells for the raster font)
	if(hdc->bk_mode==OPAQUE)
		fill_rect(hdc,x,y,text_width(kind,text,count),font_height(kind),hdc->bk_color);

	if(kind==FONT_TERMINAL)
	{
		// draw up to the first null, remaining cells stay blank
		int n=0;
		while(n<count && text[n])
			n++;
		draw_text_surface(hdc,x,y,text,n,kind,hdc->text_color);
	}
	else
	{
		int n=0;
		while(n<count && text[n])
			n++;
		draw_text_surface(hdc,x,y,text,n,kind,hdc->text_color);
	}

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// helpers for the built-in widgets (classic windows look)
void frame_3d(HDC hdc,int x,int y,int w,int h,bool sunken,bool thick)
{
	COLORREF const tl1=sunken?CLR_SHADOW:CLR_HILIGHT;
	COLORREF const br1=sunken?CLR_HILIGHT:CLR_DKSHADOW;
	COLORREF const tl2=sunken?CLR_DKSHADOW:CLR_LIGHT;
	COLORREF const br2=sunken?CLR_LIGHT:CLR_SHADOW;

	fill_rect(hdc,x,y,w-1,1,tl1);
	fill_rect(hdc,x,y,1,h-1,tl1);
	fill_rect(hdc,x,y+h-1,w,1,br1);
	fill_rect(hdc,x+w-1,y,1,h,br1);

	if(thick)
	{
		fill_rect(hdc,x+1,y+1,w-3,1,tl2);
		fill_rect(hdc,x+1,y+1,1,h-3,tl2);
		fill_rect(hdc,x+1,y+h-2,w-2,1,br2);
		fill_rect(hdc,x+w-2,y+1,1,h-2,br2);
	}
}

void draw_text_in(HDC hdc,const RECT& r,const char* text,int kind,COLORREF color,int align,bool vcenter)
{
	if(!text)
		return;

	// strip '&' mnemonics ('&&' -> '&')
	std::string s;
	for(const char* p=text;*p;p++)
	{
		if(*p=='&')
		{
			if(p[1]=='&')
			{
				s+='&';
				p++;
			}
			continue;
		}
		s+=*p;
	}

	// labels that don't fit their control use the narrower small font (the windows dialog font is narrower)
	int tw=text_width(kind,s.c_str(),(int)s.size());
	if(kind==FONT_UI && tw>(r.right-r.left))
	{
		kind=FONT_UI_SMALL;
		tw=text_width(kind,s.c_str(),(int)s.size());
	}
	int const th=font_height(kind);
	int x=r.left;
	if(align==1)
		x=r.left+((r.right-r.left)-tw)/2;
	if(align==2)
		x=r.right-tw;
	int y=r.top;
	if(vcenter)
		y=r.top+((r.bottom-r.top)-th)/2;

	// clip to the rectangle
	bool const had=hdc->has_user_clip;
	RECT const old=hdc->user_clip;
	RECT nc=r;
	if(had)
	{
		nc.left=std::max(nc.left,old.left);
		nc.top=std::max(nc.top,old.top);
		nc.right=std::min(nc.right,old.right);
		nc.bottom=std::min(nc.bottom,old.bottom);
	}
	hdc->has_user_clip=true;
	hdc->user_clip=nc;
	draw_text_surface(hdc,x,y,s.c_str(),(int)s.size(),kind,color);
	hdc->has_user_clip=had;
	hdc->user_clip=old;
}

void draw_focus_rect(HDC hdc,int x,int y,int w,int h)
{
	for(int i=0;i<w;i+=2)
	{
		fill_rect(hdc,x+i,y,1,1,0);
		fill_rect(hdc,x+i,y+h-1,1,1,0);
	}
	for(int i=0;i<h;i+=2)
	{
		fill_rect(hdc,x,y+i,1,1,0);
		fill_rect(hdc,x+w-1,y+i,1,1,0);
	}
}
