/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_surf.c: surface-related refresh code

#include "r_local.h"

drawsurf_t	r_drawsurf;

int				lightleft, sourcesstep, blocksize, sourcetstep;
int				lightdelta, lightdeltastep;
int				lightright, lightleftstep, lightrightstep, blockdivshift;
unsigned		blockdivmask;
void			*prowdestbase;
unsigned char	*pbasesource;
int				surfrowbytes;	// used by ASM files
unsigned		*r_lightptr;
int				r_stepback;
int				r_lightwidth;
int				r_numhblocks, r_numvblocks;
unsigned char	*r_source, *r_sourcemax;

void GenerateCacheBlockMip0 (void);
void GenerateCacheBlockMip1 (void);
void GenerateCacheBlockMip2 (void);
void GenerateCacheBlockMip3 (void);

static void	(*surfmiptable[4])(void) = {
	GenerateCacheBlockMip0,
	GenerateCacheBlockMip1,
	GenerateCacheBlockMip2,
	GenerateCacheBlockMip3
};

void R_BuildLightMap (void);
extern	unsigned		blocklights[1024];	// allow some very large lightmaps

float           surfscale;
qboolean        r_cache_thrash;         // set if surface cache is thrashing

int         sc_size;
surfcache_t	*sc_rover, *sc_base;


int D_log2 (int num)
{
	int     c;
	
	c = 0;
	
	while (num>>=1)
		c++;
	return c;
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex)
{
	int		c;

	if (!tex->next)
		return tex->image;

	c = currententity->frame % tex->numframes;
	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}


/*
===============
R_DrawSurface
===============
*/
//PANZER - convert it to colored 
/*
void R_DrawSurface (void)
{
	unsigned char	*basetptr;
	int				smax, tmax, twidth;
	int				u;
	int				soffset, basetoffset, texwidth;
	int				horzblockstep;
	unsigned char	*pcolumndest;
	void			(*pblockdrawer)(void);
	image_t			*mt;

	surfrowbytes = r_drawsurf.rowbytes;

	mt = r_drawsurf.image;
	
	r_source = mt->pixels[r_drawsurf.surfmip];
	
// the fractional light values should range from 0 to (VID_GRADES - 1) << 16
// from a source range of 0 - 255
	
	texwidth = mt->width >> r_drawsurf.surfmip;

	blocksize = 16 >> r_drawsurf.surfmip;
	blockdivshift = 4 - r_drawsurf.surfmip;
	blockdivmask = (1 << blockdivshift) - 1;
	
	r_lightwidth = (r_drawsurf.surf->extents[0]>>4)+1;

	r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
	r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

//==============================

	pblockdrawer = surfmiptable[r_drawsurf.surfmip];
// TODO: only needs to be set when there is a display settings change
	horzblockstep = blocksize;

	smax = mt->width >> r_drawsurf.surfmip;
	twidth = texwidth;
	tmax = mt->height >> r_drawsurf.surfmip;
	sourcetstep = texwidth;
	r_stepback = tmax * twidth;

	r_sourcemax = r_source + (tmax * smax);

	soffset = r_drawsurf.surf->texturemins[0];
	basetoffset = r_drawsurf.surf->texturemins[1];

// << 16 components are to guarantee positive values for %
	soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
	basetptr = &r_source[((((basetoffset >> r_drawsurf.surfmip) 
		+ (tmax << 16)) % tmax) * twidth)];

	pcolumndest = r_drawsurf.surfdat;

	for (u=0 ; u<r_numhblocks; u++)
	{
		r_lightptr = blocklights + u;

		prowdestbase = pcolumndest;

		pbasesource = basetptr + soffset;

		(*pblockdrawer)();

		soffset = soffset + blocksize;
		if (soffset >= smax)
			soffset = 0;

		pcolumndest += horzblockstep;
	}
}*/


//inner LightmapFetch variables. Not changing in surface scanlines, and becouse maked globals
unsigned char* current_lightmap_data;
int current_lightmap_size_x;

/*
------per block cache generation
*/
unsigned char b_block_lights[4*4];
unsigned char* b_dst;
int b_cache_width;
const unsigned char* b_tex_data;
int b_tex_size_x1, b_tex_size_y1;
int b_tex_size_x_log2;
int b_tc[2];//initial block texture coords

//inner GenerateCacheBlockMip variables
fixed16_t light_left[3];
fixed16_t d_light_left[3];
fixed16_t light_right[3];
fixed16_t d_light_right[3];
fixed16_t d_line_light[3];
fixed16_t line_light[3];

void GenerateCacheBlockMip0()
{
	const int mip= 0;
	int x, y, i, c, tc_y;
	const unsigned char* src;
	unsigned char* dst;
	unsigned char color[4];

	for( i= 0; i< 3; i++ )
	{
		light_left[i]= 2*(b_block_lights[i]<<16);
		light_right[i]= 2*(b_block_lights[i+4]<<16);
		d_light_left[i]= ( 2*(b_block_lights[i+8]<<16) - light_left[i] )>>(4-mip);
		d_light_right[i]= ( 2*(b_block_lights[i+12]<<16) - light_right[i] )>>(4-mip);
	}

	for( y= 0; y< (1<<(4-mip)); y++ )
	{
		for( i= 0; i< 3; i++ )
		{
			d_line_light[i]= ( (light_right[i]- light_left[i])>>(4-mip) )>>6;
			line_light[i]= light_left[i]>>6;
		}

		tc_y= (b_tc[1]+y)&b_tex_size_y1;
		src= b_tex_data + (tc_y<<b_tex_size_x_log2);
		dst= b_dst + y*b_cache_width;
		for( x= 0; x< (1<<(4-mip)); x++, dst++ )
		{
			*((int*)color)= d_8to24table[ src[(x+b_tc[0])&b_tex_size_x1] ];
			
			c= (line_light[0] * color[0])>>(11+10); if( c > 31 ) c= 31; color[0]= c;
			c= (line_light[1] * color[1])>>(8+10); if( c > 255 ) c= 255; color[1]= c;
			c= (line_light[2] * color[2])>>(8+10); if( c > 255 ) c= 255; color[2]= c;
			c= color[0] | ((color[1]&252)<<3) | ((color[2]&248)<<8);

			line_light[0]+= d_line_light[0];
			line_light[1]+= d_line_light[1];
			line_light[2]+= d_line_light[2];
			*dst= d_16to8table[c];
		}//for x
		
		for( i= 0; i< 3; i++ )
		{
			light_left[i]+= d_light_left[i];
			light_right[i]+= d_light_right[i];
		}
	}//for y
}//GenerateBlock0
void GenerateCacheBlockMip1()
{
	const int mip= 1;
	int x, y, i, c, tc_y;
	const unsigned char* src;
	unsigned char* dst;
	unsigned char color[4];

	for( i= 0; i< 3; i++ )
	{
		light_left[i]= 2*(b_block_lights[i]<<16);
		light_right[i]= 2*(b_block_lights[i+4]<<16);
		d_light_left[i]= ( 2*(b_block_lights[i+8]<<16) - light_left[i] )>>(4-mip);
		d_light_right[i]= ( 2*(b_block_lights[i+12]<<16) - light_right[i] )>>(4-mip);
	}

	for( y= 0; y< (1<<(4-mip)); y++ )
	{
		for( i= 0; i< 3; i++ )
		{
			d_line_light[i]= ( (light_right[i]- light_left[i])>>(4-mip) )>>6;
			line_light[i]= light_left[i]>>6;
		}

		tc_y= (b_tc[1]+y)&b_tex_size_y1;
		src= b_tex_data + (tc_y<<b_tex_size_x_log2);
		dst= b_dst + y*b_cache_width;
		for( x= 0; x< (1<<(4-mip)); x++, dst++ )
		{
			*((int*)color)= d_8to24table[ src[(x+b_tc[0])&b_tex_size_x1] ];
			
			c= (line_light[0] * color[0])>>(11+10); if( c > 31 ) c= 31; color[0]= c;
			c= (line_light[1] * color[1])>>(8+10); if( c > 255 ) c= 255; color[1]= c;
			c= (line_light[2] * color[2])>>(8+10); if( c > 255 ) c= 255; color[2]= c;
			c= color[0] | ((color[1]&252)<<3) | ((color[2]&248)<<8);

			line_light[0]+= d_line_light[0];
			line_light[1]+= d_line_light[1];
			line_light[2]+= d_line_light[2];
			*dst= d_16to8table[c];
		}//for x
		
		for( i= 0; i< 3; i++ )
		{
			light_left[i]+= d_light_left[i];
			light_right[i]+= d_light_right[i];
		}
	}//for y
}//GenerateBlock1
void GenerateCacheBlockMip2()
{
	const int mip= 2;
	int x, y, i, c, tc_y;
	const unsigned char* src;
	unsigned char* dst;
	unsigned char color[4];

	for( i= 0; i< 3; i++ )
	{
		light_left[i]= 2*(b_block_lights[i]<<16);
		light_right[i]= 2*(b_block_lights[i+4]<<16);
		d_light_left[i]= ( 2*(b_block_lights[i+8]<<16) - light_left[i] )>>(4-mip);
		d_light_right[i]= ( 2*(b_block_lights[i+12]<<16) - light_right[i] )>>(4-mip);
	}

	for( y= 0; y< (1<<(4-mip)); y++ )
	{
		for( i= 0; i< 3; i++ )
		{
			d_line_light[i]= ( (light_right[i]- light_left[i])>>(4-mip) )>>6;
			line_light[i]= light_left[i]>>6;
		}

		tc_y= (b_tc[1]+y)&b_tex_size_y1;
		src= b_tex_data + (tc_y<<b_tex_size_x_log2);
		dst= b_dst + y*b_cache_width;
		for( x= 0; x< (1<<(4-mip)); x++, dst++ )
		{
			*((int*)color)= d_8to24table[ src[(x+b_tc[0])&b_tex_size_x1] ];
			
			c= (line_light[0] * color[0])>>(11+10); if( c > 31 ) c= 31; color[0]= c;
			c= (line_light[1] * color[1])>>(8+10); if( c > 255 ) c= 255; color[1]= c;
			c= (line_light[2] * color[2])>>(8+10); if( c > 255 ) c= 255; color[2]= c;
			c= color[0] | ((color[1]&252)<<3) | ((color[2]&248)<<8);

			line_light[0]+= d_line_light[0];
			line_light[1]+= d_line_light[1];
			line_light[2]+= d_line_light[2];
			*dst= d_16to8table[c];
		}//for x
		
		for( i= 0; i< 3; i++ )
		{
			light_left[i]+= d_light_left[i];
			light_right[i]+= d_light_right[i];
		}
	}//for y
}//GenerateBlock2

void GenerateCacheBlockMip3()
{
	const int mip= 3;
	int x, y, i, c, tc_y;
	const unsigned char* src;
	unsigned char* dst;
	unsigned char color[4];

	for( i= 0; i< 3; i++ )
	{
		light_left[i]= 2*(b_block_lights[i]<<16);
		light_right[i]= 2*(b_block_lights[i+4]<<16);
		d_light_left[i]= ( 2*(b_block_lights[i+8]<<16) - light_left[i] )>>(4-mip);
		d_light_right[i]= ( 2*(b_block_lights[i+12]<<16) - light_right[i] )>>(4-mip);
	}

	for( y= 0; y< (1<<(4-mip)); y++ )
	{
		for( i= 0; i< 3; i++ )
		{
			d_line_light[i]= ( (light_right[i]- light_left[i])>>(4-mip) )>>6;
			line_light[i]= light_left[i]>>6;
		}

		tc_y= (b_tc[1]+y)&b_tex_size_y1;
		src= b_tex_data + (tc_y<<b_tex_size_x_log2);
		dst= b_dst + y*b_cache_width;
		for( x= 0; x< (1<<(4-mip)); x++, dst++ )
		{
			*((int*)color)= d_8to24table[ src[(x+b_tc[0])&b_tex_size_x1] ];
			
			c= (line_light[0] * color[0])>>(11+10); if( c > 31 ) c= 31; color[0]= c;
			c= (line_light[1] * color[1])>>(8+10); if( c > 255 ) c= 255; color[1]= c;
			c= (line_light[2] * color[2])>>(8+10); if( c > 255 ) c= 255; color[2]= c;
			c= color[0] | ((color[1]&252)<<3) | ((color[2]&248)<<8);

			line_light[0]+= d_line_light[0];
			line_light[1]+= d_line_light[1];
			line_light[2]+= d_line_light[2];
			*dst= d_16to8table[c];
		}//for x
		
		for( i= 0; i< 3; i++ )
		{
			light_left[i]+= d_light_left[i];
			light_right[i]+= d_light_right[i];
		}
	}//for y
}//GenerateBlock3


void GenSurfacePerBlocks( msurface_t* surf, int mip )
{
	int x, y, cache_x, cache_y;
	int min_x, min_y;
	int block_count_x= surf->extents[0]>>4;
	int block_count_y= surf->extents[1]>>4;

	b_tex_data= r_drawsurf.image->pixels[mip];

	b_tex_size_x1= (r_drawsurf.image->width>>mip)-1;
	b_tex_size_y1= (r_drawsurf.image->height>>mip)-1;
	b_tex_size_x_log2= D_log2(r_drawsurf.image->width) - mip ;

	b_cache_width= r_drawsurf.rowbytes;

	min_x= (surf->texturemins[0]>>mip);
	min_y= (surf->texturemins[1]>>mip);//-1 for border

	current_lightmap_data= (byte*)blocklights;
	current_lightmap_size_x= (surf->extents[0]>>4)+1;

	for( y= 0; y< block_count_y; y++ )
	{
		cache_y= (y<<(4-mip));
		b_tc[1]= min_y + (y<<(4-mip));

		for( x= 0; x< block_count_x; x++ )
		{
			*((int*)b_block_lights     )= ((int*)current_lightmap_data)[ x + y * current_lightmap_size_x ];
			*((int*)(b_block_lights +4))= ((int*)current_lightmap_data)[ x+1 + y * current_lightmap_size_x ];
			*((int*)(b_block_lights +8))= ((int*)current_lightmap_data)[ x + (y+1) * current_lightmap_size_x ];
			*((int*)(b_block_lights+12))= ((int*)current_lightmap_data)[ x+1 + (y+1) * current_lightmap_size_x ];
			cache_x= (x<<(4-mip));
			b_dst= r_drawsurf.surfdat +  cache_x + cache_y * r_drawsurf.rowbytes;
			b_tc[0]= min_x + (x<<(4-mip));
			(surfmiptable[mip])();
		}//for block x
	}//for block y
}


/*
void BuildSurfaceMip3()
{
	const int mip= 3;

	int y, y_end, x, x_end;
	int tc_y;
	byte *src, *dst, *tex_data;
	msurface_t* surf= r_drawsurf.surf;
	int tex_size_x1= (r_drawsurf.image->width>>mip)-1;
	int tex_size_y1= (r_drawsurf.image->height>>mip)-1;
	int tex_size_x= r_drawsurf.image->width>>mip;
	int min_x= (surf->texturemins[0]>>mip);
	int min_y= (surf->texturemins[1]>>mip);

	current_lightmap_data= (byte*)blocklights;//r_drawsurf.surf->samples;
	current_lightmap_size_x= (surf->extents[0]>>4)+1;

	tex_data= r_drawsurf.image->pixels[mip];

#ifdef SURFACE_GEN_USE_MMX
	
	//mm0 - always zero
	
	__asm pxor mm0, mm0
	__asm movq mm1, qword ptr[color_multipler]
#endif

	for(  y= 0, y_end= (surf->extents[1]>>mip); y< y_end; y++ )
	{
		tc_y= (y+min_y) & tex_size_y1;

		dst= r_drawsurf.surfdat + y * r_drawsurf.rowbytes;
		src= tex_data + tc_y*tex_size_x;

		lightmap_dy=  (y<<(16-4-8+mip)) & 255;
		lightmap_dy1= 256 - lightmap_dy;
		lightmap_y= current_lightmap_size_x * (y>>(4-mip))*4;
		lightmap_y1= lightmap_y + current_lightmap_size_x*4;
		SetupColoredLightmapDYVectors();
		for( x= 0, x_end= (surf->extents[0]>>mip); x< x_end; x++, dst++ )
		{
			ALIGN_8 byte color[4];
			int c;
			unsigned short light[4];
			int color_index= src[(x+min_x)&tex_size_x1];
			*((int*)color)= d_8to24table[ color_index ];

			LightmapColoredFetch( x<<(8-4+mip), light ); 
#ifdef SURFACE_GEN_USE_MMX
			__asm
			{
				//mm4 - input lightmap
				movd mm2, dword ptr[color]
				punpcklbw mm2, mm0//convert color bytes to words
				pmullw mm2, mm4//*= lightmap
				pmulhuw mm2, mm1 //=( color * color_multipler )/ 256
				packuswb mm2, mm0
				movd dword ptr[color], mm2
			}
			c= ((color[0]>>3)&31) | ((color[1]&252)<<3) | ((color[2]&248)<<8);
#else
			c= (light[0] * color[0])>>10; if( c > 31 ) c= 31; color[0]= c;
			c= (light[1] * color[1])>>7; if( c > 255 ) c= 255; color[1]= c;
			c= (light[2] * color[2])>>7; if( c > 255 ) c= 255; color[2]= c;
			c= color[0] | ((color[1]&252)<<3) | ((color[2]&248)<<8);
#endif
			*dst= d_16to8table[c];
		}
	}
#ifdef SURFACE_GEN_USE_MMX
	__asm emms
#endif
}
*/


void R_DrawSurface (void)
{
	//(surfmiptable[r_drawsurf.surfmip])();
	GenSurfacePerBlocks( r_drawsurf.surf, r_drawsurf.surfmip );
}


//=============================================================================

#if	!id386

/*
================
R_DrawSurfaceBlock8_mip0
================
*/
void R_DrawSurfaceBlock8_mip0 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 4;
		lightrightstep = (r_lightptr[1] - lightright) >> 4;

		for (i=0 ; i<16 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 4;

			light = lightright;

			for (b=15; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip1
================
*/
void R_DrawSurfaceBlock8_mip1 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 3;
		lightrightstep = (r_lightptr[1] - lightright) >> 3;

		for (i=0 ; i<8 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 3;

			light = lightright;

			for (b=7; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip2
================
*/
void R_DrawSurfaceBlock8_mip2 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 2;
		lightrightstep = (r_lightptr[1] - lightright) >> 2;

		for (i=0 ; i<4 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 2;

			light = lightright;

			for (b=3; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip3
================
*/
void R_DrawSurfaceBlock8_mip3 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 1;
		lightrightstep = (r_lightptr[1] - lightright) >> 1;

		for (i=0 ; i<2 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 1;

			light = lightright;

			for (b=1; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}

#endif


//============================================================================


/*
================
R_InitCaches

================
*/
void R_InitCaches (void)
{
	int		size;
	int		pix;

	// calculate size to allocate
	if (sw_surfcacheoverride->value)
	{
		size = sw_surfcacheoverride->value;
	}
	else
	{
		size = SURFCACHE_SIZE_AT_320X240;

		pix = vid.width*vid.height;
		if (pix > 64000)
			size += (pix-64000)*3;
		size*= 4;
	}		

	// round up to page size
	size = (size + 8191) & ~8191;

	ri.Con_Printf (PRINT_ALL,"%ik surface cache\n", size/1024);

	sc_size = size;
	sc_base = (surfcache_t *)malloc(size);
	sc_rover = sc_base;
	
	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
}


/*
==================
D_FlushCaches
==================
*/
void D_FlushCaches (void)
{
	surfcache_t     *c;
	
	if (!sc_base)
		return;

	for (c = sc_base ; c ; c = c->next)
	{
		if (c->owner)
			*c->owner = NULL;
	}
	
	sc_rover = sc_base;
	sc_base->next = NULL;
	sc_base->owner = NULL;
	sc_base->size = sc_size;
}

/*
=================
D_SCAlloc
=================
*/
surfcache_t     *D_SCAlloc (int width, int size)
{
	surfcache_t             *new;
	qboolean                wrapped_this_time;

	if ((width < 0) || (width > 256))
		ri.Sys_Error (ERR_FATAL,"D_SCAlloc: bad cache width %d\n", width);

	if ((size <= 0) || (size > 0x10000))
		ri.Sys_Error (ERR_FATAL,"D_SCAlloc: bad cache size %d\n", size);
	
	size = (int)&((surfcache_t *)0)->data[size];
	size = (size + 3) & ~3;
	if (size > sc_size)
		ri.Sys_Error (ERR_FATAL,"D_SCAlloc: %i > cache size of %i",size, sc_size);

// if there is not size bytes after the rover, reset to the start
	wrapped_this_time = false;

	if ( !sc_rover || (byte *)sc_rover - (byte *)sc_base > sc_size - size)
	{
		if (sc_rover)
		{
			wrapped_this_time = true;
		}
		sc_rover = sc_base;
	}
		
// colect and free surfcache_t blocks until the rover block is large enough
	new = sc_rover;
	if (sc_rover->owner)
		*sc_rover->owner = NULL;
	
	while (new->size < size)
	{
	// free another
		sc_rover = sc_rover->next;
		if (!sc_rover)
			ri.Sys_Error (ERR_FATAL,"D_SCAlloc: hit the end of memory");
		if (sc_rover->owner)
			*sc_rover->owner = NULL;
			
		new->size += sc_rover->size;
		new->next = sc_rover->next;
	}

// create a fragment out of any leftovers
	if (new->size - size > 256)
	{
		sc_rover = (surfcache_t *)( (byte *)new + size);
		sc_rover->size = new->size - size;
		sc_rover->next = new->next;
		sc_rover->width = 0;
		sc_rover->owner = NULL;
		new->next = sc_rover;
		new->size = size;
	}
	else
		sc_rover = new->next;
	
	new->width = width;
// DEBUG
	if (width > 0)
		new->height = (size - sizeof(*new) + sizeof(new->data)) / width;

	new->owner = NULL;              // should be set properly after return

	if (d_roverwrapped)
	{
		if (wrapped_this_time || (sc_rover >= d_initial_rover))
			r_cache_thrash = true;
	}
	else if (wrapped_this_time)
	{       
		d_roverwrapped = true;
	}

	return new;
}


/*
=================
D_SCDump
=================
*/
void D_SCDump (void)
{
	surfcache_t             *test;

	for (test = sc_base ; test ; test = test->next)
	{
		if (test == sc_rover)
			ri.Con_Printf (PRINT_ALL,"ROVER:\n");
		ri.Con_Printf (PRINT_ALL,"%p : %i bytes     %i width\n",test, test->size, test->width);
	}
}

//=============================================================================

// if the num is not a power of 2, assume it will not repeat

int     MaskForNum (int num)
{
	if (num==128)
		return 127;
	if (num==64)
		return 63;
	if (num==32)
		return 31;
	if (num==16)
		return 15;
	return 255;
}


//=============================================================================

/*
================
D_CacheSurface
================
*/
surfcache_t *D_CacheSurface (msurface_t *surface, int miplevel)
{
	surfcache_t     *cache;

//
// if the surface is animating or flashing, flush the cache
//
	r_drawsurf.image = R_TextureAnimation (surface->texinfo);
	r_drawsurf.lightadj[0] = r_newrefdef.lightstyles[surface->styles[0]].white*128;
	r_drawsurf.lightadj[1] = r_newrefdef.lightstyles[surface->styles[1]].white*128;
	r_drawsurf.lightadj[2] = r_newrefdef.lightstyles[surface->styles[2]].white*128;
	r_drawsurf.lightadj[3] = r_newrefdef.lightstyles[surface->styles[3]].white*128;
	
//
// see if the cache holds apropriate data
//
	cache = surface->cachespots[miplevel];

	if (cache && !cache->dlight && surface->dlightframe != r_framecount
			&& cache->image == r_drawsurf.image
			&& cache->lightadj[0] == r_drawsurf.lightadj[0]
			&& cache->lightadj[1] == r_drawsurf.lightadj[1]
			&& cache->lightadj[2] == r_drawsurf.lightadj[2]
			&& cache->lightadj[3] == r_drawsurf.lightadj[3] )
		return cache;

//
// determine shape of surface
//
	surfscale = 1.0 / (1<<miplevel);
	r_drawsurf.surfmip = miplevel;
	r_drawsurf.surfwidth = surface->extents[0] >> miplevel;
	r_drawsurf.rowbytes = r_drawsurf.surfwidth;
	r_drawsurf.surfheight = surface->extents[1] >> miplevel;
	
//
// allocate memory if needed
//
	if (!cache)     // if a texture just animated, don't reallocate it
	{
		cache = D_SCAlloc (r_drawsurf.surfwidth,
						   r_drawsurf.surfwidth * r_drawsurf.surfheight);
		surface->cachespots[miplevel] = cache;
		cache->owner = &surface->cachespots[miplevel];
		cache->mipscale = surfscale;
	}
	
	if (surface->dlightframe == r_framecount)
		cache->dlight = 1;
	else
		cache->dlight = 0;

	r_drawsurf.surfdat = (pixel_t *)cache->data;
	
	cache->image = r_drawsurf.image;
	cache->lightadj[0] = r_drawsurf.lightadj[0];
	cache->lightadj[1] = r_drawsurf.lightadj[1];
	cache->lightadj[2] = r_drawsurf.lightadj[2];
	cache->lightadj[3] = r_drawsurf.lightadj[3];

//
// draw and light the surface texture
//
	r_drawsurf.surf = surface;

	c_surf++;

	// calculate the lightings
	R_BuildLightMap ();
	
	// rasterize the surface into the cache
	R_DrawSurface ();

	return cache;
}


