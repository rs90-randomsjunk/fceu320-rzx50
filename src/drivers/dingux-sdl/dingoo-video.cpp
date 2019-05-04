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
SDL_Surface *screen, *toscale_surface;
SDL_Surface *nes_screen; // 256x224

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

	if (nes_screen) SDL_FreeSurface(nes_screen);
	
	s_inited = 0;
	return 0;
}

void Destroy_Fceux_Video()
{
	if (toscale_surface) SDL_FreeSurface(toscale_surface);
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

	screen = SDL_SetVideoMode(240, 160, 16, SDL_HWSURFACE);
	toscale_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 256, 240, 16, 0,0,0,0);
	
	s_VideoModeSet = true;
	
	// a hack to bind inner buffer to nes_screen surface
	extern uint8 *XBuf;

	nes_screen = SDL_CreateRGBSurfaceFrom(XBuf, 256, 240, 8, 256, 0, 0, 0, 0);
	if(!nes_screen)
		printf("Error in SDL_CreateRGBSurfaceFrom\n");
	SDL_SetPalette(nes_screen, SDL_LOGPAL, (SDL_Color *)s_cpsdl, 0, 256);

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

uint32_t toclip_ppu = 0;
uint32_t toclip_ppu_y = 0;
extern uint8_t PPU[4];

/**
 * Pushes the given buffer of bits to the screen.
 */
void BlitScreen(uint8 *XBuf) {
	int x, x2, y, y2;

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
	
	register uint8 *pBuf = XBuf;
	int32 pinc = 0;
	register uint32 *dest = (uint32 *) toscale_surface->pixels;
	
	/* Clip Left Column if PPU background setting is enabled */
	toclip_ppu = 0;
	toclip_ppu_y = 0;
	
	if (!(PPU[1] & 2)) toclip_ppu = 1;

	// TODO - Move these to its own file?
	if (SDL_LockSurface(screen) == 0)
	{
		for (y = s_tlines; y; y--) 
		{
			for (x = 256 >> 3; x; x--) 
			{
				__builtin_prefetch(dest + 4, 1);
				*dest++ = palettetranslate[*(uint16 *) pBuf];
				*dest++ = palettetranslate[*(uint16 *) (pBuf + 2)];
				*dest++ = palettetranslate[*(uint16 *) (pBuf + 4)];
				*dest++ = palettetranslate[*(uint16 *) (pBuf + 6)];
				pBuf += 8;
			}
			dest += pinc;
		}
		switch(s_fullscreen)
		{
			// No crop (except Left Column)
			case 0:
				bitmap_scale(toclip_ppu ? 8 : 0, 0, toclip_ppu ? 248 : 256, 224, screen->w, screen->h, 256, 0, (uint16_t* __restrict__)toscale_surface->pixels, (uint16_t* __restrict__)screen->pixels);
			break;
			// Nintendo Safe overscan
			case 1:
				bitmap_scale(16, 16, 224, 192, screen->w, screen->h, 256, 0, (uint16_t* __restrict__)toscale_surface->pixels, (uint16_t* __restrict__)screen->pixels);
			break;
			// PocketNES
			case 2:
				bitmap_scale(8, 16, 240, 197, screen->w, screen->h, 256, 0, (uint16_t* __restrict__)toscale_surface->pixels, (uint16_t* __restrict__)screen->pixels);
			break;
			// 240x16 crop
			case 3:
				bitmap_scale(8, 48, 240, 160, screen->w, screen->h, 256, 0, (uint16_t* __restrict__)toscale_surface->pixels, (uint16_t* __restrict__)screen->pixels);
			break;
			// Zelda ingame
			case 4:
				bitmap_scale(8, 64, 240, 160, screen->w, screen->h, 256, 0, (uint16_t* __restrict__)toscale_surface->pixels, (uint16_t* __restrict__)screen->pixels);
			break;
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
	SDL_FillRect(screen,NULL,SDL_MapRGBA(screen->format, 0, 0, 0, 255));
	SDL_Flip(screen);
	SDL_FillRect(screen,NULL,SDL_MapRGBA(screen->format, 0, 0, 0, 255));
	SDL_Flip(screen);
#ifdef SDL_TRIPLEBUF
	SDL_FillRect(screen,NULL,SDL_MapRGBA(screen->format, 0, 0, 0, 255));
	SDL_Flip(screen);
#endif
}
