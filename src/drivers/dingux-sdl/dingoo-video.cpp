/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/// \file
/// \brief Handles the graphical game display for the SDL implementation.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL/SDL.h>

#include "dingoo.h"
#include "dingoo-video.h"
#include "scaler.h"

#include "../common/vidblit.h"
#include "../../fceu.h"
#include "../../version.h"

#include "dface.h"

#include "../common/configSys.h"

// GLOBALS
SDL_Surface *screen;
//SDL_Surface *nes_screen; // 256x224

uint8_t drm_palette[3][256];

extern Config *g_config;

// STATIC GLOBALS
static int s_curbpp;
static int s_srendline, s_erendline;
static int s_tlines;
static int s_inited;
static bool s_VideoModeSet = false;

static int s_clipSides;
int s_fullscreen = 3;
static int noframe;

static int FDSTimer = 0;
int FDSSwitchRequested = 0;

#define NWIDTH	(256)
#define NOFFSET	(0)

/* Blur effect taken from vidblit.cpp */
uint32 palettetranslate[65536 * 4];
static uint32 CBM[3] = { 63488, 2016, 31 };
static uint16 s_psdl[256];

struct Color {
	uint8 r;
	uint8 g;
	uint8 b;
	uint8 unused;
};

static struct Color s_cpsdl[256];

#define BLUR_RED	30
#define BLUR_GREEN	30
#define BLUR_BLUE	20

#ifdef SDL_TRIPLEBUF
#  define DINGOO_MULTIBUF SDL_TRIPLEBUF
#else
#  define DINGOO_MULTIBUF SDL_DOUBLEBUF
#endif


#define UINT16_16(val) ((uint32_t)(val * (float)(1<<16)))
static const uint32_t YUV_MAT[3][3] = {
	{UINT16_16(0.2999f),   UINT16_16(0.587f),    UINT16_16(0.114f)},
	{UINT16_16(0.168736f), UINT16_16(0.331264f), UINT16_16(0.5f)},
	{UINT16_16(0.5f),      UINT16_16(0.418688f), UINT16_16(0.081312f)}
};


static void Update_Hardware_Palette(void)
{
	int i;
	for (i = 0; i < 256; i++)
	{
		drm_palette[0][i] = ( ( UINT16_16(  0) + YUV_MAT[0][0] * s_cpsdl[i].r + YUV_MAT[0][1] * s_cpsdl[i].g + YUV_MAT[0][2] * s_cpsdl[i].b) >> 16 );
		drm_palette[1][i] = ( ( UINT16_16(128) - YUV_MAT[1][0] * s_cpsdl[i].r - YUV_MAT[1][1] * s_cpsdl[i].g + YUV_MAT[1][2] * s_cpsdl[i].b) >> 16 );
		drm_palette[2][i] = ( ( UINT16_16(128) + YUV_MAT[2][0] * s_cpsdl[i].r - YUV_MAT[2][1] * s_cpsdl[i].g - YUV_MAT[2][2] * s_cpsdl[i].b) >> 16 );
	}
}

/**
 * Attempts to destroy the graphical video display.  Returns 0 on
 * success, -1 on failure.
 */

//draw input aids if we are fullscreen
bool FCEUD_ShouldDrawInputAids() {
	return s_fullscreen != 0;
}

int KillVideo() {
	// return failure if the video system was not initialized
	if (s_inited == 0)
		return -1;

	//if (nes_screen) SDL_FreeSurface(nes_screen);
	
	s_inited = 0;
	return 0;
}

void Destroy_Fceux_Video()
{
	if (screen) SDL_FreeSurface(screen);
}

/**
 * These functions determine an appropriate scale factor for fullscreen/
 */
inline double GetXScale(int xres) {
	return ((double) xres) / NWIDTH;
}
inline double GetYScale(int yres) {
	return ((double) yres) / s_tlines;
}
void FCEUD_VideoChanged() {
	int buf;
	g_config->getOption("SDL.PAL", &buf);
	if (buf)
		PAL = 1;
	else
		PAL = 0;
}

extern uint8_t menu;

/**
 * Attempts to initialize the graphical video display.  Returns 0 on
 * success, -1 on failure.
 */
int InitVideo(FCEUGI *gi) {
	FCEUI_printf("Initializing video...\n");

	// load the relevant configuration variables
	g_config->getOption("SDL.Fullscreen", &s_fullscreen);
	g_config->getOption("SDL.ClipSides", &s_clipSides);

	// check the starting, ending, and total scan lines
	FCEUI_GetCurrentVidSystem(&s_srendline, &s_erendline);
	s_tlines = s_erendline - s_srendline + 1;

	int brightness;
	g_config->getOption("SDL.Brightness", &brightness);

	s_inited = 1;
	FDSSwitchRequested = 0;

	//int desbpp;
	//g_config->getOption("SDL.SpecialFilter", &s_eefx);

	SetPaletteBlitToHigh((uint8 *) s_cpsdl);

	//Init video subsystem
	if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1) {
			fprintf(stderr,"%s",SDL_GetError());
		}

	if (menu)
	{
		if (screen->w != 240)
		{
			screen = SDL_SetVideoMode(240, 160, 16, SDL_HWSURFACE | SDL_TRIPLEBUF);
		}
	}
	else
	{
		screen = SDL_SetVideoMode(256, 224 + (PAL*16), 24, SDL_HWSURFACE | SDL_TRIPLEBUF | SDL_YUV444);
	}
	
	s_VideoModeSet = true;

	/*nes_screen = SDL_CreateRGBSurfaceFrom(XBuf, 256, 240, 8, 256, 0, 0, 0, 0);
	if(!nes_screen)
		printf("Error in SDL_CreateRGBSurfaceFrom\n");*/
#ifndef YUV_SCALING
	SDL_SetPalette(screen, SDL_LOGPAL|SDL_PHYSPAL, (SDL_Color *)s_cpsdl, 0, 256);
#else
	Update_Hardware_Palette();
#endif
	SDL_ShowCursor(0);

	/* clear screen */
	dingoo_clear_video();

	return 0;
}

/**
 * Toggles the full-screen display.
 */
void ToggleFS() {
}

/* Taken from /src/drivers/common/vidblit.cpp */
static void CalculateShift(uint32 *CBM, int *cshiftr, int *cshiftl)
{
	int a, x, z, y;
	cshiftl[0] = cshiftl[1] = cshiftl[2] = -1;
	for (a = 0; a < 3; a++) {
		for (x = 0, y = -1, z = 0; x < 32; x++) {
			if (CBM[a] & (1 << x)) {
				if (cshiftl[a] == -1)
					cshiftl[a] = x;
				z++;
			}
		}
		cshiftr[a] = (8 - z);
	}
}

void SetPaletteBlitToHigh(uint8 *src)
{
	int cshiftr[3];
	int cshiftl[3];
	int x, y;

	CalculateShift(CBM, cshiftr, cshiftl);

	for (x = 0; x < 65536; x++) {
		uint16 lower, upper;

		lower = (src[((x & 255) << 2)] >> cshiftr[0]) << cshiftl[0];
		lower |= (src[((x & 255) << 2) + 1] >> cshiftr[1]) << cshiftl[1];
		lower |= (src[((x & 255) << 2) + 2] >> cshiftr[2]) << cshiftl[2];
		upper = (src[((x >> 8) << 2)] >> cshiftr[0]) << cshiftl[0];
		upper |= (src[((x >> 8) << 2) + 1] >> cshiftr[1]) << cshiftl[1];
		upper |= (src[((x >> 8) << 2) + 2] >> cshiftr[2]) << cshiftl[2];

		palettetranslate[x] = lower | (upper << 16);
	}
}

/**
 * Sets the color for a particular index in the palette.
 */
void FCEUD_SetPalette(uint8 index, uint8 r, uint8 g, uint8 b)
{
	s_cpsdl[index].r = r;
	s_cpsdl[index].g = g;
	s_cpsdl[index].b = b;
	
	drm_palette[0][index] = ( ( UINT16_16(  0) + YUV_MAT[0][0] * s_cpsdl[index].r + YUV_MAT[0][1] * s_cpsdl[index].g + YUV_MAT[0][2] * s_cpsdl[index].b) >> 16 );
	drm_palette[1][index] = ( ( UINT16_16(128) - YUV_MAT[1][0] * s_cpsdl[index].r - YUV_MAT[1][1] * s_cpsdl[index].g + YUV_MAT[1][2] * s_cpsdl[index].b) >> 16 );
	drm_palette[2][index] = ( ( UINT16_16(128) + YUV_MAT[2][0] * s_cpsdl[index].r - YUV_MAT[2][1] * s_cpsdl[index].g - YUV_MAT[2][2] * s_cpsdl[index].b) >> 16 );

	//uint32 col = (r << 16) | (g << 8) | b;
	//s_psdl[index] = (uint16)COL32_TO_16(col);
	
	s_psdl[index] = dingoo_video_color15(r, g, b);

	if (index == 255)
		SetPaletteBlitToHigh((uint8 *) s_cpsdl);
}

/**
 * Gets the color for a particular index in the palette.
 */
void FCEUD_GetPalette(uint8 index, uint8 *r, uint8 *g, uint8 *b)
{
	*r = s_cpsdl[index].r;
	*g = s_cpsdl[index].g;
	*b = s_cpsdl[index].b;
}

uint16 * FCEUD_GetPaletteArray16()
{
	return s_psdl;
}

/** 
 * Pushes the palette structure into the underlying video subsystem.
 */
static void RedoPalette() {
}

// XXX soules - console lock/unlock unimplemented?

///Currently unimplemented.
void LockConsole() {
}

///Currently unimplemented.
void UnlockConsole() {
}

#define READU16(x)  (uint16) ((uint16)(x)[0] | (uint16)(x)[1] << 8) 

extern uint8_t PPU[4];

uint8_t clip_ppu = 0;
uint8_t forceRefresh = 0;
uint16_t height;
uint16_t width;

/**
 * Pushes the given buffer of bits to the screen.
 */
void BlitScreen(uint8 *XBuf) {
	int x, x2, y, y2, i;
	
	// Taken from fceugc
	// FDS switch disk requested - need to eject, select, and insert
	// but not all at once!
	if (FDSSwitchRequested) {
		switch (FDSSwitchRequested) {
		case 1:
			FDSSwitchRequested++;
			FCEUI_FDSInsert(); // eject disk
			FDSTimer = 0;
			break;
		case 2:
			if (FDSTimer > 60) {
				FDSSwitchRequested++;
				FDSTimer = 0;
				FCEUI_FDSSelect(); // select other side
				FCEUI_FDSInsert(); // insert disk
			}
			break;
		case 3:
			if (FDSTimer > 200) {
				FDSSwitchRequested = 0;
				FDSTimer = 0;
			}
			break;
		}
		FDSTimer++;
	}
	
	extern uint8 *XBuf;
	int32 pinc = 0;
	if (!(PPU[1] & 2)) clip_ppu = 8;
	else clip_ppu = 0;
	
	height = 224 + (PAL*16);
	width = 256 - clip_ppu;
	
	if (screen->w != width || screen->h != height || forceRefresh)
	{
		if (screen) SDL_FreeSurface(screen);
		screen = SDL_SetVideoMode(width, height, 24, SDL_HWSURFACE | SDL_TRIPLEBUF | SDL_YUV444);
		forceRefresh = 0;
		for(i=0;i<3;i++)
		{
			SDL_FillRect(screen, NULL, 0);
			SDL_Flip(screen);
		}
	}
	
	// TODO - Move these to its own file?
	if (SDL_LockSurface(screen) == 0)
	{
		uint16_t i;
		uint_fast8_t j, a, plane;
		uint8_t* dst_yuv[3];
		uint32_t srcwidth = 256;
		uint8_t *srcbase = XBuf + clip_ppu;
		dst_yuv[0] = (uint8_t*)screen->pixels;
		dst_yuv[1] = dst_yuv[0] + height * screen->pitch;
		dst_yuv[2] = dst_yuv[1] + height * screen->pitch;
		for (plane=0; plane<3; plane++) /* The three Y, U and V planes */
		{
			uint32_t y;
			register uint8_t *pal = drm_palette[plane];
			for (y=0; y < height; y++)   /* The number of lines to copy */
			{
				register uint8_t *src = srcbase + (y*srcwidth);
				register uint8_t *end = src + width;
				register uint32_t *dst = (uint32_t *)&dst_yuv[plane][width * y];

				 __builtin_prefetch(pal, 0, 1 );
				 __builtin_prefetch(src, 0, 1 );
				 __builtin_prefetch(dst, 1, 0 );

				while (src < end)       /* The actual line data to copy */
				{
					register uint32_t pix;
					pix  = pal[*src++];
					pix |= (pal[*src++])<<8;
					pix |= (pal[*src++])<<16;
					pix |= (pal[*src++])<<24;
					*dst++ = pix;
				}
			}
		}
		SDL_UnlockSurface(screen);
	}
		
	SDL_Flip(screen);
}

/**
 *  Converts an x-y coordinate in the window manager into an x-y
 *  coordinate on FCEU's screen.
 */
uint32 PtoV(uint16 x, uint16 y) {
	y = (uint16) ((double) y);
	x = (uint16) ((double) x);
	if (s_clipSides) {
		x += 8;
	}
	y += s_srendline;
	return (x | (y << 16));
}

bool disableMovieMessages = false;
bool FCEUI_AviDisableMovieMessages() {
	if (disableMovieMessages)
		return true;

	return false;
}

void FCEUI_SetAviDisableMovieMessages(bool disable) {
	disableMovieMessages = disable;
}

//clear all screens (for multiple-buffering)
void dingoo_clear_video(void) {
	SDL_FillRect(screen,NULL,SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);
	SDL_FillRect(screen,NULL,SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);
#ifdef SDL_TRIPLEBUF
	SDL_FillRect(screen,NULL,SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);
#endif
}
