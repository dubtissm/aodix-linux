/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - color themes
// event, block and menu colors that the original had hard coded; loaded from a text file (*.theme)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef AODIX_THEME_H
#define AODIX_THEME_H

#include "../platform/win32compat.h"

struct ADX_THEME
{
	// note events: 6 hue anchors (instrument colors, interpolated cyclically), brightening of the block background
	COLORREF note_hue[6];
	COLORREF note_back_add;
	COLORREF note_plain;			// note color when note colors are switched off
	COLORREF note_text;				// note text at velocity 0 (velocity brightens it)
	COLORREF pianoroll_frame;

	// other events (block background / text)
	COLORREF pattern,pattern_text;
	COLORREF jump,jump_text;
	COLORREF midi,midi_text;
	COLORREF automation,automation_text;	// value/2 is added to the red channel
	COLORREF tempo,tempo_text;				// tempo/2 is added to the red channel

	// play position line and the activity bars above it
	bool	 play_line_invert;		// classic: the line inverts what is below it
	COLORREF play_line;
	COLORREF bar_low,bar_mid,bar_high;	// bar gradient bottom -> middle -> top
	int		 bar_alpha;				// 0..255

	// menus, dialogs and controls
	adxplat::UiColors ui;
};

// size of the activity bars over the play position
#define ADX_BAR_HEIGHT		72
#define ADX_BAR_WIDTH		15

extern ADX_THEME gl_adx_theme;

// built-in classic colors (exactly the original ones)
void		adx_theme_defaults(ADX_THEME* theme);

// load a theme file (path as stored in the config: empty = built-in classic, relative = next to the program /
// share/aodix). keys missing in the file keep the classic value. applies the menu colors right away.
bool		adx_theme_load(const char* path);

// activity bar color at a height position (0 = bottom, 1 = top)
COLORREF	adx_theme_bar_color(float pos);

// note block color for a hue position (0..383)
COLORREF	adx_theme_note_color(int hue);

// per channel saturating add (0x00BBGGRR); sub is subtracted when negative
COLORREF	adx_color_add(COLORREF color,COLORREF add);
COLORREF	adx_color_sub(COLORREF color,COLORREF sub);

#endif
