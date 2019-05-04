

#include "../../types.h"

void upscale_320x240(uint32 *dst, uint8 *src); 
void upscale_384x240(uint32 *dst, uint8 *src);
void upscale_384x272(uint32 *dst, uint8 *src);
void upscale_480x272(uint32 *dst, uint8 *src);
void upscale_320x240_bilinearish_clip(uint32_t* dst, uint8 *src, int width);
void upscale_320x240_bilinearish_noclip(uint32_t* dst, uint8 *src, int width);
void bitmap_scale(uint32_t startx, uint32_t starty, uint32_t viswidth, uint32_t visheight, uint32_t newwidth, uint32_t newheight,uint32_t pitchsrc,uint32_t pitchdest, uint16_t* __restrict__ src, uint16_t* __restrict__ dst);
