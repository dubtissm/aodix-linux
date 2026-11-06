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

	// sequencer track separator lines (left dark, right light); ADX_THEME_AUTO = derived from the skin background
	COLORREF track_line_dark,track_line_light;

	// routing view: audio wires, arrow at full gain (gain 0 = wire color), midi wires, wire being dragged
	COLORREF wire,wire_arrow,midi_wire,wire_drag;

	// play position line, and its flash over a track when a note starts / an automation event plays there
	bool	 play_line_invert;		// classic: the line inverts what is below it
	COLORREF play_line;
	COLORREF flash_note,flash_automation;

	// menus, dialogs and controls
	adxplat::UiColors ui;
};

// color value meaning "not set in the theme, use the skin"
#define ADX_THEME_AUTO		0xFFFFFFFF

// fade speed of the flashes on the play line (per second: full color to the line in about 0.3 s)
#define ADX_FLASH_FADE		3.2f

extern ADX_THEME gl_adx_theme;

// built-in classic colors (exactly the original ones)
void		adx_theme_defaults(ADX_THEME* theme);

// load a theme file (path as stored in the config: empty = built-in classic, relative = next to the program /
// share/aodix). keys missing in the file keep the classic value. applies the menu colors right away.
bool		adx_theme_load(const char* path);

// mix of two colors (t 0..1)
COLORREF	adx_color_lerp(COLORREF a,COLORREF b,float t);

// note block color for a hue position (0..383)
COLORREF	adx_theme_note_color(int hue);

// per channel saturating add (0x00BBGGRR); sub is subtracted when negative
COLORREF	adx_color_add(COLORREF color,COLORREF add);
COLORREF	adx_color_sub(COLORREF color,COLORREF sub);

#endif
