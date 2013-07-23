/*----------------------------------------------------------------------------
 * pgsparse - Parses BluRay PGS/SUP files
 * Copyright (C) 2010 Arne Bochem <avs2bdnxml at ps-auxw de>
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
 *----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef BE_ARCH
#define SWAP32(x) (x)
#define SWAP16(x) (x)
#endif
#ifdef LE_ARCH
#define SWAP32(x) ((int32_t)(((x & 0xff000000) >> 24) | ((x & 0xff0000) >> 8) | ((x & 0xff00) << 8) | ((x & 0xff) << 24)))
#define SWAP16(x) ((int16_t)(((x & 0xff) << 8) | ((x & 0xff00) >> 8)))
#endif
#ifndef LE_ARCH
#ifndef BE_ARCH
#error "Please specify endian-ness with -DLE_ARCH or -DBE_ARCH."
#endif
#endif

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef struct parser_state_s
{
	FILE *fh;
	int total_object_sizes;
	int images;
	int palettes;
	int diff_palettes;
	char last_pal[256 * 5];
} parser_state_t;

void clear_palette (parser_state_t *ps)
{
	memset(ps->last_pal, 0, 256*5);
}

void clear_stats (parser_state_t *ps)
{
	ps->total_object_sizes = 0;
	ps->images = 0;
	ps->palettes = 0;
	ps->diff_palettes = 0;
}

parser_state_t *new_sup_parser (char *filename)
{
	parser_state_t *ps = malloc(sizeof(parser_state_t));

	if ((ps->fh = fopen(filename, "rb")) == NULL)
	{
		fprintf(stderr, "Couldn't open SUP file (%s): ", filename);
		perror(NULL);
		abort();
	}

	clear_stats(ps);
	clear_palette(ps);

	return ps;
}

void die (FILE *fh, int offset, char *error)
{
	fprintf(stderr, "Error at %08x: %s\n", (int)ftell(fh) - offset, error);
	exit(1);
}

void safe_read (void *p, int size, FILE *fh, char *desc)
{
	char *error;
	int r;

	if ((r = fread(p, 1, size, fh)) != size)
	{
		error = malloc(128);
		error[127] = 0;
		snprintf(error, 127, "Could not read all %uB of %s.", size, desc);
		die(fh, r, error);
	}
}

void safe_seek (FILE *fh, int pos, char *desc)
{
	char *error;

	if (fseek(fh, pos, SEEK_CUR))
	{
		error = malloc(128);
		error[127] = 0;
		snprintf(error, 127, "Unexpected seek failure during %s.", desc);
		perror(error);
		abort();
	}
}

typedef struct fps_id_s
{
	int num;
	int den;
	int id;
} fps_id_t;

int get_fps (int id, int *fps_num, int *fps_den)
{
	fps_id_t ids[] = {{24000, 1001, 16}, {24, 1, 32}, {25, 1, 48}, {30000, 1001, 64}, {50, 1, 96}, {60000, 1001, 112}, {0, 0, 0}};
	int i = 0;

	while (ids[i].id)
	{
		if (ids[i].id == id)
		{
			*fps_num = ids[i].num;
			*fps_den = ids[i].den;
			return 1;
		}
		i++;
	}

	return 0;
}

typedef struct sup_header_s
{
	uint8_t m1;          /* 'P' */
	uint8_t m2;          /* 'G' */
	uint32_t start_time;
	uint32_t dts;
	uint8_t packet_type; /* 0x16 = pcs_start/end, 0x17 = wds, 0x14 = palette, 0x15 = ods_first, 0x80 = null */
	uint16_t packet_len;
} __attribute ((packed)) sup_header_t;

void conv_sup_header (sup_header_t *h)
{
	h->start_time = SWAP32(h->start_time);
	h->dts = SWAP32(h->dts);
	h->packet_len = SWAP16(h->packet_len);
}

void print_ts (uint32_t ts)
{
	printf("%.7fs (%u/90000s)", ((double)ts) / 90000, ts);
}

typedef struct sup_palette_s
{
	uint16_t palette; /* FIXME */
} __attribute ((packed)) sup_palette_t;

void conv_sup_palette (sup_palette_t *p)
{
	p->palette = SWAP16(p->palette);
}

void read_palette (parser_state_t *ps, sup_header_t h)
{
	sup_palette_t p;
	char *palette = malloc(5*256);
	int i, p_size;

	if (h.packet_len < 2+5)
		die(ps->fh, sizeof(h), "Undersized palette packet.");
	if (h.packet_len > 2+5*256)
		die(ps->fh, sizeof(h), "Oversized palette packet.");

	safe_read(&p, sizeof(p), ps->fh, "palette marker");
	conv_sup_palette(&p);

	printf("Palette\n");
	printf("\tpalette = %u\n", p.palette);
	(ps->palettes)++;

	p_size = h.packet_len - 2;
	safe_read(palette, p_size, ps->fh, "palette");
	for (i = 0; i < p_size; i++)
		if (palette[i] != ps->last_pal[i])
		{
			(ps->diff_palettes)++;
			clear_palette(ps);
			memcpy(ps->last_pal, palette, p_size);
			break;
		}
	free(palette);
}

typedef struct sup_ods_next_s
{
	uint16_t picture;
	uint8_t m; /* 0 */
	uint8_t last; /* 0 if not, if 64 yes */
} __attribute ((packed)) sup_ods_next_t;

void conv_sup_ods_next (sup_ods_next_t *odsn)
{
	odsn->picture = SWAP16(odsn->picture);
}

void read_odsn (parser_state_t *ps, sup_header_t h)
{
	sup_ods_next_t odsn;

	if (h.packet_len < sizeof(odsn))
		die(ps->fh, sizeof(h), "Undersized ODSN packet.");

	safe_read(&odsn, sizeof(odsn), ps->fh, "ODSN structure");
	conv_sup_ods_next(&odsn);
	if (odsn.m != 0 || (odsn.last != 0 && odsn.last != 64))
		die(ps->fh, sizeof(odsn), "Invalid ODSN magic.");
	printf("ODS next\n");
	printf("\tpicture = %u\n", odsn.picture);
	printf("\tlast    = %s (%u)\n", odsn.last ? "yes" : "no", odsn.last);

	safe_seek(ps->fh, h.packet_len - sizeof(odsn), "ODSN data");
}

typedef struct sup_ods_first_s
{
	uint16_t picture;
	uint8_t palette; /* FIXME */
	uint32_t magic_len; /* (single_packet ? 0xc0000000 : 0x80000000) | (length + 4) */
	uint16_t width;
	uint16_t height;
} __attribute ((packed)) sup_ods_first_t;

void conv_sup_ods_first (sup_ods_first_t *odsf)
{
	odsf->picture = SWAP16(odsf->picture);
	odsf->magic_len = SWAP32(odsf->magic_len);
	odsf->width = SWAP16(odsf->width);
	odsf->height = SWAP16(odsf->height);
}

void read_odsf (parser_state_t *ps, sup_header_t h)
{
	sup_ods_first_t odsf;

	if (h.packet_len < sizeof(odsf))
		die(ps->fh, sizeof(h), "Undersized ODSF packet.");

	safe_read(&odsf, sizeof(odsf), ps->fh, "ODSF structure");
	conv_sup_ods_first(&odsf);

	if ((odsf.magic_len & 0x80000000) && (odsf.magic_len & 0x40000000))
	{
		printf("ODS first\n\todsf type = single\n");
	}
	else if (odsf.magic_len & 0x80000000)
	{
		printf("ODS first\n\todsf type = multi\n");
	}
	else
	{
		fseek(ps->fh, -sizeof(odsf), SEEK_CUR);
		read_odsn(ps, h);
		return;
	}

	ps->total_object_sizes += odsf.width * odsf.height;
	(ps->images)++;
	printf("\tpicture   = %u\n", odsf.picture);
	printf("\tpalette   = %u\n", odsf.palette);
	printf("\tlength    = %u (incl. + 4)\n", odsf.magic_len & 0x3fffffff);
	printf("\twidth     = %u\n", odsf.width);
	printf("\theight    = %u\n", odsf.height);

	safe_seek(ps->fh, h.packet_len - sizeof(odsf), "OSDF data");
}

typedef struct sup_wds_s
{
	uint8_t objects; /* 1 or 2 */
} __attribute ((packed)) sup_wds_t;

void conv_sup_wds (sup_wds_t *wds)
{
	/* Do nothing. */
}

typedef struct sup_wds_obj_s
{
	uint8_t object; /* 0 or 1 */
	uint16_t x_off;
	uint16_t y_off;
	uint16_t width;
	uint16_t height;
} __attribute ((packed)) sup_wds_obj_t;

void conv_sup_wds_obj (sup_wds_obj_t *wdso)
{
	wdso->x_off = SWAP16(wdso->x_off);
	wdso->y_off = SWAP16(wdso->y_off);
	wdso->width = SWAP16(wdso->width);
	wdso->height = SWAP16(wdso->height);
}

void read_wds (parser_state_t *ps, sup_header_t h)
{
	sup_wds_t wds;
	sup_wds_obj_t wdso;
	int i;

	if (h.packet_len != sizeof(wds) + sizeof(wdso) && h.packet_len != sizeof(wds) + 2 * sizeof(wdso))
		die(ps->fh, sizeof(h), "Bad size for WDS packet.");

	safe_read(&wds, sizeof(wds), ps->fh, "WDS structure");
	conv_sup_wds(&wds);
	if (wds.objects != 1 && wds.objects != 2)
		die(ps->fh, sizeof(wds), "Invalid number of WDS objects.");

	printf("WDS\n");
	printf("\tobjects = %u\n", wds.objects);

	for (i = 0; i < wds.objects; i++)
	{
		safe_read(&wdso, sizeof(wdso), ps->fh, "WDS object structure");
		conv_sup_wds_obj(&wdso);

		if (wdso.object != 0 && wdso.object != 1)
			die(ps->fh, sizeof(wdso), "Invalid object id in WDS object.");

		printf("\tObject %u\n", wdso.object + 1);
		printf("\t\tx offset = %u\n", wdso.x_off);
		printf("\t\ty offset = %u\n", wdso.y_off);
		printf("\t\twidth    = %u\n", wdso.width);
		printf("\t\theight   = %u\n", wdso.height);
	}
}

typedef struct sup_pcs_start_s
{
	uint16_t width;
	uint16_t height; /* height - 2 * Core.getCropOfsY */
	uint8_t fps_id; /* getFpsId() */
	uint16_t comp_num;
	uint8_t follower;  /* 0x80 if first or single, 0x40 if follows directly (end = start) or the frame after */
	uint16_t m; /* 0 */
	uint8_t objects; /* 1 */
} __attribute ((packed)) sup_pcs_start_t;

void conv_sup_pcs_start (sup_pcs_start_t *pcss)
{
	pcss->width = SWAP16(pcss->width);
	pcss->height = SWAP16(pcss->height);
	pcss->comp_num = SWAP16(pcss->comp_num);
	pcss->m = SWAP16(pcss->m);
}

typedef struct sup_pcs_start_obj_s
{
	uint16_t picture;
	uint8_t window;
	uint8_t forced; /* forced ? 64 : 0 */
	uint16_t x_off;
	uint16_t y_off;
} __attribute ((packed)) sup_pcs_start_obj_t;

void conv_sup_pcs_start_obj (sup_pcs_start_obj_t *pcsso)
{
	pcsso->picture = SWAP16(pcsso->picture);
	pcsso->x_off = SWAP16(pcsso->x_off);
	pcsso->y_off = SWAP16(pcsso->y_off);
}

void read_pcs_start (parser_state_t *ps, sup_header_t h)
{
	sup_pcs_start_t pcss;
	sup_pcs_start_obj_t pcsso;
	int fps_num, fps_den, i;

	safe_read(&pcss, sizeof(pcss), ps->fh, "PCSS structure");
	conv_sup_pcs_start(&pcss);

	if (pcss.m != 0)
		die(ps->fh, sizeof(pcss), "Invalid PCSS magic.");

	printf("PCS start\n");

	printf("\tframe width  = %u\n", pcss.width);
	printf("\tframe height = %u\n", pcss.height);
	if (!get_fps(pcss.fps_id, &fps_num, &fps_den))
		die(ps->fh, sizeof(pcss), "Invalid FPS ID in PCSS.");
	printf("\tfps id       = %u (%u/%u)\n", pcss.fps_id, fps_num, fps_den);
	printf("\tcomposition  = %u\n", pcss.comp_num);
	printf("\tfollower     = 0x%02X (%s)\n", pcss.follower, pcss.follower == 0x80 ? "no" : "within 2f");
	printf("\tobjects      = %u\n", pcss.objects);

	if (pcss.objects > 2)
		die(ps->fh, sizeof(pcss), "Invalid number of objects (must be 1 or 2).");

	for (i = 0; i < pcss.objects; i++)
	{
		safe_read(&pcsso, sizeof(pcsso), ps->fh, "PCSSO structure");
		conv_sup_pcs_start_obj(&pcsso);

		if (pcsso.window != 0 && pcsso.window != 1)
			die(ps->fh, sizeof(pcsso), "Invalid window id in PCSS object.");
		printf("\tObject %u\n", i + 1);
		if (pcsso.forced && (pcsso.forced != 64))
			die(ps->fh, sizeof(pcsso), "Invalid forced flag in PCSS object.");
		printf("\t\tpicture  = %u\n", pcsso.picture);
		printf("\t\twindow   = %u\n", pcsso.window);
		printf("\t\tforced   = %u\n", pcsso.forced);
		printf("\t\tx offset = %u\n", pcsso.x_off);
		printf("\t\ty offset = %u\n", pcsso.y_off);
	}
}

typedef struct sup_pcs_end_s
{
	uint16_t width;
	uint16_t height;
	uint8_t fps_id;
	uint16_t comp_num;
	uint32_t m;
} __attribute ((packed)) sup_pcs_end_t;

void conv_sup_pcs_end (sup_pcs_end_t *pcse)
{
	pcse->width = SWAP16(pcse->width);
	pcse->height = SWAP16(pcse->height);
	pcse->comp_num = SWAP16(pcse->comp_num);
	pcse->m = SWAP32(pcse->m);
}

void read_pcs_end (parser_state_t *ps, sup_header_t h)
{
	sup_pcs_end_t pcse;
	int fps_num, fps_den;

	safe_read(&pcse, sizeof(pcse), ps->fh, "PCSE structure");
	conv_sup_pcs_end(&pcse);

	if (pcse.m != 0)
		die(ps->fh, sizeof(pcse), "Invalid PCSE magic.");

	printf("PCS end\n");

	printf("\tsubtitle width  = %u\n", pcse.width);
	printf("\tsubtitle height = %u\n", pcse.height);
	if (!get_fps(pcse.fps_id, &fps_num, &fps_den))
		die(ps->fh, sizeof(pcse), "Invalid FPS ID in PCSE.");
	printf("\tfps id          = %u (%u/%u)\n", pcse.fps_id, fps_num, fps_den);
	printf("\tcomposition     = %u\n", pcse.comp_num);
	printf("\tStats\n\t\ttot_ob_size   = %u\n\t\timages        = %u\n\t\tpalettes      = %u\n\t\tdiff_palettes = %u\n", ps->total_object_sizes, ps->images, ps->palettes, ps->diff_palettes);
	clear_stats(ps);
	clear_palette(ps);
}

void read_sup (parser_state_t *ps)
{
	sup_header_t h;

	safe_read(&h, sizeof(h), ps->fh, "PG header");
	conv_sup_header(&h);

	if (h.m1 != 80 || h.m2 != 71)
		die(ps->fh, sizeof(h), "Invalid PG header.");

	printf("Packet at 0x%08x:\nstart_time = ", (unsigned int)(ftell(ps->fh) - sizeof(h)));
	print_ts(h.start_time);
	printf("\ndts        = ");
	print_ts(h.dts);
	printf("\ntype       = 0x%02X\n", h.packet_type);
	printf("length     = %u\n", h.packet_len);
	switch (h.packet_type)
	{
		case 0x14:
			read_palette(ps, h);
			break;
		case 0x15:
			read_odsf(ps, h);
			break;
		case 0x16:
			switch (h.packet_len)
			{
				case 11:
					read_pcs_end(ps, h);
					break;
				case 19:
				case 27:
					read_pcs_start(ps, h);
					break;
				default:
					die(ps->fh, sizeof(h), "Invalid PCS size.");
			}
			break;
		case 0x17:
			read_wds(ps, h);
			break;
		case 0x80:
			if (h.packet_len == 0)
				printf("Marker\n");
			else
				die(ps->fh, sizeof(h), "Marker with payload.");
			break;
		default:
			die(ps->fh, sizeof(h), "Unknown packet type.");
	}

	printf("\n");
}

int main (int argc, char *argv[])
{
	parser_state_t *ps;
	size_t last;

	if (argc != 2)
	{
		printf("Usage: pgsparse SUPFILE\n");
		return 0;
	}

	ps = new_sup_parser(argv[1]);

	fseek(ps->fh, 0, SEEK_END);
	last = ftell(ps->fh);
	fseek(ps->fh, 0, SEEK_SET);

	while (!feof(ps->fh) && ftell(ps->fh) < last)
		read_sup(ps);
	
	fclose(ps->fh);

	return 0;
}

