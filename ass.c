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
 *----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ass.h"
#include "abstract_lists.h"

#ifndef DEBUG
#define DEBUG 0
#endif

void parse_ass(char *filename)
{
    char l[BUFSIZ];
    char p[128];
    int start[4], end[4];
    FILE *fh;

    if ((fh = fopen(filename, "r")) == NULL)
	{
		perror("Error opening input ASS file");
		exit(1);
	}

    int i = 0;
    while (fgets(l, BUFSIZ - 1, fh) != NULL) {
        i++;
        if (l[0] == 0 || l[0] == '\n' || l[0] == '\r')
            continue;
        size_t x = sscanf(l, "Dialogue: %*d,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,%s[127]", &start[0], &start[1], &start[2], &start[3], &end[0], &end[1], &end[2], &end[3], p);
        if (x == 9) {
            char *subchar_start = strstr(p, ",");
            if (subchar_start == NULL) {
                printf("Error while parsing ASS in line %d - no ',' found after end timestamp.", i);
                exit(1);
            }
            subchar_start++;
            if (subchar_start[0] == '!') {
                printf("%d forced!\n", i);
            }
        }
    }

    fclose(fh);
}
