/*----------------------------------------------------------------------------
 * avs2bdnxml - Generates BluRay subtitle stuff from RGBA AviSynth scripts
 * Copyright (C) 2008-2013 Arne Bochem <avs2bdnxml at ps-auxw de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *----------------------------------------------------------------------------
 * Version 2.09
 *   - Added parameter -F to mark all subtitles forced
 *
 * Version 2.08
 *   - Fix PNG filename references when used with timecode offsets
 *
 * Version 2.07
 *   - Fix two bugs in timecode parsing function
 *   - Move most inline assembly to yasm
 *
 * Version 2.06
 *   - Add option to add an offset to output timecodes
 *   - Change return value when no events were detected to 0
 *   - Add option to allow writing of empty output files
 *   - Add option to enforce even Y coordinates (may help avoid problems with
 *     interlaced video in DVD related use cases)
 *   - Fix SSE2 with more recent GCC versions
 *   - Add option for stricter checks on buffer/palette limits for SUPs
 *   - Fix bug where LastEventOutTC could be one frame early
 *   - Add additional checks to commandline argument parsing
 *   - PNG files are now always placed in the same directory as the XML file
 *
 * Version 2.05
 *   - Fix crash when using XML and SUP output
 *
 * Version 2.04
 *   - Minor fixes
 *   - Code cleanup regarding linked lists
 *   - Add new minimal window finding code
 *   - Fix PGS output more, especially WDS handling
 *   - Fix old bug in RLE encoder
 *
 * Version 2.03
 *   - Output some statistics about previous epoch in pgsparse in end packet
 *   - Fix unnecessary epoch splits due to bad buffer calculation and more.
 *   - Some small fixes
 *
 * Version 2.02
 *   - It is now possible to use -o twice, with different output formats
 *   - Corrections to SUP timestamp calculation
 *   - PGS decode buffer should contain palettized image data, not RGBA.
 *     Buffer usage calculation fixed.
 *
 * Version 2.01
 *   - Restrict number of composition objects within an epoch to 64
 *
 * Version 2.00
 *   - Options --seek and --count were added, to allow for partial processing
 *     of input video
 *   - For debug purposes, a stand-alone SUP file parser was added in debug/
 *   - Experimental SUP output support
 *
 * Version 1.13
 *   - By default, images are no longer split into to composition objects,
 *     if the result would look ugly. For example, descenders should no
 *     longer get put into their own image. Reverting back to the old
 *     behaviour is possible using the --ugly option.
 *   - Ensure minimum size of 8x8 for composition objects
 *
 * Version 1.12
 *   - Fix error in example
 *   - Fix double free, which could result in broken images when image
 *     splitting and palletized output were enabled.
 *
 * Version 1.11
 *   - Add option to not split lines, if the rest isn't a minimum
 *     of 3 (default) frames long
 *   - Images are now autocropped by default
 *   - Images are now converted to 8bit palette by default
 *   - Misc bugfixes that should've been fixed before
 *
 * Version 1.10b
 *   - Splitting of images is now disabled by default
 *
 * Version 1.10
 *   - Add option to split events longer than a given number of frames, to
 *     allow better seeking
 *   - Change image filename format to XXXXXXXX_X.png
 *   - Add option to optimize for Presentation Graphics buffer usage, by
 *     auto-cropping images, and splitting them into two separate ones to
 *     minimize surface area, when necessary (enabled by default)
 *   - Use getopts for commandline argument parsing
 *   - Add Makefile
 *
 * Version 1.9
 *   - Correct calculation of timecodes. Integer only, now.
 *   - Remove support for drop timecodes. If anybody really needs them, and
 *     has a way to check calculated timecodes are correct, please tell me.
 *
 * Version 1.8
 *   - Fix crash with certain frame sizes (unaligned buffer used with SSE2)
 *
 * Version 1.7
 *   - Terminate using exit/return instead of abort (no more "crashes")
 *
 * Version 1.6
 *   - Zero transparent pixels to guard against invisible color information
 *     (Slight slowdown, but more robustness)
 *   - Add SSE2 runtime detection
 *   - Correctly set DropFrame attribute
 *
 * Version 1.5
 *   - Validate framerates
 *   - Make non-drop timecodes the default for 23.976, 29.97 and 59.94
 *   - Fix bad timecode calculation due to float math optimization
 *   - Add integer SSE2 optimization (but most time is spent in AviSynth)
 *   - Don't uselessly swap R and B channels
 *
 * Version 1.4
 *   - Add 30 to framerate list
 *   - Enhance timecode precision
 *
 * Version 1.3
 *   - Read frames correctly in avs2bdnxml. No more FlipVertical() necessary.
 *
 * Version 1.2
 *   - Fixed bug, where only the bottom part of the image was checked for
 *     changes
 *
 * Version 1.1
 *   - Fixed crash bug with videos shorter than 1000 frames.
 *   - Convert from BGR to RGB, colors in PNGs should be correct now.
 *
 * Version 1.0
 *   - Initial release. Seems to work.
 *----------------------------------------------------------------------------
 * Thanks to:
 *   - hamletiii on Doom9 for a steady flow of feedback.
 *   - Daiz in #x264 for inspiration, testing, and bug reports.
 *   - Loren Merritt and Laurent Aimar for writing x264's AVS reader code.
 *----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <png.h>
#include <getopt.h>
#include <assert.h>
#include "auto_split.h"
#include "palletize.h"
#include "sup.h"
#include "ass.h"
#include "abstract_lists.h"

/* AVIS input code taken from muxers.c from the x264 project (GPLv2 or later).
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *          Loren Merritt <lorenm@u.washington.edu>
 * Slightly modified, so if you find breakage, it's probably mine. :)
 */
#ifndef LINUX
#include <windows.h>
#include <vfw.h>
#endif

#ifndef LINUX
static int64_t gcd( int64_t a, int64_t b )
{
    while (1)
    {
        int64_t c = a % b;
        if( !c )
            return b;
        a = b;
        b = c;
    }
}
#endif

typedef struct {
#if !defined(LINUX)
    PAVISTREAM p_avi;
#else
    int fps_den;
    int fps_num;
    int frames;
    FILE *fh;
#endif
    int width, height;
} avis_input_t;

typedef struct {
    int i_width;
    int i_height;
    int i_fps_den;
    int i_fps_num;
} stream_info_t;

int open_file_avis( char *psz_filename, avis_input_t **p_handle, stream_info_t *p_param )
{
    avis_input_t *h = malloc(sizeof(avis_input_t));
#if !defined(LINUX)
    AVISTREAMINFO info;
    int i;

    *p_handle = h;

    AVIFileInit();
    if( AVIStreamOpenFromFile( &h->p_avi, psz_filename, streamtypeVIDEO, 0, OF_READ, NULL ) )
    {
        AVIFileExit();
        return -1;
    }

    if( AVIStreamInfo(h->p_avi, &info, sizeof(AVISTREAMINFO)) )
    {
        AVIStreamRelease(h->p_avi);
        AVIFileExit();
        return -1;
    }

    /* Check input format */
    if (info.fccHandler != MAKEFOURCC('D', 'I', 'B', ' '))
    {
        fprintf( stderr, "avis [error]: unsupported input format (%c%c%c%c)\n",
            (char)(info.fccHandler & 0xff), (char)((info.fccHandler >> 8) & 0xff),
            (char)((info.fccHandler >> 16) & 0xff), (char)((info.fccHandler >> 24)) );

        AVIStreamRelease(h->p_avi);
        AVIFileExit();

        return -1;
    }

    h->width =
    p_param->i_width = info.rcFrame.right - info.rcFrame.left;
    h->height =
    p_param->i_height = info.rcFrame.bottom - info.rcFrame.top;
    i = gcd(info.dwRate, info.dwScale);
    p_param->i_fps_den = info.dwScale / i;
    p_param->i_fps_num = info.dwRate / i;

    fprintf( stderr, "avis [info]: %dx%d @ %.2f fps (%d frames)\n",
        p_param->i_width, p_param->i_height,
        (double)p_param->i_fps_num / (double)p_param->i_fps_den,
        (int)info.dwLength );

    return 0;
#else
    *p_handle = h;
    p_param->i_width = 1920;
    p_param->i_height = 1080;
    p_param->i_fps_den = 30000;
    p_param->i_fps_num = 1001;
    h->width = p_param->i_width;
    h->height = p_param->i_height;
    h->fps_den = p_param->i_fps_den;
    h->fps_num = p_param->i_fps_num;
    h->fh = fopen(psz_filename, "r");
    h->frames = 15000;
		return 0;
#endif
}

int get_frame_total_avis( avis_input_t *handle )
{
#if !defined(LINUX)
    avis_input_t *h = handle;
    AVISTREAMINFO info;

    if( AVIStreamInfo(h->p_avi, &info, sizeof(AVISTREAMINFO)) )
        return -1;

    return info.dwLength;
#else
    return handle->frames;
#endif
}

int read_frame_avis( char *p_pic, avis_input_t *handle, int i_frame )
{
#if !defined(LINUX)
    avis_input_t *h = handle;

    if( AVIStreamRead(h->p_avi, i_frame, 1, p_pic, h->width * h->height * 4, NULL, NULL ) )
        return -1;

    return 0;
#else
    fread(p_pic, 4, handle->width * handle->height, handle->fh);
    return 0;
#endif
}

int close_file_avis( avis_input_t *handle )
{
#if !defined(LINUX)
    avis_input_t *h = handle;
    AVIStreamRelease(h->p_avi);
    AVIFileExit();
    free(h);
    return 0;
#else
    fclose((FILE *)handle);
    return 0;
#endif
}

/* AVIS code ends here */

/* Main avs2bdnxml code starts here, too */

void get_dir_path(char *filename, char *dir_path)
{
	char abs_path[MAX_PATH + 1] = {0};
	char drive[3] = {0};
	char dir[MAX_PATH + 1] = {0};

	/* Get absolute path of output XML file */
	if (_fullpath(abs_path, filename, MAX_PATH) == NULL)
	{
		fprintf(stderr, "Cannot determine absolute path for: %s\n", filename);
		exit(1);
	}

	/* Split absolute path into components */
	_splitpath(abs_path, drive, dir, NULL, NULL);
	strncpy(dir_path, drive, 2);
	strncat(dir_path, dir, MAX_PATH - 2);

	if (strlen(dir_path) > MAX_PATH - 16)
	{
		fprintf(stderr, "Path for PNG files too long.\n");
		exit(1);
	}
}

void write_png(char *dir, int file_id, uint8_t *image, int w, int h, int graphic, uint32_t *pal, crop_t c)
{
	FILE *fh;
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep *row_pointers;
	png_colorp palette = NULL;
	png_bytep trans = NULL;
	char tmp[16] = {0};
	char filename[MAX_PATH + 1] = {0};
	char *col;
	int step = pal == NULL ? 4 : 1;
	int colors = 0;
	int i;

	snprintf(tmp, 15, "%08d_%d.png", file_id, graphic);
	strncpy(filename, dir, MAX_PATH);
	strncat(filename, tmp, 15);

	if ((fh = fopen(filename, "wb")) == NULL)
	{
		perror("Cannot open PNG file for writing");
		exit(1);
	}

	/* Initialize png struct */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL)
	{
		fprintf(stderr, "Cannot create png_ptr.\n");
		exit(1);
	}

	/* Initialize info struct */
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		fprintf(stderr, "Cannot create info_ptr.\n");
		exit(1);
	}

	/* Set long jump stuff (weird..?) */
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fh);
		fprintf(stderr, "Error while writing PNG file: %s\n", filename);
		exit(1);
	}

	/* Initialize IO */
	png_init_io(png_ptr, fh);

	/* Set file info */
	if (pal == NULL)
		png_set_IHDR(png_ptr, info_ptr, c.w, c.h, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	else
	{
		png_set_IHDR(png_ptr, info_ptr, c.w, c.h, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		palette = calloc(256, sizeof(png_color));
		trans = calloc(256, sizeof(png_byte));
		colors = 1;
		for (i = 1; i < 256 && pal[i]; i++)
		{
			col = (char *)&(pal[i]);
			palette[i].red = col[0];
			palette[i].green = col[1];
			palette[i].blue = col[2];
			trans[i] = col[3];
			colors++;
		}
		png_set_PLTE(png_ptr, info_ptr, palette, colors);
		png_set_tRNS(png_ptr, info_ptr, trans, colors, NULL);
	}

	/* Allocate row pointer memory */
	row_pointers = calloc(c.h, sizeof(png_bytep));

	/* Set row pointers */
	image = image + step * (c.x + w * c.y);
	for (i = 0; i < c.h; i++)
	{
		row_pointers[i] = image + i * w * step;
	}
	png_set_rows(png_ptr, info_ptr, row_pointers);

	/* Set compression */
	png_set_filter(png_ptr, 0, PNG_FILTER_VALUE_SUB);
	png_set_compression_level(png_ptr, 5);

	/* Write image */
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	/* Free memory */
	png_destroy_write_struct(&png_ptr, &info_ptr);
	free(row_pointers);
	if (palette != NULL)
		free(palette);
	if (trans != NULL)
		free(trans);

	/* Close file handle */
	fclose(fh);
}

extern int asm_is_identical_sse2 (stream_info_t *s_info, char *img, char *img_old);
extern int asm_is_empty_sse2 (stream_info_t *s_info, char *img);
extern void asm_zero_transparent_sse2 (stream_info_t *s_info, char volatile *img);
extern void asm_swap_rb_sse2 (stream_info_t *s_info, char volatile *img, char volatile *out);

int is_identical_c (stream_info_t *s_info, char *img, char *img_old)
{
	uint32_t *max = (uint32_t *)(img + s_info->i_width * s_info->i_height * 4);
	uint32_t *im = (uint32_t *)img;
	uint32_t *im_old = (uint32_t *)img_old;

	while (im < max)
	{
		if (!((char *)im)[3])
			*im = 0;
		if (*(im++) ^ *(im_old++))
			return 0;
	}

	return 1;
}

int is_empty_c (stream_info_t *s_info, char *img)
{
	char *max = img + s_info->i_width * s_info->i_height * 4;
	char *im = img;

	while (im < max)
	{
		if (im[3])
			return 0;
		im += 4;
	}

	return 1;
}

void zero_transparent_c (stream_info_t *s_info, char *img)
{
	char *max = img + s_info->i_width * s_info->i_height * 4;
	char *im = img;

	while (im < max)
	{
		if (!im[3])
			*(uint32_t *)img = 0;
		im += 4;
	}
}

void swap_rb_c (stream_info_t *s_info, char *img, char *out)
{
	char *max = img + s_info->i_width * s_info->i_height * 4;

	while (img < max)
	{
		out[0] = img[2];
		out[1] = img[1];
		out[2] = img[0];
		out[3] = img[3];
		img += 4;
		out += 4;
	}
}

int detect_sse2 ()
{
	static int detection = -1;
	unsigned int func = 0x00000001;
	unsigned int eax, ebx, ecx, edx;

	if (detection != -1)
		return detection;

	asm volatile
	(
		"cpuid\n"
		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		: "a" (func)
	);

	/* SSE2:  edx & 0x04000000
	 * SSSE3: ecx & 0x00000200
	 */
	detection = (edx & 0x04000000) ? 1 : 0;

	if (detection)
		fprintf(stderr, "CPU: Using SSE2 optimized functions.\n");
	else
		fprintf(stderr, "CPU: Using pure C functions.\n");

	return detection;
}

int is_identical (stream_info_t *s_info, char *img, char *img_old)
{
	if (detect_sse2())
		return asm_is_identical_sse2(s_info, img, img_old);
	else
		return is_identical_c(s_info, img, img_old);
}

int is_empty (stream_info_t *s_info, char *img)
{
	if (detect_sse2())
		return asm_is_empty_sse2(s_info, img);
	else
		return is_empty_c(s_info, img);
}

void zero_transparent (stream_info_t *s_info, char *img)
{
	if (detect_sse2())
		return asm_zero_transparent_sse2(s_info, img);
	else
		return zero_transparent_c(s_info, img);
}

void swap_rb (stream_info_t *s_info, char *img, char *out)
{
	if (detect_sse2())
		return asm_swap_rb_sse2(s_info, img, out);
	else
		return swap_rb_c(s_info, img, out);
}

/* SMPTE non-drop time code */
void mk_timecode (int frame, int fps, char *buf) /* buf must have length 12 (incl. trailing \0) */
{
	int frames, s, m, h;
	int tc = frame;

	tc = frame;
	frames = tc % fps;
	tc /= fps;
	s = tc % 60;
	tc /= 60;
	m = tc % 60;
	tc /= 60;
	h = tc;

	if (h > 99)
	{
		fprintf(stderr, "Timecodes above 99:59:59:99 not supported: %u:%02u:%02u:%02u\n", h, m, s, frames);
		exit(1);
	}

	if (snprintf(buf, 12, "%02d:%02d:%02d:%02d", h, m, s, frames) != 11)
	{
		fprintf(stderr, "Timecode lead to invalid format: %s\n", buf);
		exit(1);
	}
}

void print_usage ()
{
	fprintf(stderr,
		"avs2bdnxml 2.09\n\n"
		"Usage: avs2bdnxml [options] -o output input\n\n"
		"Input has to be an AviSynth script with RGBA as output colorspace\n\n"
		"  -o, --output <string>        Output file in BDN XML format\n"
		"                               For SUP/PGS output, use a .sup extension\n"
		"  -j, --seek <integer>         Start processing at this frame, first is 0\n"
		"  -c, --count <integer>        Number of input frames to process\n"
		"  -t, --trackname <string>     Name of track, like: Undefined\n"
		"  -l, --language <string>      Language code, like: und\n"
		"  -v, --video-format <string>  Either of: 480i, 480p,  576i,\n"
		"                                          720p, 1080i, 1080p\n"
		"  -f, --fps <float>            Either of: 23.976, 24, 25, 29.97, 50, 59.94\n"
		"  -x, --x-offset <integer>     X offset, for use with partial frames.\n"
		"  -y, --y-offset <integer>     Y offset, for use with partial frames.\n"
		"  -d, --t-offset <string>      Offset timecodes by this many frames or\n"
		"                               given non-drop timecode (HH:MM:SS:FF).\n"
		"  -s, --split-at <integer>     Split events longer than this, in frames.\n"
		"                               Disabled when 0, which is the default.\n"
		"  -m, --min-split <integer>    Minimum length of line segment after split.\n"
		"  -e, --even-y <integer>       Enforce even Y coordinates. [on=1, off=0]\n"
		"  -a, --autocrop <integer>     Automatically crop output. [on=1, off=0]\n"
		"  -p, --palette <integer>      Output 8bit palette PNG. [on=1, off=0]\n"
		"  -n, --null-xml <integer>     Allow output of empty XML files. [on=1, off=0]\n"
		"  -z, --stricter <integer>     Stricter checks in the SUP writer. May lead to\n"
		"                               less optimized buffer use, but might raise\n"
		"                               compatibility. [on=1, off=0]\n"
		"  -u, --ugly <integer>         Allow splitting images in ugly ways.\n"
		"                               Might improve buffer problems, but is ugly.\n"
		"                               [on=1, off=0]\n"
		"  -b, --buffer-opt <integer>   Optimize PG buffer size by image\n"
		"                               splitting. [on=1, off=0]\n"
        "  -F, --forced <integer>       mark all subtitles as forced [on=1, off=0]\n\n"
		"Example:\n"
		"  avs2bdnxml -t Undefined -l und -v 1080p -f 23.976 -a1 -p1 -b0 -m3 \\\n"
		"    -u0 -e0 -n0 -z0 -o output.xml input.avs\n"
		"  (Input and output are required settings. The rest are set to default.)\n"
		);
}

extern char *rindex(const char *s, int c);
int is_extension(char *filename, char *check_ext)
{
	char *ext = rindex(filename, '.');

	if (ext == NULL)
		return 0;

	ext++;
	if (!strcasecmp(ext, check_ext))
		return 1;

	return 0;
}

int parse_int(char *in, char *name, int *error)
{
	char *end;
	int r;
	errno = 0;
	if (error != NULL)
		*error = 0;
	r = strtol(in, &end, 10);
	if (errno || end == in || end != in + strlen(in))
	{
		if (error != NULL)
			*error = 1;
		if (name != NULL)
		{
			fprintf(stderr, "Error: Failed to parse integer (%s): %s\n", name, in);
			exit(1);
		}
	}
	return r;
}

int parse_tc(char *in, int fps)
{
	int r = 0;
	int e;
	int h, m, s, f;

	/* Test for raw frame number. */
	r = parse_int(in, NULL, &e);
	if (!e)
		return r;

	if (strlen(in) != 2 * 4 + 3 || in[2] != ':' || in[5] != ':' || in[8] != ':')
	{
		fprintf(stderr, "Error: Invalid timecode offset. Expected FRAMENUMBER or HH:MM:SS:FF, but got: %s\n", in);
		exit(1);
	}
	in[2] = 0;
	in[5] = 0;
	in[8] = 0;
	h = parse_int(in,     "t-offset hours",   NULL);
	m = parse_int(in + 3, "t-offset minutes", NULL);
	s = parse_int(in + 6, "t-offset seconds", NULL);
	f = parse_int(in + 9, "t-offset frames",  NULL);
	r = f;
	r += s * fps;
	fps *= 60;
	r += m * fps;
	fps *= 60;
	r += h * fps;
	return r;
}

typedef struct event_s
{
	int image_number;
	int start_frame;
	int end_frame;
	int graphics;
    int forced;
	crop_t c[2];
} event_t;

STATIC_LIST(event, event_t)

void add_event_xml_real (event_list_t *events, int image, int start, int end, int graphics, crop_t *crops, int forced)
{
	event_t *new = calloc(1, sizeof(event_t));
	new->image_number = image;
	new->start_frame = start;
	new->end_frame = end;
	new->graphics = graphics;
	new->c[0] = crops[0];
	new->c[1] = crops[1];
    new->forced = forced;
	event_list_insert_after(events, new);
}

void add_event_xml (event_list_t *events, int split_at, int min_split, int start, int end, int graphics, crop_t *crops, int forced)
{
	int image = start;
	int d = end - start;

	if (!split_at)
		add_event_xml_real(events, image, start, end, graphics, crops, forced);
	else
	{
		while (d >= split_at + min_split)
		{
			d -= split_at;
			add_event_xml_real(events, image, start, start + split_at, graphics, crops, forced);
			start += split_at;
		}
		if (d)
			add_event_xml_real(events, image, start, start + d, graphics, crops, forced);
	}
}

void write_sup_wrapper (sup_writer_t *sw, uint8_t *im, int num_crop, crop_t *crops, uint32_t *pal, int start, int end, int split_at, int min_split, int stricter, int forced)
{
	int d = end - start;

	if (!split_at)
		write_sup(sw, im, num_crop, crops, pal, start, end, stricter, forced);
	else
	{
		while (d >= split_at + min_split)
		{
			d -= split_at;
			write_sup(sw, im, num_crop, crops, pal, start, start + split_at, stricter, forced);
			start += split_at;
		}
		if (d)
			write_sup(sw, im, num_crop, crops, pal, start, start + d, stricter, forced);
	}
}


struct framerate_entry_s
{
	char *name;
	char *out_name;
	int rate;
	int drop;
	int fps_num;
	int fps_den;
};

/* Most of the time seems to be spent in AviSynth (about 4/5). */
int main (int argc, char *argv[])
{
	struct framerate_entry_s framerates[] = { {"23.976", "23.976", 24, 0, 24000, 1001}
	                                        /*, {"23.976d", "23.976", 24000/1001.0, 1}*/
	                                        , {"24", "24", 24, 0, 24, 1}
	                                        , {"25", "25", 25, 0, 25, 1}
	                                        , {"29.97", "29.97", 30, 0, 30000, 1001}
	                                        /*, {"29.97d", "29.97", 30000/1001.0, 1}*/
	                                        , {"50", "50", 50, 0, 50, 1}
	                                        , {"59.94", "59.94", 60, 0, 60000, 1001}
	                                        /*, {"59.94d", "59.94", 60000/1001.0, 1}*/
	                                        , {NULL, NULL, 0, 0, 0, 0}
	                                        };
	char *avs_filename = NULL;
	char *track_name = "Undefined";
	char *language = "und";
	char *video_format = "1080p";
	char *frame_rate = "23.976";
	char *out_filename[2] = {NULL, NULL};
	char *sup_output_fn = NULL;
	char *xml_output_fn = NULL;
	char *x_offset = "0";
	char *y_offset = "0";
	char *t_offset = "0";
	char *buffer_optimize = "0";
	char *split_after = "0";
	char *minimum_split = "3";
	char *palletize_png = "1";
	char *even_y_string = "0";
	char *auto_crop_image = "1";
	char *ugly_option = "0";
	char *seek_string = "0";
	char *allow_empty_string = "0";
	char *stricter_string = "0";
	char *count_string = "2147483647";
	char *in_img = NULL, *old_img = NULL, *tmp = NULL, *out_buf = NULL;
	char *intc_buf = NULL, *outtc_buf = NULL;
	char *drop_frame = NULL;
    char *mark_forced_string = "0";
	char png_dir[MAX_PATH + 1] = {0};
	crop_t crops[2];
	pic_t pic;
	uint32_t *pal = NULL;
	int out_filename_idx = 0;
	int have_fps = 0;
	int n_crop = 1;
	int split_at = 0;
	int min_split = 3;
	int autocrop = 0;
	int xo, yo, to;
	int fps = 25;
	int count_frames = INT_MAX, last_frame;
	int init_frame = 0;
	int frames;
	int first_frame = -1, start_frame = -1, end_frame = -1;
	int num_of_events = 0;
	int i, c, j;
	int have_line = 0;
	int must_zero = 0;
	int checked_empty;
	int even_y = 0;
	int auto_cut = 0;
	int pal_png = 1;
	int ugly = 0;
	int progress_step = 1000;
	int buffer_opt;
	int bench_start = time(NULL);
	int fps_num = 25, fps_den = 1;
	int sup_output = 0;
	int xml_output = 0;
	int allow_empty = 0;
	int stricter = 0;
    int mark_forced = 0;
	sup_writer_t *sw = NULL;
	avis_input_t *avis_hnd;
	stream_info_t *s_info = malloc(sizeof(stream_info_t));
	event_list_t *events = event_list_new();
	event_t *event;
	FILE *fh;

	/* Get args */
	if (argc < 2)
	{
		print_usage();
		return 0;
	}
	while (1)
	{
		static struct option long_options[] =
			{ {"output",       required_argument, 0, 'o'}
			, {"seek",         required_argument, 0, 'j'}
			, {"count",        required_argument, 0, 'c'}
			, {"trackname",    required_argument, 0, 't'}
			, {"language",     required_argument, 0, 'l'}
			, {"video-format", required_argument, 0, 'v'}
			, {"fps",          required_argument, 0, 'f'}
			, {"x-offset",     required_argument, 0, 'x'}
			, {"y-offset",     required_argument, 0, 'y'}
			, {"t-offset",     required_argument, 0, 'd'}
			, {"split-at",     required_argument, 0, 's'}
			, {"min-split",    required_argument, 0, 'm'}
			, {"autocrop",     required_argument, 0, 'a'}
			, {"even-y",       required_argument, 0, 'e'}
			, {"palette",      required_argument, 0, 'p'}
			, {"buffer-opt",   required_argument, 0, 'b'}
			, {"ugly",         required_argument, 0, 'u'}
			, {"null-xml",     required_argument, 0, 'n'}
			, {"stricter",     required_argument, 0, 'z'}
			, {"forced",       required_argument, 0, 'F'}
			, {0, 0, 0, 0}
			};
			int option_index = 0;

			c = getopt_long(argc, argv, "o:j:c:t:l:v:f:x:y:d:b:s:m:e:p:a:u:n:z:F:", long_options, &option_index);
			if (c == -1)
				break;
			switch (c)
			{
				case 'o':
					if (out_filename_idx < 2)
						out_filename[out_filename_idx++] = optarg;
					else
					{
						fprintf(stderr, "No more than two output filenames allowed.\nIf more than one is used, the other must have a\ndifferent output format.\n");
						exit(0);
					}
					break;
				case 'j':
					seek_string = optarg;
					break;
				case 'c':
					count_string = optarg;
					break;
				case 't':
					track_name = optarg;
					break;
				case 'l':
					language = optarg;
					break;
				case 'v':
					video_format = optarg;
					break;
				case 'f':
					frame_rate = optarg;
					break;
				case 'x':
					x_offset = optarg;
					break;
				case 'y':
					y_offset = optarg;
					break;
				case 'd':
					t_offset = optarg;
					break;
				case 'e':
					even_y_string = optarg;
					break;
				case 'p':
					palletize_png = optarg;
					break;
				case 'a':
					auto_crop_image = optarg;
					break;
				case 'b':
					buffer_optimize = optarg;
					break;
				case 's':
					split_after = optarg;
					break;
				case 'm':
					minimum_split = optarg;
					break;
				case 'u':
					ugly_option = optarg;
					break;
				case 'n':
					allow_empty_string = optarg;
					break;
				case 'z':
					stricter_string = optarg;
					break;
				case 'F':
					mark_forced_string = optarg;
					break;
				default:
					print_usage();
					return 0;
					break;
			}
	}
	if (argc - optind == 1)
		avs_filename = argv[optind];
	else
	{
		fprintf(stderr, "Only a single input file allowed.\n");
		return 1;
	}

	/* Both input and output filenames are required */
	if (avs_filename == NULL)
	{
		print_usage();
		return 0;
	}
	if (out_filename[0] == NULL)
	{
		print_usage();
		return 0;
	}

	/* Get target output format */
	for (i = 0; i < out_filename_idx; i++)
	{
		if (is_extension(out_filename[i], "xml"))
		{
			xml_output_fn = out_filename[i];
			xml_output++;
			get_dir_path(xml_output_fn, png_dir);
		}
		else if (is_extension(out_filename[i], "sup") || is_extension(out_filename[i], "pgs"))
		{
			sup_output_fn = out_filename[i];
			sup_output++;
			pal_png = 1;
		}
		else
		{
			fprintf(stderr, "Output file extension must be \".xml\", \".sup\" or \".pgs\".\n");
			return 1;
		}
	}
	if (sup_output > 1 || xml_output > 1)
	{
		fprintf(stderr, "If more than one output filename is used, they must have\ndifferent output formats.\n");
		exit(0);
	}

	/* Set X and Y offsets, and split value */
	xo = parse_int(x_offset, "x-offset", NULL);
	yo = parse_int(y_offset, "y-offset", NULL);
	pal_png = parse_int(palletize_png, "palette", NULL);
	even_y = parse_int(even_y_string, "even-y", NULL);
	autocrop = parse_int(auto_crop_image, "autocrop", NULL);
	split_at = parse_int(split_after, "split-at", NULL);
	ugly = parse_int(ugly_option, "ugly", NULL);
	allow_empty = parse_int(allow_empty_string, "null-xml", NULL);
	stricter = parse_int(stricter_string, "stricter", NULL);
	init_frame = parse_int(seek_string, "seek", NULL);
	count_frames = parse_int(count_string, "count", NULL);
	min_split = parse_int(minimum_split, "min-split", NULL);
	if (!min_split)
		min_split = 1;
	mark_forced = parse_int(mark_forced_string, "forced", NULL);

	/* TODO: Sanity check video_format and frame_rate. */

	/* Get frame rate */
	i = 0;
	while (framerates[i].name != NULL)
	{
		if (!strcasecmp(framerates[i].name, frame_rate))
		{
			frame_rate = framerates[i].out_name;
			fps = framerates[i].rate;
			drop_frame = framerates[i].drop ? "true" : "false";
			fps_num = framerates[i].fps_num;
			fps_den = framerates[i].fps_den;
			have_fps = 1;
		}
		i++;
	}
	if (!have_fps)
	{
		fprintf(stderr, "Error: Invalid framerate (%s).\n", frame_rate);
		return 1;
	}

	/* Get timecode offset. */
	to = parse_tc(t_offset, fps);

	/* Detect CPU features */
	detect_sse2();

	/* Get video info and allocate buffer */
	if (open_file_avis(avs_filename, &avis_hnd, s_info))
	{
		print_usage();
		return 1;
	}
	in_img  = calloc(s_info->i_width * s_info->i_height * 4 + 16 * 2, sizeof(char)); /* allocate + 16 for alignment, and + n * 16 for over read/write */
	old_img = calloc(s_info->i_width * s_info->i_height * 4 + 16 * 2, sizeof(char)); /* see above */
	out_buf = calloc(s_info->i_width * s_info->i_height * 4 + 16 * 2, sizeof(char));

	/* Check minimum size */
	if (s_info->i_width < 8 || s_info->i_height < 8)
	{
		fprintf(stderr, "Error: Video dimensions below 8x8 (%dx%d).\n", s_info->i_width, s_info->i_height);
		return 1;
	}

	/* Align buffers */
	in_img  = in_img + (short)(16 - ((long)in_img % 16));
	old_img = old_img + (short)(16 - ((long)old_img % 16));
	out_buf = out_buf + (short)(16 - ((long)out_buf % 16));

	/* Set up buffer (non-)optimization */
	buffer_opt = parse_int(buffer_optimize, "buffer-opt", NULL);
	pic.b = out_buf;
	pic.w = s_info->i_width;
	pic.h = s_info->i_height;
	pic.s = s_info->i_width;
	n_crop = 1;
	crops[0].x = 0;
	crops[0].y = 0;
	crops[0].w = pic.w;
	crops[0].h = pic.h;

	/* Get frame number */
	frames = get_frame_total_avis(avis_hnd);
	if (count_frames + init_frame > frames)
	{
		count_frames = frames - init_frame;
	}
	last_frame = count_frames + init_frame;

	/* No frames mean nothing to do */
	if (count_frames < 1)
	{
		fprintf(stderr, "No frames found.\n");
		return 0;
	}

	/* Set progress step */
	if (count_frames < 1000)
	{
		if (count_frames > 200)
			progress_step = 50;
		else if (count_frames > 50)
			progress_step = 10;
		else
			progress_step = 1;
	}

	/* Open SUP writer, if applicable */
	if (sup_output)
		sw = new_sup_writer(sup_output_fn, pic.w, pic.h, fps_num, fps_den);

	/* Process frames */
	for (i = init_frame; i < last_frame; i++)
	{
		if (read_frame_avis(in_img, avis_hnd, i))
		{
			fprintf(stderr, "Error reading frame.\n");
			return 1;
		}
		checked_empty = 0;

		/* Progress indicator */
		if (i % (count_frames / progress_step) == 0)
		{
			fprintf(stderr, "\rProgress: %d/%d - Lines: %d", i - init_frame, count_frames, num_of_events);
		}

		/* If we are outside any lines, check for empty frames first */
		if (!have_line)
		{
			if (is_empty(s_info, in_img))
				continue;
			else
				checked_empty = 1;
		}

		/* Check for duplicate, unless first frame */
		if ((i != init_frame) && have_line && is_identical(s_info, in_img, old_img))
			continue;
		/* Mark frames that were not used as new image in comparison to have transparent pixels zeroed */
		else if (!(i && have_line))
			must_zero = 1;

		/* Not a dup, write end-of-line, if we had a line before */
		if (have_line)
		{
			if (sup_output)
			{
				assert(pal != NULL);
				write_sup_wrapper(sw, (uint8_t *)out_buf, n_crop, crops, pal, start_frame + to, i + to, split_at, min_split, stricter, mark_forced);
				if (!xml_output)
					free(pal);
				pal = NULL;
			}
			if (xml_output)
				add_event_xml(events, split_at, min_split, start_frame + to, i + to, n_crop, crops, mark_forced);
			end_frame = i;
			have_line = 0;
		}

		/* Check for empty frame, if we didn't before */
		if (!checked_empty && is_empty(s_info, in_img))
			continue;

		/* Zero transparent pixels, if needed */
		if (must_zero)
			zero_transparent(s_info, in_img);
		must_zero = 0;

		/* Not an empty frame, start line */
		have_line = 1;
		start_frame = i;
		swap_rb(s_info, in_img, out_buf);
		if (buffer_opt)
			n_crop = auto_split(pic, crops, ugly, even_y);
		else if (autocrop)
		{
			crops[0].x = 0;
			crops[0].y = 0;
			crops[0].w = pic.w;
			crops[0].h = pic.h;
			auto_crop(pic, crops);
		}
		if ((buffer_opt || autocrop) && even_y)
			enforce_even_y(crops, n_crop);
		if ((pal_png || sup_output) && pal == NULL)
			pal = palletize(out_buf, s_info->i_width, s_info->i_height);
		if (xml_output)
			for (j = 0; j < n_crop; j++)
				write_png(png_dir, start_frame, (uint8_t *)out_buf, s_info->i_width, s_info->i_height, j, pal, crops[j]);
		if (pal_png && xml_output && !sup_output)
		{
			free(pal);
			pal = NULL;
		}
		num_of_events++;
		if (first_frame == -1)
			first_frame = i;

		/* Save image for next comparison. */
		tmp = in_img;
		in_img = old_img;
		old_img = tmp;
	}

	fprintf(stderr, "\rProgress: %d/%d - Lines: %d - Done\n", i - init_frame, count_frames, num_of_events);

	/* Add last event, if available */
	if (have_line)
	{
		if (sup_output)
		{
			assert(pal != NULL);
			write_sup_wrapper(sw, (uint8_t *)out_buf, n_crop, crops, pal, start_frame + to, i - 1 + to, split_at, min_split, stricter, mark_forced);
			if (!xml_output)
				free(pal);
			pal = NULL;
		}
		if (xml_output)
		{
			add_event_xml(events, split_at, min_split, start_frame + to, i - 1 + to, n_crop, crops, mark_forced);
			free(pal);
			pal = NULL;
		}
		auto_cut = 1;
		end_frame = i - 1;
	}

	if (sup_output)
	{
		close_sup_writer(sw);
	}

	if (xml_output)
	{
		/* Check if we actually have any events */
		if (first_frame == -1)
		{
			if (!allow_empty)
			{
				fprintf(stderr, "No events detected. Cowardly refusing to write XML file.\n");
				return 0;
			}
			else
			{
				first_frame = 0;
				end_frame = 0;
			}
		}

		/* Initialize timecode buffers */
		intc_buf = calloc(12, 1);
		outtc_buf = calloc(12, 1);

		/* Creating output file */
		if ((fh = fopen(xml_output_fn, "w")) == 0)
		{
			perror("Error opening output XML file");
			return 1;
		}

		/* Write XML header */
		mk_timecode(first_frame + to, fps, intc_buf);
		mk_timecode(end_frame + to + auto_cut, fps, outtc_buf);
		fprintf(fh, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<BDN Version=\"0.93\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
			"xsi:noNamespaceSchemaLocation=\"BD-03-006-0093b BDN File Format.xsd\">\n"
			"<Description>\n"
			"<Name Title=\"%s\" Content=\"\"/>\n"
			"<Language Code=\"%s\"/>\n"
			"<Format VideoFormat=\"%s\" FrameRate=\"%s\" DropFrame=\"%s\"/>\n"
			"<Events LastEventOutTC=\"%s\" FirstEventInTC=\"%s\"\n", track_name, language, video_format, frame_rate, drop_frame, outtc_buf, intc_buf);

		mk_timecode(0, fps, intc_buf);
		mk_timecode(frames + to, fps, outtc_buf);
		fprintf(fh, "ContentInTC=\"%s\" ContentOutTC=\"%s\" NumberofEvents=\"%d\" Type=\"Graphic\"/>\n"
			"</Description>\n"
			"<Events>\n", intc_buf, outtc_buf, num_of_events);

		/* Write XML events */
		if (!event_list_empty(events))
		{
			event = event_list_first(events);
			do
			{
				mk_timecode(event->start_frame, fps, intc_buf);
				mk_timecode(event->end_frame, fps, outtc_buf);

				if (auto_cut && event->end_frame == frames - 1)
				{
					mk_timecode(event->end_frame + 1, fps, outtc_buf);
				}

				fprintf(fh, "<Event Forced=\"%s\" InTC=\"%s\" OutTC=\"%s\">\n", (event->forced ? "True" : "False"), intc_buf, outtc_buf);
				for (i = 0; i < event->graphics; i++)
				{
					fprintf(fh, "<Graphic Width=\"%d\" Height=\"%d\" X=\"%d\" Y=\"%d\">%08d_%d.png</Graphic>\n", event->c[i].w, event->c[i].h, xo + event->c[i].x, yo + event->c[i].y, event->image_number - to, i);
				}
				fprintf(fh, "</Event>\n");
				event = event_list_next(events);
			}
			while (event != NULL);
		}

		/* Write XML footer */
		fprintf(fh, "</Events>\n</BDN>\n");

		/* Close XML file */
		fclose(fh);
	}

	/* Cleanup */
	close_file_avis(avis_hnd);

	/* Give runtime */
	if (0)
		fprintf(stderr, "Time elapsed: %lu\n", time(NULL) - bench_start);

	return 0;
}

