/*----------------------------------------------------------------------------
 * avs2bdnxml - Generates BluRay subtitle stuff from RGBA AviSynth scripts
 * Copyright (C) 2008-2010 Arne Bochem <avs2bdnxml at ps-auxw de>
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

#ifndef SUP_H
#define SUP_H

#include "abstract_lists.h"

typedef struct ass_sub_info_s
{
	int start;
	int end;
    int forced;
} ass_sub_info_t;


DECLARE_LIST(asi, ass_sub_info_t)

typedef struct ass_reader_s
{
	FILE *fh;
	asi_list_t *asil;
} ass_reader_t;


void parse_ass(char *filename);

#endif

