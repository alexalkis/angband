/**
 * \file gen-room.c
 * \brief Dungeon room generation.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2013 Erik Osheim, Nick McConnell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 *
 * This file covers everything to do with generation of individual rooms in
 * the dungeon.  It consists of room generating helper functions plus the 
 * actual room builders (which are referred to in the room profiles in
 * generate.c).
 *
 * The room builders all take as arguments the chunk they are being generated
 * in, and the co-ordinates of the room centre in that chunk.  Each room
 * builder is also able to find space for itself in the chunk using the 
 * find_space() function; the chunk generating functions can ask it to do that
 * by passing too large centre co-ordinates.
 */

#include "angband.h"
#include "cave.h"
#include "math.h"
#include "game-event.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-spell.h"
#include "obj-tval.h"
#include "parser.h"
#include "trap.h"
#include "z-queue.h"
#include "z-type.h"

#ifdef USE_AMI
// math lib is needed only for sqrt, so we provide an integer sqrt in asm
//  and we don't need to link with math (-lm) for Amiga
int isqrt(int n);
#endif

/**
 * Chooses a room template of a particular kind at random.
 * \param typ template room type - currently unused
 * \return a pointer to the room template
 */
struct room_template *random_room_template(int typ)
{
	struct room_template *t = room_templates;
	struct room_template *r = NULL;
	int n = 1;
	do {
		if (t->typ == typ) {
			if (one_in_(n)) r = t;
			n++;
		}
		t = t->next;
	} while(t);
	return r;
}

/**
 * Chooses a vault of a particular kind at random.
 * \param depth the current depth, for vault boun checking
 * \param typ vault type
 * \return a pointer to the vault template
 */
struct vault *random_vault(int depth, const char *typ)
{
	struct vault *v = vaults;
	struct vault *r = NULL;
	int n = 1;
	do {
		if (streq(v->typ, typ) && (v->min_lev <= depth)
			&& (v->max_lev >= depth)) {
			if (one_in_(n)) r = v;
			n++;
		}
		v = v->next;
	} while(v);
	return r;
}


/**
 * Mark squares as being in a room, and optionally light them.
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param light whether or not to light the room
 */
static void generate_room(struct chunk *c, int y1, int x1, int y2, int x2, int light)
{
	int y, x;
	for (y = y1; y <= y2; y++)
		for (x = x1; x <= x2; x++) {
			sqinfo_on(c->squares[y][x].info, SQUARE_ROOM);
			if (light)
				sqinfo_on(c->squares[y][x].info, SQUARE_GLOW);
		}
}


/**
 * Mark a rectangle with a sqinfo flag
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param flag the SQUARE_* flag we are marking with
 */
void generate_mark(struct chunk *c, int y1, int x1, int y2, int x2, int flag)
{
	int y, x;

	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			sqinfo_on(c->squares[y][x].info, flag);
		}
	}
}


/**
 * Fill a rectangle with a feature.
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param feat the terrain feature
 * \param flag the SQUARE_* flag we are marking with
 */
void fill_rectangle(struct chunk *c, int y1, int x1, int y2, int x2, int feat,
					int flag)
{
	int y, x;
	for (y = y1; y <= y2; y++)
		for (x = x1; x <= x2; x++)
			square_set_feat(c, y, x, feat);
	if (flag) generate_mark(c, y1, x1, y2, x2, flag);
}


/**
 * Fill the edges of a rectangle with a feature.
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param feat the terrain feature
 * \param flag the SQUARE_* flag we are marking with
 */
void draw_rectangle(struct chunk *c, int y1, int x1, int y2, int x2, int feat,
					int flag)
{
	int y, x;

	for (y = y1; y <= y2; y++) {
		square_set_feat(c, y, x1, feat);
		square_set_feat(c, y, x2, feat);
	}
	if (flag) {
		generate_mark(c, y1, x1, y2, x1, flag);
		generate_mark(c, y1, x2, y2, x2, flag);
	}
	for (x = x1; x <= x2; x++) {
		square_set_feat(c, y1, x, feat);
		square_set_feat(c, y2, x, feat);
	}
	if (flag) {
		generate_mark(c, y1, x1, y1, x2, flag);
		generate_mark(c, y2, x1, y2, x2, flag);
	}
}


/**
 * Fill a horizontal range with the given feature/info.
 * \param c the current chunk
 * \param y
 * \param x1
 * \param x2 inclusive range boundaries
 * \param feat the terrain feature
 * \param flag the SQUARE_* flag we are marking with
 * \param light lit or not
 */
static void fill_xrange(struct chunk *c, int y, int x1, int x2, int feat, 
						int flag, bool light)
{
	int x;
	for (x = x1; x <= x2; x++) {
		square_set_feat(c, y, x, feat);
		sqinfo_on(c->squares[y][x].info, SQUARE_ROOM);
		if (flag) sqinfo_on(c->squares[y][x].info, flag);
		if (light)
			sqinfo_on(c->squares[y][x].info, SQUARE_GLOW);
	}
}


/**
 * Fill a vertical range with the given feature/info.
 * \param c the current chunk
 * \param x
 * \param y1
 * \param y2 inclusive range boundaries
 * \param feat the terrain feature
 * \param flag the SQUARE_* flag we are marking with
 * \param light lit or not
 */
static void fill_yrange(struct chunk *c, int x, int y1, int y2, int feat, 
						int flag, bool light)
{
	int y;
	for (y = y1; y <= y2; y++) {
		square_set_feat(c, y, x, feat);
		sqinfo_on(c->squares[y][x].info, SQUARE_ROOM);
		if (flag) sqinfo_on(c->squares[y][x].info, flag);
		if (light)
			sqinfo_on(c->squares[y][x].info, SQUARE_GLOW);
	}
}


/**
 * Fill a circle with the given feature/info.
 * \param c the current chunk
 * \param y0
 * \param x0 the circle centre
 * \param radius the circle radius
 * \param border the width of the circle border
 * \param feat the terrain feature
 * \param flag the SQUARE_* flag we are marking with
 * \param light lit or not
 */
static void fill_circle(struct chunk *c, int y0, int x0, int radius, int border,
						int feat, int flag, bool light)
{
	int i, last = 0;
	int r2 = radius * radius;
	for(i = 0; i <= radius; i++) {
#ifdef USE_AMI
        int k = isqrt(r2 - (i * i));
#else
		double j = sqrt(r2 - (i * i));
		int k = (int)(j + 0.5);
#endif

		int b = border;
		if (border && last > k) b++;
		
		fill_xrange(c, y0 - i, x0 - k - b, x0 + k + b, feat, flag, light);
		fill_xrange(c, y0 + i, x0 - k - b, x0 + k + b, feat, flag, light);
		fill_yrange(c, x0 - i, y0 - k - b, y0 + k + b, feat, flag, light);
		fill_yrange(c, x0 + i, y0 - k - b, y0 + k + b, feat, flag, light);
		last = k;
	}
}


/**
 * Fill the lines of a cross/plus with a feature.
 *
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param feat the terrain feature
 * \param flag the SQUARE_* flag we are marking with
 * When combined with draw_rectangle() this will generate a large rectangular 
 * room which is split into four sub-rooms.
 */
static void generate_plus(struct chunk *c, int y1, int x1, int y2, int x2, 
						  int feat, int flag)
{
	int y, x;

	/* Find the center */
	int y0 = (y1 + y2) / 2;
	int x0 = (x1 + x2) / 2;

	assert(c);

	for (y = y1; y <= y2; y++) square_set_feat(c, y, x0, feat);
	if (flag) generate_mark(c, y1, x0, y2, x0, flag);
	for (x = x1; x <= x2; x++) square_set_feat(c, y0, x, feat);
	if (flag) generate_mark(c, y0, x1, y0, x2, flag);
}


/**
 * Generate helper -- open all sides of a rectangle with a feature
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param feat the terrain feature
 */
static void generate_open(struct chunk *c, int y1, int x1, int y2, int x2, int feat)
{
	int y0, x0;

	/* Center */
	y0 = (y1 + y2) / 2;
	x0 = (x1 + x2) / 2;

	/* Open all sides */
	square_set_feat(c, y1, x0, feat);
	square_set_feat(c, y0, x1, feat);
	square_set_feat(c, y2, x0, feat);
	square_set_feat(c, y0, x2, feat);
}


/**
 * Generate helper -- open one side of a rectangle with a feature
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 inclusive room boundaries
 * \param feat the terrain feature
 */
static void generate_hole(struct chunk *c, int y1, int x1, int y2, int x2, int feat)
{
	/* Find the center */
	int y0 = (y1 + y2) / 2;
	int x0 = (x1 + x2) / 2;

	assert(c);

	/* Open random side */
	switch (randint0(4)) {
	case 0: square_set_feat(c, y1, x0, feat); break;
	case 1: square_set_feat(c, y0, x1, feat); break;
	case 2: square_set_feat(c, y2, x0, feat); break;
	case 3: square_set_feat(c, y0, x2, feat); break;
	}
}


/**
 * Place a square of granite with a flag
 * \param c the current chunk
 * \param y
 * \param x the square co-ordinates
 * \param flag the SQUARE_* flag we are marking with
 */
void set_marked_granite(struct chunk *c, int y, int x, int flag)
{
	square_set_feat(c, y, x, FEAT_GRANITE);
	if (flag) generate_mark(c, y, x, y, x, flag);
}

/**
 * Make a starburst room. -LM-
 *
 * \param c the current chunk
 * \param y1
 * \param x1
 * \param y2
 * \param x2 boundaries which will contain the starburst
 * \param light lit or not
 * \param feat the terrain feature to make the starburst of
 * \param special_ok allow wacky cloverleaf rooms
 * \return success
 *
 * Starburst rooms are made in three steps:
 * 1: Choose a room size-dependant number of arcs.  Large rooms need to 
 *    look less granular and alter their shape more often, so they need 
 *    more arcs.
 * 2: For each of the arcs, calculate the portion of the full circle it 
 *    includes, and its maximum effect range (how far in that direction 
 *    we can change features in).  This depends on room size, shape, and 
 *    the maximum effect range of the previous arc.
 * 3: Use the table "get_angle_to_grid" to supply angles to each grid in 
 *    the room.  If the distance to that grid is not greater than the 
 *    maximum effect range that applies at that angle, change the feature 
 *    if appropriate (this depends on feature type).
 *
 * Usage notes:
 * - This function uses a table that cannot handle distances larger than 
 *   20, so it calculates a distance conversion factor for larger rooms.
 * - This function is not good at handling rooms much longer along one axis 
 *   than the other, so it divides such rooms up, and calls itself to handle
 *   each section.  
 * - It is safe to call this function on areas that might contain vaults or 
 *   pits, because "icky" and occupied grids are left untouched.
 *
 * - Mixing these rooms (using normal floor) with rectangular ones on a 
 *   regular basis produces a somewhat chaotic looking dungeon.  However, 
 *   this code does works well for lakes, etc.
 *
 */
extern bool generate_starburst_room(struct chunk *c, int y1, int x1, int y2, 
									int x2, bool light, int feat, 
									bool special_ok)
{
	int y0, x0, y, x, ny, nx;
	int i, d;
	int dist, max_dist, dist_conv, dist_check;
	int height, width;
	int degree_first, center_of_arc, degree;

	/* Special variant room.  Discovered by accident. */
	bool make_cloverleaf = FALSE;

	/* Holds first degree of arc, maximum effect distance in arc. */
	int arc[45][2];

	/* Number (max 45) of arcs. */
	int arc_num;

	struct feature *f = &f_info[feat];

	/* Make certain the room does not cross the dungeon edge. */
	if ((!square_in_bounds(c, y1, x1)) || (!square_in_bounds(c, y2, x2)))
		return (FALSE);

	/* Robustness -- test sanity of input coordinates. */
	if ((y1 + 2 >= y2) || (x1 + 2 >= x2))
		return (FALSE);


	/* Get room height and width. */
	height = 1 + y2 - y1;
	width = 1 + x2 - x1;


	/* Handle long, narrow rooms by dividing them up. */
	if ((height > 5 * width / 2) || (width > 5 * height / 2)) {
		int tmp_ay, tmp_ax, tmp_by, tmp_bx;

		/* Get bottom-left borders of the first room. */
		tmp_ay = y2;
		tmp_ax = x2;
		if (height > width)
			tmp_ay = y1 + 2 * height / 3;
		else
			tmp_ax = x1 + 2 * width / 3;

		/* Make the first room. */
		(void) generate_starburst_room(c, y1, x1, tmp_ay, tmp_ax, light, feat,
									   FALSE);


		/* Get top_right borders of the second room. */
		tmp_by = y1;
		tmp_bx = x1;
		if (height > width)
			tmp_by = y1 + 1 * height / 3;
		else
			tmp_bx = x1 + 1 * width / 3;

		/* Make the second room. */
		(void) generate_starburst_room(c, tmp_by, tmp_bx, y2, x2, light, feat,
									   FALSE);


		/* 
		 * If floor, extend a "corridor" between room centers, to ensure 
		 * that the rooms are connected together.
		 */
		if (tf_has(f->flags, TF_FLOOR)) {
			for (y = (y1 + tmp_ay) / 2; y <= (tmp_by + y2) / 2; y++) {
				for (x = (x1 + tmp_ax) / 2; x <= (tmp_bx + x2) / 2; x++) {
					square_set_feat(c, y, x, feat);
				}
			}
		}

		/*
		 * Otherwise fill any gap between two starbursts.
		 */
		else {
			int tmp_cy1, tmp_cx1, tmp_cy2, tmp_cx2;

			if (height > width) {
				tmp_cy1 = y1 + (height - width) / 2;
				tmp_cx1 = x1;
				tmp_cy2 = tmp_cy1 - (height - width) / 2;
				tmp_cx2 = x2;
			} else {
				tmp_cy1 = y1;
				tmp_cx1 = x1 + (width - height) / 2;
				tmp_cy2 = y2;
				tmp_cx2 = tmp_cx1 + (width - height) / 2;

				tmp_cy1 = y1;
				tmp_cx1 = x1;
			}

			/* Make the third room. */
			(void) generate_starburst_room(c, tmp_cy1, tmp_cx1, tmp_cy2,
										   tmp_cx2, light, feat, FALSE);
		}

		/* Return. */
		return (TRUE);
	}


	/* Get a shrinkage ratio for large rooms, as table is limited. */
	if ((width > 44) || (height > 44)) {
		if (width > height)
			dist_conv = 10 * width / 44;
		else
			dist_conv = 10 * height / 44;
	} else
		dist_conv = 10;


	/* Make a cloverleaf room sometimes. */
	if ((special_ok) && (height > 10) && (randint0(20) == 0)) {
		arc_num = 12;
		make_cloverleaf = TRUE;
	}

	/* Usually, we make a normal starburst. */
	else {
		/* Ask for a reasonable number of arcs. */
		arc_num = 8 + (height * width / 80);
		arc_num = arc_num + 3 - randint0(7);
		if (arc_num < 8)
			arc_num = 8;
		if (arc_num > 45)
			arc_num = 45;
	}


	/* Get the center of the starburst. */
	y0 = y1 + height / 2;
	x0 = x1 + width / 2;

	/* Start out at zero degrees. */
	degree_first = 0;


	/* Determine the start degrees and expansion distance for each arc. */
	for (i = 0; i < arc_num; i++) {
		/* Get the first degree for this arc. */
		arc[i][0] = degree_first;

		/* Get a slightly randomized start degree for the next arc. */
		degree_first += (180 + randint0(arc_num)) / arc_num;
		if (degree_first < 180 * (i + 1) / arc_num)
			degree_first = 180 * (i + 1) / arc_num;
		if (degree_first > (180 + arc_num) * (i + 1) / arc_num)
			degree_first = (180 + arc_num) * (i + 1) / arc_num;


		/* Get the center of the arc. */
		center_of_arc = degree_first + arc[i][0];

		/* Calculate a reasonable distance to expand vertically. */
		if (((center_of_arc > 45) && (center_of_arc < 135))
			|| ((center_of_arc > 225) && (center_of_arc < 315))) {
			arc[i][1] = height / 4 + randint0((height + 3) / 4);
		}

		/* Calculate a reasonable distance to expand horizontally. */
		else if (((center_of_arc < 45) || (center_of_arc > 315))
				 || ((center_of_arc < 225) && (center_of_arc > 135))) {
			arc[i][1] = width / 4 + randint0((width + 3) / 4);
		}

		/* Handle arcs that count as neither vertical nor horizontal */
		else if (i != 0) {
			if (make_cloverleaf)
				arc[i][1] = 0;
			else
				arc[i][1] = arc[i - 1][1] + 3 - randint0(7);
		}


		/* Keep variability under control. */
		if ((!make_cloverleaf) && (i != 0) && (i != arc_num - 1)) {
			/* Water edges must be quite smooth. */
			if (tf_has(f->flags, TF_SMOOTH)) {
				if (arc[i][1] > arc[i - 1][1] + 2)
					arc[i][1] = arc[i - 1][1] + 2;

				if (arc[i][1] > arc[i - 1][1] - 2)
					arc[i][1] = arc[i - 1][1] - 2;
			} else {
				if (arc[i][1] > 3 * (arc[i - 1][1] + 1) / 2)
					arc[i][1] = 3 * (arc[i - 1][1] + 1) / 2;

				if (arc[i][1] < 2 * (arc[i - 1][1] - 1) / 3)
					arc[i][1] = 2 * (arc[i - 1][1] - 1) / 3;
			}
		}

		/* Neaten up final arc of circle by comparing it to the first. */
		if ((i == arc_num - 1) && (ABS(arc[i][1] - arc[0][1]) > 3)) {
			if (arc[i][1] > arc[0][1])
				arc[i][1] -= randint0(arc[i][1] - arc[0][1]);
			else if (arc[i][1] < arc[0][1])
				arc[i][1] += randint0(arc[0][1] - arc[i][1]);
		}
	}


	/* Precalculate check distance. */
	dist_check = 21 * dist_conv / 10;

	/* Change grids between (and not including) the edges. */
	for (y = y1 + 1; y < y2; y++) {
		for (x = x1 + 1; x < x2; x++) {
			/* Do not touch vault grids. */
			if (square_isvault(c, y, x))
				continue;

			/* Do not touch occupied grids. */
			if (square_monster(c, y, x))
				continue;
			if (square_object(c, y, x))
				continue;

			/* Get distance to grid. */
			dist = distance(y0, x0, y, x);

			/* Reject grid if outside check distance. */
			if (dist >= dist_check) 
				continue;

			/* Convert and reorient grid for table access. */
			ny = 20 + 10 * (y - y0) / dist_conv;
			nx = 20 + 10 * (x - x0) / dist_conv;

			/* Illegal table access is bad. */
			if ((ny < 0) || (ny > 40) || (nx < 0) || (nx > 40))
				continue;

			/* Get angle to current grid. */
			degree = get_angle_to_grid[ny][nx];

			/* Scan arcs to find the one that applies here. */
			for (i = arc_num - 1; i >= 0; i--) {
				if (arc[i][0] <= degree) {
					max_dist = arc[i][1];

					/* Must be within effect range. */
					if (max_dist >= dist) {
						/* If new feature is not passable, or floor, always 
						 * place it. */
						if ((tf_has(f->flags, TF_FLOOR))
							|| (!tf_has(f->flags, TF_PASSABLE))) {
							square_set_feat(c, y, x, feat);

							if (tf_has(f->flags, TF_FLOOR))
								sqinfo_on(c->squares[y][x].info, SQUARE_ROOM);
							else
								sqinfo_off(c->squares[y][x].info, SQUARE_ROOM);

							if (light)
								sqinfo_on(c->squares[y][x].info, SQUARE_GLOW);
							else
								sqinfo_off(c->squares[y][x].info, SQUARE_GLOW);
						}

						/* If new feature is non-floor passable terrain,
						 * place it only over floor. */
						else {
							/* Replace old feature entirely in some cases. */
							if (tf_has(f->flags, TF_SMOOTH)) {
								if (tf_has(f_info[c->squares[y][x].feat].flags, 
										   TF_FLOOR))
									square_set_feat(c, y, x, feat);
							}
							/* Make denser in the middle. */
							else {
								if ((tf_has(f_info[c->squares[y][x].feat].flags,
											TF_FLOOR))
									&& (randint1(max_dist + 5) >= dist + 5))
									square_set_feat(c, y, x, feat);
							}

							/* Light grid. */
							if (light)
								sqinfo_on(c->squares[y][x].info, SQUARE_GLOW);
						}
					}

					/* Arc found.  End search */
					break;
				}
			}
		}
	}

	/*
	 * If we placed floors or dungeon granite, all dungeon granite next
	 * to floors needs to become outer wall.
	 */
	if ((tf_has(f->flags, TF_FLOOR)) || (feat == FEAT_GRANITE)) {
		for (y = y1 + 1; y < y2; y++) {
			for (x = x1 + 1; x < x2; x++) {
				/* Floor grids only */
				if (tf_has(f_info[c->squares[y][x].feat].flags, TF_FLOOR)) {
					/* Look in all directions. */
					for (d = 0; d < 8; d++) {
						/* Extract adjacent location */
						int yy = y + ddy_ddd[d];
						int xx = x + ddx_ddd[d];

						/* Join to room */
						sqinfo_on(c->squares[yy][xx].info, SQUARE_ROOM);

						/* Illuminate if requested. */
						if (light)
							sqinfo_on(c->squares[yy][xx].info, SQUARE_GLOW);

						/* Look for dungeon granite. */
						if (c->squares[yy][xx].feat == FEAT_GRANITE) {
							/* Mark as outer wall. */
							set_marked_granite(c, yy, xx, SQUARE_WALL_OUTER);
						}
					}
				}
			}
		}
	}

	/* Success */
	return (TRUE);
}



/**
 * Find a good spot for the next room.
 *
 * \param y
 * \param x centre of the room
 * \param height
 * \param width dimensions of the room
 * \return success
 *
 * Find and allocate a free space in the dungeon large enough to hold
 * the room calling this function.
 *
 * We allocate space in blocks.
 *
 * Be careful to include the edges of the room in height and width!
 *
 * Return TRUE and values for the center of the room if all went well.
 * Otherwise, return FALSE.
 */
static bool find_space(int *y, int *x, int height, int width)
{
	int i;
	int by, bx, by1, bx1, by2, bx2;

	bool filled;

	/* Find out how many blocks we need. */
	int blocks_high = 1 + ((height - 1) / dun->block_hgt);
	int blocks_wide = 1 + ((width - 1) / dun->block_wid);

	/* We'll allow twenty-five guesses. */
	for (i = 0; i < 25; i++) {
		filled = FALSE;

		/* Pick a top left block at random */
		by1 = randint0(dun->row_blocks);
		bx1 = randint0(dun->col_blocks);

		/* Extract bottom right corner block */
		by2 = by1 + blocks_high - 1;
		bx2 = bx1 + blocks_wide - 1;

		/* Never run off the screen */
		if (by1 < 0 || by2 >= dun->row_blocks) continue;
		if (bx1 < 0 || bx2 >= dun->col_blocks) continue;

		/* Verify open space */
		for (by = by1; by <= by2; by++) {
			for (bx = bx1; bx <= bx2; bx++) {
				if (dun->room_map[by][bx])
					filled = TRUE;
			}
		}

		/* If space filled, try again. */
		if (filled)	continue;

		/* Get the location of the room */
		*y = ((by1 + by2 + 1) * dun->block_hgt) / 2;
		*x = ((bx1 + bx2 + 1) * dun->block_wid) / 2;

		/* Save the room location */
		if (dun->cent_n < z_info->level_room_max) {
			dun->cent[dun->cent_n].y = *y;
			dun->cent[dun->cent_n].x = *x;
			dun->cent_n++;
		}

		/* Reserve some blocks */
		for (by = by1; by <= by2; by++) {
			for (bx = bx1; bx <= bx2; bx++) {
				dun->room_map[by][bx] = TRUE;
			}
		}

		/* Success. */
		return (TRUE);
	}

	/* Failure. */
	return (FALSE);
}

/**
 * Build a circular room (interior radius 4-7).
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_circular(struct chunk *c, int y0, int x0)
{
	/* Pick a room size */
	int radius = 2 + randint1(2) + randint1(3);

	/* Occasional light */
	bool light = c->depth <= randint1(25) ? TRUE : FALSE;

	/* Find and reserve lots of space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, 2 * radius + 10, 2 * radius + 10))
			return (FALSE);
	}

	/* Generate outer walls and inner floors */
	fill_circle(c, y0, x0, radius + 1, 1, FEAT_GRANITE, SQUARE_WALL_OUTER,
				light);
	fill_circle(c, y0, x0, radius, 0, FEAT_FLOOR, SQUARE_NONE, light);

	/* Especially large circular rooms will have a middle chamber */
	if (radius - 4 > 0 && randint0(4) < radius - 4) {
		/* choose a random direction */
		int cd, rd;
		rand_dir(&rd, &cd);

		/* draw a room with a secret door on a random side */
		draw_rectangle(c, y0 - 2, x0 - 2, y0 + 2, x0 + 2,
					   FEAT_GRANITE, SQUARE_WALL_INNER);
		square_set_feat(c, y0 + cd * 2, x0 + rd * 2, FEAT_SECRET);

		/* Place a treasure in the vault */
		vault_objects(c, y0, x0, c->depth, randint0(2));

		/* create some monsterss */
		vault_monsters(c, y0, x0, c->depth + 1, randint0(3));
	}

	return TRUE;
}


/**
 * Builds a normal rectangular room.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_simple(struct chunk *c, int y0, int x0)
{
	int y, x, y1, x1, y2, x2;
	int light = FALSE;

	/* Pick a room size */
	int height = 1 + randint1(4) + randint1(3);
	int width = 1 + randint1(11) + randint1(11);

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height + 2, width + 2))
			return (FALSE);
	}

	/* Pick a room size */
	y1 = y0 - height / 2;
	x1 = x0 - width / 2;
	y2 = y1 + height - 1;
	x2 = x1 + width - 1;

	/* Occasional light */
	if (c->depth <= randint1(25)) light = TRUE;

	/* Generate new room */
	generate_room(c, y1-1, x1-1, y2+1, x2+1, light);

	/* Generate outer walls and inner floors */
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_OUTER);
	fill_rectangle(c, y1, x1, y2, x2, FEAT_FLOOR, SQUARE_NONE);

	if (one_in_(20)) {
		/* Sometimes make a pillar room */
		for (y = y1; y <= y2; y += 2)
			for (x = x1; x <= x2; x += 2)
				set_marked_granite(c, y, x, SQUARE_WALL_INNER);

	} else if (one_in_(50)) {
		/* Sometimes make a ragged-edge room */
		for (y = y1 + 2; y <= y2 - 2; y += 2) {
			set_marked_granite(c, y, x1, SQUARE_WALL_INNER);
			set_marked_granite(c, y, x2, SQUARE_WALL_INNER);
		}

		for (x = x1 + 2; x <= x2 - 2; x += 2) {
			set_marked_granite(c, y1, x, SQUARE_WALL_INNER);
			set_marked_granite(c, y2, x, SQUARE_WALL_INNER);
		}
	}
	return TRUE;
}


/**
 * Builds an overlapping rectangular room.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_overlap(struct chunk *c, int y0, int x0)
{
	int y1a, x1a, y2a, x2a;
	int y1b, x1b, y2b, x2b;
	int height, width;

	int light = FALSE;

	/* Occasional light */
	if (c->depth <= randint1(25)) light = TRUE;

	/* Determine extents of room (a) */
	y1a = randint1(4);
	x1a = randint1(11);
	y2a = randint1(3);
	x2a = randint1(10);

	/* Determine extents of room (b) */
	y1b = randint1(3);
	x1b = randint1(10);
	y2b = randint1(4);
	x2b = randint1(11);

	/* Calculate height and width */
	height = 2 * MAX(MAX(y1a, y2a), MAX(y1b, y2b)) + 1;
	width = 2 * MAX(MAX(x1a, x2a), MAX(x1b, x2b)) + 1;

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height + 2, width + 2))
			return (FALSE);
	}

	/* locate room (a) */
	y1a = y0 - y1a;
	x1a = x0 - x1a;
	y2a = y0 + y2a;
	x2a = x0 + x2a;

	/* locate room (b) */
	y1b = y0 - y1b;
	x1b = x0 - x1b;
	y2b = y0 + y2b;
	x2b = x0 + x2b;

	/* Generate new room (a) */
	generate_room(c, y1a-1, x1a-1, y2a+1, x2a+1, light);

	/* Generate new room (b) */
	generate_room(c, y1b-1, x1b-1, y2b+1, x2b+1, light);

	/* Generate outer walls (a) */
	draw_rectangle(c, y1a-1, x1a-1, y2a+1, x2a+1, 
				   FEAT_GRANITE, SQUARE_WALL_OUTER);

	/* Generate outer walls (b) */
	draw_rectangle(c, y1b-1, x1b-1, y2b+1, x2b+1, 
				   FEAT_GRANITE, SQUARE_WALL_OUTER);

	/* Generate inner floors (a) */
	fill_rectangle(c, y1a, x1a, y2a, x2a, FEAT_FLOOR, SQUARE_NONE);

	/* Generate inner floors (b) */
	fill_rectangle(c, y1b, x1b, y2b, x2b, FEAT_FLOOR, SQUARE_NONE);

	return TRUE;
}


/**
 * Builds a cross-shaped room.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * Room "a" runs north/south, and Room "b" runs east/east 
 * So a "central pillar" would run from x1a,y1b to x2a,y2b.
 *
 * Note that currently, the "center" is always 3x3, but I think that the code
 * below will work for 5x5 (and perhaps even for unsymetric values like 4x3 or
 * 5x3 or 3x4 or 3x5).
 */
bool build_crossed(struct chunk *c, int y0, int x0)
{
	int y, x;
	int height, width;

	int y1a, x1a, y2a, x2a;
	int y1b, x1b, y2b, x2b;

	int dy, dx, wy, wx;

	int light = FALSE;

	/* Occasional light */
	if (c->depth <= randint1(25)) light = TRUE;

	/* Pick inner dimension */
	wy = 1;
	wx = 1;

	/* Pick outer dimension */
	dy = rand_range(3, 4);
	dx = rand_range(3, 11);

	/* Determine extents of room (a) */
	y1a = dy;
	x1a = wx;
	y2a = dy;
	x2a = wx;

	/* Determine extents of room (b) */
	y1b = wy;
	x1b = dx;
	y2b = wy;
	x2b = dx;

	/* Calculate height and width */
	height = MAX(y1a + y2a + 1, y1b + y2b + 1);
	width = MAX(x1a + x2a + 1, x1b + x2b + 1);

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height + 2, width + 2))
			return (FALSE);
	}

	/* locate room (b) */
	y1a = y0 - dy;
	x1a = x0 - wx;
	y2a = y0 + dy;
	x2a = x0 + wx;

	/* locate room (b) */
	y1b = y0 - wy;
	x1b = x0 - dx;
	y2b = y0 + wy;
	x2b = x0 + dx;

	/* Generate new room (a) */
	generate_room(c, y1a-1, x1a-1, y2a+1, x2a+1, light);

	/* Generate new room (b) */
	generate_room(c, y1b-1, x1b-1, y2b+1, x2b+1, light);

	/* Generate outer walls (a) */
	draw_rectangle(c, y1a-1, x1a-1, y2a+1, x2a+1, 
				   FEAT_GRANITE, SQUARE_WALL_OUTER);

	/* Generate outer walls (b) */
	draw_rectangle(c, y1b-1, x1b-1, y2b+1, x2b+1, 
				   FEAT_GRANITE, SQUARE_WALL_OUTER);

	/* Generate inner floors (a) */
	fill_rectangle(c, y1a, x1a, y2a, x2a, FEAT_FLOOR, SQUARE_NONE);

	/* Generate inner floors (b) */
	fill_rectangle(c, y1b, x1b, y2b, x2b, FEAT_FLOOR, SQUARE_NONE);

	/* Special features */
	switch (randint1(4)) {
		/* Nothing */
	case 1: break;

		/* Large solid middle pillar */
	case 2: {
		fill_rectangle(c, y1b, x1a, y2b, x2a, FEAT_GRANITE, SQUARE_WALL_INNER);
		break;
	}

		/* Inner treasure vault */
	case 3: {
		/* Generate a small inner vault */
		draw_rectangle(c, y1b, x1a, y2b, x2a, FEAT_GRANITE, SQUARE_WALL_INNER);

		/* Open the inner vault with a secret door */
		generate_hole(c, y1b, x1a, y2b, x2a, FEAT_SECRET);

		/* Place a treasure in the vault */
		place_object(c, y0, x0, c->depth, FALSE, FALSE, ORIGIN_SPECIAL, 0);

		/* Let's guard the treasure well */
		vault_monsters(c, y0, x0, c->depth + 2, randint0(2) + 3);

		/* Traps naturally */
		vault_traps(c, y0, x0, 4, 4, randint0(3) + 2);

		break;
	}

		/* Something else */
	case 4: {
		if (one_in_(3)) {
			/* Occasionally pinch the center shut */

			/* Pinch the east/west sides */
			for (y = y1b; y <= y2b; y++) {
				if (y == y0) continue;
				set_marked_granite(c, y, x1a - 1, SQUARE_WALL_INNER);
				set_marked_granite(c, y, x2a + 1, SQUARE_WALL_INNER);
			}

			/* Pinch the north/south sides */
			for (x = x1a; x <= x2a; x++) {
				if (x == x0) continue;
				set_marked_granite(c, y1b - 1, x, SQUARE_WALL_INNER);
				set_marked_granite(c, y2b + 1, x, SQUARE_WALL_INNER);
			}

			/* Open sides with secret doors */
			if (one_in_(3))
				generate_open(c, y1b-1, x1a-1, y2b+1, x2a+1, FEAT_SECRET);

		} else if (one_in_(3)) {
			/* Occasionally put a "plus" in the center */
			generate_plus(c, y1b, x1a, y2b, x2a, 
						  FEAT_GRANITE, SQUARE_WALL_INNER);

		} else if (one_in_(3)) {
			/* Occasionally put a "pillar" in the center */
			set_marked_granite(c, y0, x0, SQUARE_WALL_INNER);
		}

		break;
	}
	}

	return TRUE;
}


/**
 * Build a large room with an inner room.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * Possible sub-types:
 *	1 - An inner room
 *	2 - An inner room with a small inner room
 *	3 - An inner room with a pillar or pillars
 *	4 - An inner room with a checkerboard
 *	5 - An inner room with four compartments
 */
bool build_large(struct chunk *c, int y0, int x0)
{
	int y, x, y1, x1, y2, x2;
	int height = 9;
	int width = 23;

	int light = FALSE;

	/* Occasional light */
	if (c->depth <= randint1(25)) light = TRUE;

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height + 2, width + 2))
			return (FALSE);
	}

	/* Large room */
	y1 = y0 - height / 2;
	y2 = y0 + height / 2;
	x1 = x0 - width / 2;
	x2 = x0 + width / 2;

	/* Generate new room */
	generate_room(c, y1-1, x1-1, y2+1, x2+1, light);

	/* Generate outer walls */
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_OUTER);

	/* Generate inner floors */
	fill_rectangle(c, y1, x1, y2, x2, FEAT_FLOOR, SQUARE_NONE);

	/* The inner room */
	y1 = y1 + 2;
	y2 = y2 - 2;
	x1 = x1 + 2;
	x2 = x2 - 2;

	/* Generate inner walls */
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_INNER);

	/* Inner room variations */
	switch (randint1(5)) {
		/* An inner room */
	case 1: {
		/* Open the inner room with a secret door and place a monster */
		generate_hole(c, y1 - 1, x1 - 1, y2 + 1, x2 + 1, FEAT_SECRET);
		vault_monsters(c, y0, x0, c->depth + 2, 1);
		break;
	}


		/* An inner room with a small inner room */
	case 2: {
		/* Open the inner room with a secret door */
		generate_hole(c, y1 - 1, x1 - 1, y2 + 1, x2 + 1, FEAT_SECRET);

		/* Place another inner room */
		draw_rectangle(c, y0-1, x0-1, y0+1, x0+1, 
					   FEAT_GRANITE, SQUARE_WALL_INNER);

		/* Open the inner room with a locked door */
		generate_hole(c, y0 - 1, x0 - 1, y0 + 1, x0 + 1, FEAT_CLOSED);
		for (y = y0 - 1; y <= y0 + 1; y++)
			for (x = x0 - 1; x <= x0 + 1; x++)
				if (square_iscloseddoor(c, y, x))
					square_set_door_lock(c, y, x, randint1(7));

		/* Monsters to guard the treasure */
		vault_monsters(c, y0, x0, c->depth + 2, randint1(3) + 2);

		/* Object (80%) or Stairs (20%) */
		if (randint0(100) < 80)
			place_object(c, y0, x0, c->depth, FALSE, FALSE, ORIGIN_SPECIAL, 0);
		else
			place_random_stairs(c, y0, x0);

		/* Traps to protect the treasure */
		vault_traps(c, y0, x0, 4, 10, 2 + randint1(3));

		break;
	}


		/* An inner room with an inner pillar or pillars */
	case 3: {
		/* Open the inner room with a secret door */
		generate_hole(c, y1-1, x1-1, y2+1, x2+1, FEAT_SECRET);

		/* Inner pillar */
		fill_rectangle(c, y0-1, x0-1, y0+1, x0+1, 
					   FEAT_GRANITE, SQUARE_WALL_INNER);

		/* Occasionally, two more Large Inner Pillars */
		if (one_in_(2)) {
			if (one_in_(2)) {
				fill_rectangle(c, y0-1, x0-7, y0+1, x0-5, 
							   FEAT_GRANITE, SQUARE_WALL_INNER);
				fill_rectangle(c, y0-1, x0+5, y0+1, x0+7, 
							   FEAT_GRANITE, SQUARE_WALL_INNER);
			} else {
				fill_rectangle(c, y0-1, x0-6, y0+1, x0-4, 
							   FEAT_GRANITE, SQUARE_WALL_INNER);
				fill_rectangle(c, y0-1, x0+4, y0+1, x0+6, 
							   FEAT_GRANITE, SQUARE_WALL_INNER);
			}
		}

		/* Occasionally, some Inner rooms */
		if (one_in_(3)) {
			/* Inner rectangle */
			draw_rectangle(c, y0-1, x0-5, y0+1, x0+5, 
						   FEAT_GRANITE, SQUARE_WALL_INNER);

			/* Secret doors (random top/bottom) */
			place_secret_door(c, y0 - 3 + (randint1(2) * 2), x0 - 3);
			place_secret_door(c, y0 - 3 + (randint1(2) * 2), x0 + 3);

			/* Monsters */
			vault_monsters(c, y0, x0 - 2, c->depth + 2, randint1(2));
			vault_monsters(c, y0, x0 + 2, c->depth + 2, randint1(2));

			/* Objects */
			if (one_in_(3))
				place_object(c, y0, x0 - 2, c->depth, FALSE, FALSE,
							 ORIGIN_SPECIAL, 0);
			if (one_in_(3))
				place_object(c, y0, x0 + 2, c->depth, FALSE, FALSE,
							 ORIGIN_SPECIAL, 0);
		}

		break;
	}


		/* An inner room with a checkerboard */
	case 4: {
		/* Open the inner room with a secret door */
		generate_hole(c, y1-1, x1-1, y2+1, x2+1, FEAT_SECRET);

		/* Checkerboard */
		for (y = y1; y <= y2; y++)
			for (x = x1; x <= x2; x++)
				if ((x + y) & 0x01)
					set_marked_granite(c, y, x, SQUARE_WALL_INNER);

		/* Monsters just love mazes. */
		vault_monsters(c, y0, x0 - 5, c->depth + 2, randint1(3));
		vault_monsters(c, y0, x0 + 5, c->depth + 2, randint1(3));

		/* Traps make them entertaining. */
		vault_traps(c, y0, x0 - 3, 2, 8, randint1(3));
		vault_traps(c, y0, x0 + 3, 2, 8, randint1(3));

		/* Mazes should have some treasure too. */
		vault_objects(c, y0, x0, c->depth, 3);

		break;
	}


		/* Four small rooms. */
	case 5: {
		/* Inner "cross" */
		generate_plus(c, y1, x1, y2, x2, FEAT_GRANITE, SQUARE_WALL_INNER);

		/* Doors into the rooms */
		if (randint0(100) < 50) {
			int i = randint1(10);
			place_secret_door(c, y1 - 1, x0 - i);
			place_secret_door(c, y1 - 1, x0 + i);
			place_secret_door(c, y2 + 1, x0 - i);
			place_secret_door(c, y2 + 1, x0 + i);
		} else {
			int i = randint1(3);
			place_secret_door(c, y0 + i, x1 - 1);
			place_secret_door(c, y0 - i, x1 - 1);
			place_secret_door(c, y0 + i, x2 + 1);
			place_secret_door(c, y0 - i, x2 + 1);
		}

		/* Treasure, centered at the center of the cross */
		vault_objects(c, y0, x0, c->depth, 2 + randint1(2));

		/* Gotta have some monsters */
		vault_monsters(c, y0 + 1, x0 - 4, c->depth + 2, randint1(4));
		vault_monsters(c, y0 + 1, x0 + 4, c->depth + 2, randint1(4));
		vault_monsters(c, y0 - 1, x0 - 4, c->depth + 2, randint1(4));
		vault_monsters(c, y0 - 1, x0 + 4, c->depth + 2, randint1(4)); 

		break;
	}
	}

	return TRUE;
}


/**
 * Hook for picking monsters appropriate to a nest/pit or region.
 * \param race the race being tested for inclusion
 * \return the race is acceptable
 * Requires dun->pit_type to be set.
 */
bool mon_pit_hook(struct monster_race *race)
{
	bool match_base = TRUE;
	bool match_color = TRUE;

	assert(race);
	assert(dun->pit_type);

	if (rf_has(race->flags, RF_UNIQUE))
		return FALSE;
	else if (!rf_is_subset(race->flags, dun->pit_type->flags))
		return FALSE;
	else if (rf_is_inter(race->flags, dun->pit_type->forbidden_flags))
		return FALSE;
	else if (!rsf_is_subset(race->spell_flags, dun->pit_type->spell_flags))
		return FALSE;
	else if (rsf_is_inter(race->spell_flags, dun->pit_type->forbidden_spell_flags))
		return FALSE;
	else if (dun->pit_type->forbidden_monsters) {
		struct pit_forbidden_monster *monster;
		for (monster = dun->pit_type->forbidden_monsters; monster; monster = monster->next) {
			if (race == monster->race)
				return FALSE;
		}
	}

	if (dun->pit_type->bases) {
		struct pit_monster_profile *bases;
		match_base = FALSE;

		for (bases = dun->pit_type->bases; bases; bases = bases->next) {
			if (race->base == bases->base)
				match_base = TRUE;
		}
	}
	
	if (dun->pit_type->colors) {
		struct pit_color_profile *colors;
		match_color = FALSE;

		for (colors = dun->pit_type->colors; colors; colors = colors->next) {
			if (race->d_attr == colors->color)
				match_color = TRUE;
		}
	}

	return (match_base && match_color);
}

/**
 * Pick a type of monster for pits (or other purposes), based on the level.
 * 
 * We scan through all pit profiles, and for each one generate a random depth
 * using a normal distribution, with the mean given in pit.txt, and a
 * standard deviation of 10. Then we pick the profile that gave us a depth that
 * is closest to the player's actual depth.
 *
 * Sets dun->pit_type, which is required for mon_pit_hook.
 * \param depth is the pit profile depth to aim for in selection
 * \param type is 1 for pits, 2 for nests, 0 for any profile
 */
void set_pit_type(int depth, int type)
{
	int i;
	int pit_idx = 0;
	
	/* Hack -- set initial distance large */
	int pit_dist = 999;
	
	for (i = 0; i < z_info->pit_max; i++) {
		int offset, dist;
		struct pit_profile *pit = &pit_info[i];
		
		/* Skip empty pits or pits of the wrong room type */
		if (type && (!pit->name || pit->room_type != type)) continue;
		
		offset = Rand_normal(pit->ave, 10);
		dist = ABS(offset - depth);
		
		if (dist < pit_dist && one_in_(pit->rarity)) {
			/* This pit is the closest so far */
			pit_idx = i;
			pit_dist = dist;
		}
	}

	dun->pit_type = &pit_info[pit_idx];
}


/**
 * Build a monster nest
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * A monster nest consists of a rectangular moat around a room containing
 * monsters of a given type.
 *
 * The monsters are chosen from a set of 64 randomly selected monster races,
 * to allow the nest creation to fail instead of having "holes".
 *
 * Note the use of the "get_mon_num_prep()" function to prepare the
 * "monster allocation table" in such a way as to optimize the selection
 * of "appropriate" non-unique monsters for the nest.
 *
 * The available monster nests are specified in edit/pit.txt.
 *
 * Note that get_mon_num() function can fail, in which case the nest will be
 * empty, and will not affect the level rating.
 *
 * Monster nests will never contain unique monsters.
 */
bool build_nest(struct chunk *c, int y0, int x0)
{
	int y, x, y1, x1, y2, x2;
	int i;
	int alloc_obj;
	struct monster_race *what[64];
	bool empty = FALSE;
	int light = FALSE;
	int size_vary = randint0(4);
	int height = 9;
	int width = 11 + 2 * size_vary;

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height + 2, width + 2))
			return (FALSE);
	}

	/* Large room */
	y1 = y0 - height / 2;
	y2 = y0 + height / 2;
	x1 = x0 - width / 2;
	x2 = x0 + width / 2;

	/* Generate new room */
	generate_room(c, y1-1, x1-1, y2+1, x2+1, light);

	/* Generate outer walls */
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_OUTER);

	/* Generate inner floors */
	fill_rectangle(c, y1, x1, y2, x2, FEAT_FLOOR, SQUARE_NONE);

	/* Advance to the center room */
	y1 = y1 + 2;
	y2 = y2 - 2;
	x1 = x1 + 2;
	x2 = x2 - 2;

	/* Generate inner walls */
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_INNER);

	/* Open the inner room with a secret door */
	generate_hole(c, y1-1, x1-1, y2+1, x2+1, FEAT_SECRET);

	/* Decide on the pit type */
	set_pit_type(c->depth, 2);

	/* Chance of objects on the floor */
	alloc_obj = dun->pit_type->obj_rarity;
	
	/* Prepare allocation table */
	get_mon_num_prep(mon_pit_hook);

	/* Pick some monster types */
	for (i = 0; i < 64; i++) {
		/* Get a (hard) monster type */
		what[i] = get_mon_num(c->depth + 10);

		/* Notice failure */
		if (!what[i]) empty = TRUE;
	}

	/* Prepare allocation table */
	get_mon_num_prep(NULL);

	/* Oops */
	if (empty) return FALSE;

	/* Describe */
	ROOM_LOG("Monster nest (%s)", dun->pit_type->name);

	/* Increase the level rating */
	c->mon_rating += (size_vary + dun->pit_type->ave / 20);

	/* Place some monsters */
	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			/* Figure out what monster is being used, and place that monster */
			struct monster_race *race = what[randint0(64)];
			place_new_monster(c, y, x, race, FALSE, FALSE, ORIGIN_DROP_PIT);

			/* Occasionally place an item, making it good 1/3 of the time */
			if (randint0(100) < alloc_obj) 
				place_object(c, y, x, c->depth + 10, one_in_(3), FALSE,
							 ORIGIN_PIT, 0);
		}
	}

	return TRUE;
}

/**
 * Build a monster pit
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * Monster pits are laid-out similarly to monster nests.
 *
 * The available monster pits are specified in edit/pit.txt.
 *
 * The inside room in a monster pit appears as shown below, where the
 * actual monsters in each location depend on the type of the pit
 *
 *   #############
 *   #11000000011#
 *   #01234543210#
 *   #01236763210#
 *   #01234543210#
 *   #11000000011#
 *   #############
 *
 * Note that the monsters in the pit are chosen by using get_mon_num() to
 * request 16 "appropriate" monsters, sorting them by level, and using the
 * "even" entries in this sorted list for the contents of the pit.
 *
 * Note the use of get_mon_num_prep() to prepare the monster allocation
 * table in such a way as to optimize the selection of appropriate non-unique
 * monsters for the pit.
 *
 * The get_mon_num() function can fail, in which case the pit will be empty,
 * and will not effect the level rating.
 *
 * Like monster nests, monster pits will never contain unique monsters.
 */
bool build_pit(struct chunk *c, int y0, int x0)
{
	struct monster_race *what[16];
	int i, j, y, x, y1, x1, y2, x2;
	bool empty = FALSE;
	int light = FALSE;
	int alloc_obj;
	int height = 9;
	int width = 15;

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height + 2, width + 2))
			return (FALSE);
	}

	/* Large room */
	y1 = y0 - height / 2;
	y2 = y0 + height / 2;
	x1 = x0 - width / 2;
	x2 = x0 + width / 2;

	/* Generate new room, outer walls and inner floor */
	generate_room(c, y1-1, x1-1, y2+1, x2+1, light);
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_OUTER);
	fill_rectangle(c, y1, x1, y2, x2, FEAT_FLOOR, SQUARE_NONE);

	/* Advance to the center room */
	y1 = y1 + 2;
	y2 = y2 - 2;
	x1 = x1 + 2;
	x2 = x2 - 2;

	/* Generate inner walls, and open with a secret door */
	draw_rectangle(c, y1-1, x1-1, y2+1, x2+1, FEAT_GRANITE, SQUARE_WALL_INNER);
	generate_hole(c, y1-1, x1-1, y2+1, x2+1, FEAT_SECRET);

	/* Decide on the pit type */
	set_pit_type(c->depth, 1);

	/* Chance of objects on the floor */
	alloc_obj = dun->pit_type->obj_rarity;
	
	/* Prepare allocation table */
	get_mon_num_prep(mon_pit_hook);

	/* Pick some monster types */
	for (i = 0; i < 16; i++) {
		/* Get a (hard) monster type */
		what[i] = get_mon_num(c->depth + 10);

		/* Notice failure */
		if (!what[i]) empty = TRUE;
	}

	/* Prepare allocation table */
	get_mon_num_prep(NULL);

	/* Oops */
	if (empty)
		return FALSE;

	ROOM_LOG("Monster pit (%s)", dun->pit_type->name);

	/* Sort the entries XXX XXX XXX */
	for (i = 0; i < 16 - 1; i++) {
		/* Sort the entries */
		for (j = 0; j < 16 - 1; j++) {
			int i1 = j;
			int i2 = j + 1;

			int p1 = what[i1]->level;
			int p2 = what[i2]->level;

			/* Bubble */
			if (p1 > p2) {
				struct monster_race *tmp = what[i1];
				what[i1] = what[i2];
				what[i2] = tmp;
			}
		}
	}

	/* Select every other entry */
	for (i = 0; i < 8; i++)
		what[i] = what[i * 2];

	/* Increase the level rating */
	c->mon_rating += (3 + dun->pit_type->ave / 20);

	/* Top and bottom rows (middle) */
	for (x = x0 - 3; x <= x0 + 3; x++) {
		place_new_monster(c, y0 - 2, x, what[0], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y0 + 2, x, what[0], FALSE, FALSE, ORIGIN_DROP_PIT);
	}
    
	/* Corners */
	for (x = x0 - 5; x <= x0 - 4; x++) {
		place_new_monster(c, y0 - 2, x, what[1], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y0 + 2, x, what[1], FALSE, FALSE, ORIGIN_DROP_PIT);
	}
    
	for (x = x0 + 4; x <= x0 + 5; x++) {
		place_new_monster(c, y0 - 2, x, what[1], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y0 + 2, x, what[1], FALSE, FALSE, ORIGIN_DROP_PIT);
	}
    
	/* Corners */

	/* Middle columns */
	for (y = y0 - 1; y <= y0 + 1; y++) {
		place_new_monster(c, y, x0 - 5, what[0], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y, x0 + 5, what[0], FALSE, FALSE, ORIGIN_DROP_PIT);

		place_new_monster(c, y, x0 - 4, what[1], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y, x0 + 4, what[1], FALSE, FALSE, ORIGIN_DROP_PIT);

		place_new_monster(c, y, x0 - 3, what[2], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y, x0 + 3, what[2], FALSE, FALSE, ORIGIN_DROP_PIT);

		place_new_monster(c, y, x0 - 2, what[3], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y, x0 + 2, what[3], FALSE, FALSE, ORIGIN_DROP_PIT);
	}
    
	/* Corners around the middle monster */
	place_new_monster(c, y0 - 1, x0 - 1, what[4], FALSE, FALSE, ORIGIN_DROP_PIT);
	place_new_monster(c, y0 - 1, x0 + 1, what[4], FALSE, FALSE, ORIGIN_DROP_PIT);
	place_new_monster(c, y0 + 1, x0 - 1, what[4], FALSE, FALSE, ORIGIN_DROP_PIT);
	place_new_monster(c, y0 + 1, x0 + 1, what[4], FALSE, FALSE, ORIGIN_DROP_PIT);

	/* Above/Below the center monster */
	for (x = x0 - 1; x <= x0 + 1; x++) {
		place_new_monster(c, y0 + 1, x, what[5], FALSE, FALSE, ORIGIN_DROP_PIT);
		place_new_monster(c, y0 - 1, x, what[5], FALSE, FALSE, ORIGIN_DROP_PIT);
	}

	/* Next to the center monster */
	place_new_monster(c, y0, x0 + 1, what[6], FALSE, FALSE, ORIGIN_DROP_PIT);
	place_new_monster(c, y0, x0 - 1, what[6], FALSE, FALSE, ORIGIN_DROP_PIT);

	/* Center monster */
	place_new_monster(c, y0, x0, what[7], FALSE, FALSE, ORIGIN_DROP_PIT);

	/* Place some objects */
	for (y = y0 - 2; y <= y0 + 2; y++) {
		for (x = x0 - 9; x <= x0 + 9; x++) {
			/* Occasionally place an item, making it good 1/3 of the time */
			if (randint0(100) < alloc_obj) 
				place_object(c, y, x, c->depth + 10, one_in_(3), FALSE,
							 ORIGIN_PIT, 0);
		}
	}

	return TRUE;
}

/**
 * Build a room template from its string representation.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \param ymax 
 * \param xmax the room dimensions
 * \param doors the door position
 * \param data the room template text description
 * \param tval the object type for any included objects
 * \return success
 */
static bool build_room_template(struct chunk *c, int y0, int x0, int ymax, int xmax, int doors, const char *data, int tval)
{
	int dx, dy, x, y, rnddoors, doorpos;
	const char *t;
	bool rndwalls, light;
	

	assert(c);

	/* Occasional light */
	light = c->depth <= randint1(25) ? TRUE : FALSE;

	/* Set the random door position here so it generates doors in all squares
	 * marked with the same number */
	rnddoors = randint1(doors);

	/* Decide whether optional walls will be generated this time */
	rndwalls = one_in_(2) ? TRUE : FALSE;

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, ymax + 2, xmax + 2))
			return (FALSE);
	}

	/* Place dungeon features and objects */
	for (t = data, dy = 0; dy < ymax && *t; dy++) {
		for (dx = 0; dx < xmax && *t; dx++, t++) {
			/* Extract the location */
			x = x0 - (xmax / 2) + dx;
			y = y0 - (ymax / 2) + dy;

			/* Skip non-grids */
			if (*t == ' ') continue;

			/* Lay down a floor */
			square_set_feat(c, y, x, FEAT_FLOOR);

			/* Debugging assertion */
			assert(square_isempty(c, y, x));

			/* Analyze the grid */
			switch (*t) {
			case '%': set_marked_granite(c, y, x, SQUARE_WALL_OUTER); break;
			case '#': set_marked_granite(c, y, x, SQUARE_WALL_SOLID); break;
			case '+': place_secret_door(c, y, x); break;
			case 'x': {

				/* If optional walls are generated, put a wall in this square */
				if (rndwalls)
					set_marked_granite(c, y, x, SQUARE_WALL_SOLID);
				break;
			}
			case '(': {

				/* If optional walls are generated, put a door in this square */
				if (rndwalls)
					place_secret_door(c, y, x);
				break;
			}
			case ')': {
				/* If no optional walls generated, put a door in this square */
				if (!rndwalls)
					place_secret_door(c, y, x);
				else
					set_marked_granite(c, y, x, SQUARE_WALL_SOLID);
				break;
			}
			case '8': {

				/* Put something nice in this square
				 * Object (80%) or Stairs (20%) */
				if (randint0(100) < 80)
					place_object(c, y, x, c->depth, FALSE, FALSE, ORIGIN_SPECIAL, 0);
				else
					place_random_stairs(c, y, x);

				/* Some monsters to guard it */
				vault_monsters(c, y, x, c->depth + 2, randint0(2) + 3);

				/* And some traps too */
				vault_traps(c, y, x, 4, 4, randint0(3) + 2);

				break;
			}
			case '9': {

				/* Create some interesting stuff nearby */

				/* A few monsters */
				vault_monsters(c, y - 3, x - 3, c->depth + randint0(2), randint1(2));
				vault_monsters(c, y + 3, x + 3, c->depth + randint0(2), randint1(2));

				/* And maybe a bit of treasure */

				if (one_in_(2))
					vault_objects(c, y - 2, x + 2, c->depth, 1 + randint0(2));

				if (one_in_(2))
					vault_objects(c, y + 2, x - 2, c->depth, 1 + randint0(2));

				break;

			}
			case '[': {
				
				/* Place an object of the template's specified tval */
				place_object(c, y, x, c->depth, FALSE, FALSE, ORIGIN_SPECIAL, tval);
				break;
			}
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6': {
				/* Check if this is chosen random door position */
				doorpos = (int) (*t - '0');

				if (doorpos == rnddoors)
					place_secret_door(c, y, x);
				else
					set_marked_granite(c, y, x, SQUARE_WALL_SOLID);

				break;
			}
			}

			/* Part of a room */
			sqinfo_on(c->squares[y][x].info, SQUARE_ROOM);
			if (light)
				sqinfo_on(c->squares[y][x].info, SQUARE_GLOW);
		}
	}

	return TRUE;
}


/**
 * Helper function for building room templates.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \param typ the room template type (currently unused)
 * \return success
 */
static bool build_room_template_type(struct chunk *c, int y0, int x0, int typ)
{
	struct room_template *room = random_room_template(typ);
	
	if (room == NULL)
		return FALSE;

	/* Build the room */
	if (!build_room_template(c, y0, x0, room->hgt, room->wid, room->dor,
							 room->text, room->tval))
		return FALSE;

	ROOM_LOG("Room template (%s)", room->name);

	return TRUE;
}

/**
 * Build a template room
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
*/
bool build_template(struct chunk *c, int y0, int x0)
{
	/* All room templates currently have type 1 */
	return build_room_template_type(c, y0, x0, 1);
}




/**
 * Build a vault from its string representation.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \param v pointer to the vault template
 * \return success
 */
bool build_vault(struct chunk *c, int y0, int x0, struct vault *v)
{
	const char *data = v->text;
	int y1, x1, y2, x2;
	int x, y, races = 0;
	const char *t;
	char racial_symbol[30] = "";
	bool icky;

	assert(c);

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, v->hgt + 2, v->wid + 2))
			return (FALSE);
	}

	/* Get the room corners */
	y1 = y0 - (v->hgt / 2);
	x1 = x0 - (v->wid / 2);
	y2 = y1 + v->hgt - 1;
	x2 = x1 + v->wid - 1;

	/* No random monsters in vaults. */
	generate_mark(c, y1, x1, y2, x2, SQUARE_MON_RESTRICT);

	/* Place dungeon features and objects */
	for (t = data, y = y1; y <= y2 && *t; y++) {
		for (x = x1; x <= x2 && *t; x++, t++) {
			/* Skip non-grids */
			if (*t == ' ') continue;

			/* Lay down a floor */
			square_set_feat(c, y, x, FEAT_FLOOR);

			/* Debugging assertion */
			assert(square_isempty(c, y, x));

			/* By default vault squares are marked icky */
			icky = TRUE;

			/* Analyze the grid */
			switch (*t) {
			case '%': {
				/* In this case, the square isn't really part of the
				 * vault, but rather is part of the "door step" to the
				 * vault. We don't mark it icky so that the tunneling
				 * code knows its allowed to remove this wall. */
				set_marked_granite(c, y, x, SQUARE_WALL_OUTER);
				icky = FALSE;
				break;
			}
				/* Inner granite wall */
			case '#': set_marked_granite(c, y, x, SQUARE_WALL_INNER); break;
				/* Permanent wall */
			case '@': square_set_feat(c, y, x, FEAT_PERM); break;
				/* Gold seam */
			case '*': {
				square_set_feat(c, y, x, one_in_(2) ? FEAT_MAGMA_K :
								FEAT_QUARTZ_K);
				break;
			}
				/* Rubble */
			case ':': square_set_feat(c, y, x, FEAT_RUBBLE); break;
				/* Secret door */
			case '+': place_secret_door(c, y, x); break;
				/* Trap */
			case '^': place_trap(c, y, x, -1, c->depth); break;
				/* Treasure or a trap */
			case '&': {
				if (randint0(100) < 75)
					place_object(c, y, x, c->depth, FALSE, FALSE, ORIGIN_VAULT, 0);
				else
					place_trap(c, y, x, -1, c->depth);
				break;
			}
				/* Stairs */
			case '<': square_set_feat(c, y, x, FEAT_LESS); break;
			case '>': {
				/* No down stairs at bottom or on quests */
				if (is_quest(c->depth) || c->depth >= z_info->max_depth - 1)
					square_set_feat(c, y, x, FEAT_LESS);
				else
					square_set_feat(c, y, x, FEAT_MORE);
				break;
			}
				/* Included to allow simple inclusion of FA vaults */
			case '`': /*square_set_feat(c, y, x, FEAT_LAVA)*/; break;
			case '/': /*square_set_feat(c, y, x, FEAT_WATER)*/; break;
			case ';': /*square_set_feat(c, y, x, FEAT_TREE)*/; break;
			}

			/* Part of a vault */
			sqinfo_on(c->squares[y][x].info, SQUARE_ROOM);
			if (icky) sqinfo_on(c->squares[y][x].info, SQUARE_VAULT);
		}
	}


	/* Place regular dungeon monsters and objects */
	for (t = data, y = y1; y <= y2 && *t; y++) {
		for (x = x1; x <= x2 && *t; x++, t++) {
			/* Hack -- skip "non-grids" */
			if (*t == ' ') continue;

			/* Most alphabetic characters signify monster races. */
			if (isalpha(*t) && (*t != 'x') && (*t != 'X')) {
				/* If the symbol is not yet stored, ... */
				if (!strchr(racial_symbol, *t)) {
					/* ... store it for later processing. */
					if (races < 30)
						racial_symbol[races++] = *t;
				}
			}

			/* Otherwise, analyze the symbol */
			else
				switch (*t) {
					/* An ordinary monster, object (sometimes good), or trap. */
				case '1': {
					if (one_in_(2))
						pick_and_place_monster(c, y, x, c->depth , TRUE, TRUE,
											   ORIGIN_DROP_VAULT);
					else if (one_in_(2))
						place_object(c, y, x, c->depth, one_in_(8) ? TRUE : FALSE, FALSE, ORIGIN_VAULT, 0);
					else
						place_trap(c, y, x, -1, c->depth);
					break;
				}
					/* Slightly out of depth monster. */
				case '2': pick_and_place_monster(c, y, x, c->depth + 5, TRUE, TRUE, ORIGIN_DROP_VAULT); break;
					/* Slightly out of depth object. */
				case '3': place_object(c, y, x, c->depth + 3, FALSE, FALSE, 
									   ORIGIN_VAULT, 0); break;
					/* Monster and/or object */
				case '4': {
					if (one_in_(2))
						pick_and_place_monster(c, y, x, c->depth + 3, TRUE, 
											   TRUE, ORIGIN_DROP_VAULT);
					if (one_in_(2))
						place_object(c, y, x, c->depth + 7, FALSE, FALSE,
									 ORIGIN_VAULT, 0);
					break;
				}
					/* Out of depth object. */
				case '5': place_object(c, y, x, c->depth + 7, FALSE, FALSE,
									   ORIGIN_VAULT, 0); break;
					/* Out of depth monster. */
				case '6': pick_and_place_monster(c, y, x, c->depth + 11, TRUE, TRUE, ORIGIN_DROP_VAULT); break;
					/* Very out of depth object. */
				case '7': place_object(c, y, x, c->depth + 15, FALSE, FALSE,
									   ORIGIN_VAULT, 0); break;
					/* Very out of depth monster. */
				case '0': pick_and_place_monster(c, y, x, c->depth + 20, TRUE, TRUE, ORIGIN_DROP_VAULT); break;
					/* Meaner monster, plus treasure */
				case '9': {
					pick_and_place_monster(c, y, x, c->depth + 9, TRUE, TRUE,
										   ORIGIN_DROP_VAULT);
					place_object(c, y, x, c->depth + 7, TRUE, FALSE,
								 ORIGIN_VAULT, 0);
					break;
				}
					/* Nasty monster and treasure */
				case '8': {
					pick_and_place_monster(c, y, x, c->depth + 40, TRUE, TRUE,
										   ORIGIN_DROP_VAULT);
					place_object(c, y, x, c->depth + 20, TRUE, TRUE,
								 ORIGIN_VAULT, 0);
					break;
				}
					/* A chest. */
				case '~': place_object(c, y, x, c->depth + 5, TRUE, TRUE,
									   ORIGIN_VAULT, TV_CHEST); break;
					/* Treasure. */
				case '$': place_gold(c, y, x, c->depth, ORIGIN_VAULT);break;
					/* Armour. */
				case ']': {
					int	tval = 0, temp = one_in_(3) ? randint1(9) : randint1(8);
					switch (temp) {
					case 1: tval = TV_BOOTS; break;
					case 2: tval = TV_GLOVES; break;
					case 3: tval = TV_HELM; break;
					case 4: tval = TV_CROWN; break;
					case 5: tval = TV_SHIELD; break;
					case 6: tval = TV_CLOAK; break;
					case 7: tval = TV_SOFT_ARMOR; break;
					case 8: tval = TV_HARD_ARMOR; break;
					case 9: tval = TV_DRAG_ARMOR; break;
					}
					place_object(c, y, x, c->depth + 3, TRUE, FALSE,
								 ORIGIN_VAULT, tval);
					break;
				}
					/* Weapon. */
				case '|': {
					int	tval = 0, temp = randint1(4);
					switch (temp) {
					case 1: tval = TV_SWORD; break;
					case 2: tval = TV_POLEARM; break;
					case 3: tval = TV_HAFTED; break;
					case 4: tval = TV_BOW; break;
					}
					place_object(c, y, x, c->depth + 3, TRUE, FALSE,
								 ORIGIN_VAULT, tval);
					break;
				}
					/* Ring. */
				case '=': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, TV_RING); break;
					/* Amulet. */
				case '"': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, TV_AMULET); break;
					/* Potion. */
				case '!': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, TV_POTION); break;
					/* Scroll. */
				case '?': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, TV_SCROLL); break;
					/* Staff. */
				case '_': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, TV_STAFF); break;
					/* Wand or rod. */
				case '-': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, one_in_(2) ? TV_WAND : TV_ROD);
					break;
					/* Food or mushroom. */
				case ',': place_object(c, y, x, c->depth + 3, one_in_(4), FALSE,
									   ORIGIN_VAULT, TV_FOOD); break;
				}
		}
	}

	/* Place specified monsters */
	get_vault_monsters(c, racial_symbol, v->typ, data, y1, y2, x1, x2);

	return TRUE;
}


/**
 * Helper function for building vaults.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \param typ the vault type
 * \param label name of the vault type (eg "Greater vault")
 * \return success
 */
static bool build_vault_type(struct chunk *c, int y0, int x0, const char *typ)
{
	struct vault *v = random_vault(c->depth, typ);
	if (v == NULL) {
		/*quit_fmt("got NULL from random_vault(%s)", typ);*/
		return FALSE;
	}

	/* Build the vault */
	if (!build_vault(c, y0, x0, v))
		return FALSE;

	ROOM_LOG("%s (%s)", typ, v->name);

	/* Boost the rating */
	c->mon_rating += v->rat;

	return TRUE;
}


/**
 * Build an interesting room.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_interesting(struct chunk *c, int y0, int x0)
{
	return build_vault_type(c, y0, x0, "Interesting room");
}


/**
 * Build a lesser vault.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_lesser_vault(struct chunk *c, int y0, int x0)
{
	if (!streq(dun->profile->name, "classic") && (one_in_(2)))
		return build_vault_type(c, y0, x0, "Lesser vault (new)");
	return build_vault_type(c, y0, x0, "Lesser vault");
}


/**
 * Build a medium vault.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_medium_vault(struct chunk *c, int y0, int x0)
{
	if (!streq(dun->profile->name, "classic") && (one_in_(2)))
		return build_vault_type(c, y0, x0, "Medium vault (new)");
	return build_vault_type(c, y0, x0, "Medium vault");
}


/**
 * Build a greater vaults.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * Since Greater Vaults are so large (4x6 blocks, in a 6x18 dungeon) there is
 * a 63% chance that a randomly chosen quadrant to start a GV on won't work.
 * To balance this, we give Greater Vaults an artificially high probability
 * of being attempted, and then in this function use a depth check to cancel
 * vault creation except at deep depths.
 *
 * The following code should make a greater vault with frequencies:
 * dlvl  freq
 * 100+  18.0%
 * 90-99 16.0 - 18.0%
 * 80-89 10.0 - 11.0%
 * 70-79  5.7 -  6.5%
 * 60-69  3.3 -  3.8%
 * 50-59  1.8 -  2.1%
 * 0-49   0.0 -  1.0%
 */
bool build_greater_vault(struct chunk *c, int y0, int x0)
{
	int i;
	int numerator   = 2;
	int denominator = 3;
	
	/* Only try to build a GV as the first room. */
	if (dun->cent_n > 0) return FALSE;

	/* Level 90+ has a 2/3 chance, level 80-89 has 4/9, ... */
	for(i = 90; i > c->depth; i -= 10) {
		numerator *= 2;
		denominator *= 3;
	}

	/* Attempt to pass the depth check and build a GV */
	if (randint0(denominator) >= numerator) return FALSE;

	if (!streq(dun->profile->name, "classic") && (one_in_(2)))
		return build_vault_type(c, y0, x0, "Greater vault (new)");
	return build_vault_type(c, y0, x0, "Greater vault");
}


/**
 * Moria room (from Oangband).  Uses the "starburst room" code.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 */
bool build_moria(struct chunk *c, int y0, int x0)
{
	int y1, x1, y2, x2;
	int i;
	int height, width;

	bool light = c->depth <= randint1(35);

	/* Pick a room size */
	height = 8 + randint0(5);
	width = 10 + randint0(5);


	/* Try twice to find space for a room. */
	for (i = 0; i < 2; i++) {
		/* Really large room - only on first try. */
		if ((i == 0) && one_in_(15)) {
			height *= 1 + randint1(2);
			width *= 2 + randint1(3);
		}

		/* Long, narrow room.  Sometimes tall and thin. */
		else if (!one_in_(4)) {
			if (one_in_(15))
				height *= 2 + randint0(2);
			else
				width *= 2 + randint0(3);
		}

		/* Find and reserve some space in the dungeon.  Get center of room. */
		if ((y0 >= c->height) || (x0 >= c->width)) {
			if (!find_space(&y0, &x0, height, width)) {
				if (i == 0) continue;  /* Failed first attempt */
				if (i == 1) return (FALSE);  /* Failed second attempt */
			} else break;  /* Success */
		} else break;   /* Not finding space */
	}

	/* Locate the room */
	y1 = y0 - height / 2;
	x1 = x0 - width / 2;
	y2 = y1 + height - 1;
	x2 = x1 + width - 1;


	/* Generate starburst room.  Return immediately if out of bounds. */
	if (!generate_starburst_room(c, y1, x1, y2, x2, light, FEAT_FLOOR, TRUE)) {
		return (FALSE);
	}

	/* Sometimes, the room may have rubble in it. */
	if (one_in_(10))
		(void) generate_starburst_room(c, y1 + randint0(height / 4),
									   x1 + randint0(width / 4),
									   y2 - randint0(height / 4),
									   x2 - randint0(width / 4), FALSE,
									   FEAT_RUBBLE, FALSE);

	/* Success */
	return (TRUE);
}

/**
 * Helper for rooms of chambers; builds a marked wall grid if appropriate
 * \param c the chunk the room is being built in
 * \param y
 * \param x co-ordinates 
 */
static void make_inner_chamber_wall(struct chunk *c, int y, int x)
{
	if ((c->squares[y][x].feat != FEAT_GRANITE) &&
		(c->squares[y][x].feat != FEAT_MAGMA))
		return;
	if (square_iswall_outer(c, y, x)) return;
	if (square_iswall_solid(c, y, x)) return;
	set_marked_granite(c, y, x, SQUARE_WALL_INNER);
}

/**
 * Helper function for rooms of chambers.  Fill a room matching
 * the rectangle input with magma, and surround it with inner wall.
 * Create a door in a random inner wall grid along the border of the
 * rectangle.
 * \param c the chunk the room is being built in
 * \param y1
 * \param x1
 * \param y2
 * \param x2 chamber dimensions
 */
static void make_chamber(struct chunk *c, int y1, int x1, int y2, int x2)
{
	int i, d, y, x;
	int count;

	/* Fill with soft granite (will later be replaced with floor). */
	fill_rectangle(c, y1 + 1, x1 + 1, y2 - 1, x2 - 1, FEAT_MAGMA,
				   SQUARE_NONE);

	/* Generate inner walls over dungeon granite and magma. */
	for (y = y1; y <= y2; y++) {
		/* left wall */
		make_inner_chamber_wall(c, y, x1);
		/* right wall */
		make_inner_chamber_wall(c, y, x2);
	}

	for (x = x1; x <= x2; x++) {
		/* top wall */
		make_inner_chamber_wall(c, y1, x);
		/* bottom wall */
		make_inner_chamber_wall(c, y2, x);
	}

	/* Try a few times to place a door. */
	for (i = 0; i < 20; i++) {
		/* Pick a square along the edge, not a corner. */
		if (one_in_(2)) {
			/* Somewhere along the (interior) side walls. */
			x = one_in_(2) ? x1 : x2;
			y = y1 + randint0(1 + ABS(y2 - y1));
		} else {
			/* Somewhere along the (interior) top and bottom walls. */
			y = one_in_(2) ? y1 : y2;
			x = x1 + randint0(1 + ABS(x2 - x1));
		}

		/* If not an inner wall square, try again. */
		if (!square_iswall_inner(c, y, x))
			continue;

		/* Paranoia */
		if (!square_in_bounds_fully(c, y, x))
			continue;

		/* Reset wall count */
		count = 0;

		/* If square has not more than two adjacent walls, and no adjacent
		 * doors, place door. */
		for (d = 0; d < 9; d++) {
			/* Extract adjacent (legal) location */
			int yy = y + ddy_ddd[d];
			int xx = x + ddx_ddd[d];

			/* No doors beside doors. */
			if (c->squares[yy][xx].feat == FEAT_OPEN)
				break;

			/* Count the inner walls. */
			if (square_iswall_inner(c, yy, xx))
				count++;

			/* No more than two walls adjacent (plus the one we're on). */
			if (count > 3)
				break;

			/* Checked every direction? */
			if (d == 8) {
				/* Place an open door. */
				square_set_feat(c, y, x, FEAT_OPEN);

				/* Success. */
				return;
			}
		}
	}
}



/**
 * Expand in every direction from a start point, turning magma into rooms.
 * Stop only when the magma and the open doors totally run out.
 * \param c the chunk the room is being built in
 * \param y
 * \param x co-ordinates to start hollowing
 */
static void hollow_out_room(struct chunk *c, int y, int x)
{
	int d, yy, xx;

	for (d = 0; d < 9; d++) {
		/* Extract adjacent location */
		yy = y + ddy_ddd[d];
		xx = x + ddx_ddd[d];

		/* Change magma to floor. */
		if (c->squares[yy][xx].feat == FEAT_MAGMA) {
			square_set_feat(c, yy, xx, FEAT_FLOOR);

			/* Hollow out the room. */
			hollow_out_room(c, yy, xx);
		}
		/* Change open door to broken door. */
		else if (c->squares[yy][xx].feat == FEAT_OPEN) {
			square_set_feat(c, yy, xx, FEAT_BROKEN);

			/* Hollow out the (new) room. */
			hollow_out_room(c, yy, xx);
		}
	}
}



/**
 * Rooms of chambers
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * Build a room, varying in size between 22x22 and 44x66, consisting of
 * many smaller, irregularly placed, chambers all connected by doors or
 * short tunnels. -LM-
 *
 * Plop down an area-dependent number of magma-filled chambers, and remove
 * blind doors and tiny rooms.
 *
 * Hollow out a chamber near the center, connect it to new chambers, and
 * hollow them out in turn.  Continue in this fashion until there are no
 * remaining chambers within two squares of any cleared chamber.
 *
 * Clean up doors.  Neaten up the wall types.  Turn floor grids into rooms,
 * illuminate if requested.
 *
 * Fill the room with up to 35 (sometimes up to 50) monsters of a creature
 * race or type that offers a challenge at the character's depth.  This is
 * similar to monster pits, except that we choose among a wider range of
 * monsters.
 *
 */
bool build_room_of_chambers(struct chunk *c, int y0, int x0)
{
	int i, d;
	int area, num_chambers;
	int y, x, y1, x1, y2, x2;

	int height, width, count;

	char name[40];

	/* Deeper in the dungeon, chambers are less likely to be lit. */
	bool light = (randint0(45) > c->depth) ? TRUE : FALSE;

	/* Calculate a level-dependent room size. */
	height = 20 + m_bonus(20, c->depth);
	width = 20 + randint1(20) + m_bonus(20, c->depth);

	/* Find and reserve some space in the dungeon.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height, width))
			return (FALSE);
	}

	/* Calculate the borders of the room. */
	y1 = y0 - (height / 2);
	x1 = x0 - (width / 2);
	y2 = y0 + (height - 1) / 2;
	x2 = x0 + (width - 1) / 2;

	/* Make certain the room does not cross the dungeon edge. */
	if ((!square_in_bounds(c, y1, x1)) || (!square_in_bounds(c, y2, x2)))
		return (FALSE);

	/* Determine how much space we have. */
	area = ABS(y2 - y1) * ABS(x2 - x1);

	/* Calculate the number of smaller chambers to make. */
	num_chambers = 10 + area / 80;

	/* Build the chambers. */
	for (i = 0; i < num_chambers; i++) {
		int c_y1, c_x1, c_y2, c_x2;
		int size, width, height;

		/* Determine size of chamber. */
		size = 3 + randint0(4);
		width = size + randint0(10);
		height = size + randint0(4);

		/* Pick an upper-left corner at random. */
		c_y1 = y1 + randint0(1 + y2 - y1 - height);
		c_x1 = x1 + randint0(1 + x2 - x1 - width);

		/* Determine lower-right corner of chamber. */
		c_y2 = c_y1 + height;
		if (c_y2 > y2) c_y2 = y2;

		c_x2 = c_x1 + width;
		if (c_x2 > x2) c_x2 = x2;

		/* Make me a (magma filled) chamber. */
		make_chamber(c, c_y1, c_x1, c_y2, c_x2);
	}

	/* Remove useless doors, fill in tiny, narrow rooms. */
	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			count = 0;

			/* Stay legal. */
			if (!square_in_bounds_fully(c, y, x))
				continue;

			/* Check all adjacent grids. */
			for (d = 0; d < 8; d++) {
				/* Extract adjacent location */
				int yy = y + ddy_ddd[d];
				int xx = x + ddx_ddd[d];

				/* Count the walls and dungeon granite. */
				if ((c->squares[yy][xx].feat == FEAT_GRANITE) &&
					(!square_iswall_outer(c, yy, xx)) &&
					(!square_iswall_solid(c, yy, xx)))
					count++;
			}

			/* Five adjacent walls: Change non-chamber to wall. */
			if ((count == 5) && (c->squares[y][x].feat != FEAT_MAGMA))
				set_marked_granite(c, y, x, SQUARE_WALL_INNER);

			/* More than five adjacent walls: Change anything to wall. */
			else if (count > 5)
				set_marked_granite(c, y, x, SQUARE_WALL_INNER);
		}
	}

	/* Pick a random magma spot near the center of the room. */
	for (i = 0; i < 50; i++) {
		y = y1 + ABS(y2 - y1) / 4 + randint0(ABS(y2 - y1) / 2);
		x = x1 + ABS(x2 - x1) / 4 + randint0(ABS(x2 - x1) / 2);
		if (c->squares[y][x].feat == FEAT_MAGMA)
			break;
	}

	/* Hollow out the first room. */
	square_set_feat(c, y, x, FEAT_FLOOR);
	hollow_out_room(c, y, x);

	/* Attempt to change every in-room magma grid to open floor. */
	for (i = 0; i < 100; i++) {
		/* Assume this run will do no useful work. */
		bool joy = FALSE;

		/* Make new doors and tunnels between magma and open floor. */
		for (y = y1; y < y2; y++) {
			for (x = x1; x < x2; x++) {
				/* Current grid must be magma. */
				if (c->squares[y][x].feat != FEAT_MAGMA) continue;

				/* Stay legal. */
				if (!square_in_bounds_fully(c, y, x)) continue;

				/* Check only horizontal and vertical directions. */
				for (d = 0; d < 4; d++) {
					int yy1, xx1, yy2, xx2, yy3, xx3;

					/* Extract adjacent location */
					yy1 = y + ddy_ddd[d];
					xx1 = x + ddx_ddd[d];

					/* Need inner wall. */
					if (!square_iswall_inner(c, yy1, xx1)) 
						continue;

					/* Keep going in the same direction, if in bounds. */
					yy2 = yy1 + ddy_ddd[d];
					xx2 = xx1 + ddx_ddd[d];
					if (!square_in_bounds(c, yy2, xx2)) continue;

					/* If we find open floor, place a door. */
					if (c->squares[yy2][xx2].feat == FEAT_FLOOR) {
						joy = TRUE;

						/* Make a broken door in the wall grid. */
						square_set_feat(c, yy1, xx1, FEAT_BROKEN);

						/* Hollow out the new room. */
						square_set_feat(c, y, x, FEAT_FLOOR);
						hollow_out_room(c, y, x);

						break;
					}

					/* If we find more inner wall... */
					if (square_iswall_inner(c, yy2, xx2)) {
						/* ...Keep going in the same direction. */
						yy3 = yy2 + ddy_ddd[d];
						xx3 = xx2 + ddx_ddd[d];
						if (!square_in_bounds(c, yy3, xx3)) continue;

						/* If we /now/ find floor, make a tunnel. */
						if (c->squares[yy3][xx3].feat == FEAT_FLOOR) {
							joy = TRUE;

							/* Turn both wall grids into floor. */
							square_set_feat(c, yy1, xx1, FEAT_FLOOR);
							square_set_feat(c, yy2, xx2, FEAT_FLOOR);

							/* Hollow out the new room. */
							square_set_feat(c, y, x, FEAT_FLOOR);
							hollow_out_room(c, y, x);

							break;
						}
					}
				}
			}
		}

		/* If we could find no work to do, stop. */
		if (!joy) break;
	}


	/* Turn broken doors into a random kind of door, remove open doors. */
	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			if (c->squares[y][x].feat == FEAT_OPEN)
				set_marked_granite(c, y, x, SQUARE_WALL_INNER);
			else if (c->squares[y][x].feat == FEAT_BROKEN)
				place_random_door(c, y, x);
		}
	}


	/* Turn all walls and magma not adjacent to floor into dungeon granite. */
	/* Turn all floors and adjacent grids into rooms, sometimes lighting them */
	for (y = (y1 - 1 > 0 ? y1 - 1 : 0);
		 y < (y2 + 2 < c->height ? y2 + 2 : c->height); y++) {
		for (x = (x1 - 1 > 0 ? x1 - 1 : 0);
			 x < (x2 + 2 < c->width ? x2 + 2 : c->width); x++) {
			if (square_iswall_inner(c, y, x)
				|| (c->squares[y][x].feat == FEAT_MAGMA)) {
				for (d = 0; d < 9; d++) {
					/* Extract adjacent location */
					int yy = y + ddy_ddd[d];
					int xx = x + ddx_ddd[d];

					/* Stay legal */
					if (!square_in_bounds(c, yy, xx)) continue;

					/* No floors allowed */
					if (c->squares[yy][xx].feat == FEAT_FLOOR) break;

					/* Turn me into dungeon granite. */
					if (d == 8)
						set_marked_granite(c, y, x, SQUARE_NONE);
				}
			}
			if (tf_has(f_info[c->squares[y][x].feat].flags, TF_FLOOR)) {
				for (d = 0; d < 9; d++) {
					/* Extract adjacent location */
					int yy = y + ddy_ddd[d];
					int xx = x + ddx_ddd[d];

					/* Stay legal */
					if (!square_in_bounds(c, yy, xx)) continue;

					/* Turn into room. */
					sqinfo_on(c->squares[yy][xx].info, SQUARE_ROOM);

					/* Illuminate if requested. */
					if (light) sqinfo_on(c->squares[yy][xx].info, SQUARE_GLOW);
				}
			}
		}
	}


	/* Turn all inner wall grids adjacent to dungeon granite into outer walls */
	for (y = (y1 - 1 > 0 ? y1 - 1 : 0);
		 y < (y2 + 2 < c->height ? y2 + 2 : c->height); y++) {
		for (x = (x1 - 1 > 0 ? x1 - 1 : 0);
			 x < (x2 + 2 < c->width ? x2 + 2 : c->width); x++) {
			/* Stay legal. */
			if (!square_in_bounds_fully(c, y, x)) continue;

			if (square_iswall_inner(c, y, x)) {
				for (d = 0; d < 9; d++) {
					/* Extract adjacent location */
					int yy = y + ddy_ddd[d];
					int xx = x + ddx_ddd[d];

					/* Look for dungeon granite */
					if ((c->squares[yy][xx].feat == FEAT_GRANITE) && 
						(!square_iswall_inner(c, y, x)) &&
						(!square_iswall_outer(c, y, x)) &&
						(!square_iswall_solid(c, y, x)))
					{
						/* Turn me into outer wall. */
						set_marked_granite(c, y, x, SQUARE_WALL_OUTER);

						/* Done; */
						break;
					}
				}
			}
		}
	}

	/*** Now we get to place the monsters. ***/
	get_chamber_monsters(c, y1, x1, y2, x2, name, height * width);

	/* Increase the level rating */
	c->mon_rating += 10;

	/* Describe */
	ROOM_LOG("Room of chambers (%s)", strlen(name) ? name : "empty");

	/* Success. */
	return (TRUE);
}

/**
 * A single starburst-shaped room of extreme size, usually dotted or
 * even divided with irregularly-shaped fields of rubble. No special
 * monsters.  Appears deeper than level 40.
 * \param c the chunk the room is being built in
 * \param y0
 * \param x0 co-ordinates of the centre; out of chunk bounds invoke find_space()
 * \return success
 *
 * These are the largest, most difficult to position, and thus highest-
 * priority rooms in the dungeon.  They should be rare, so as not to
 * interfere with greater vaults.
 */
bool build_huge(struct chunk *c, int y0, int x0)
{
	bool light;

	int i, count;

	int y1, x1, y2, x2;
	int y1_tmp, x1_tmp, y2_tmp, x2_tmp;
	int width_tmp, height_tmp;

	int height = 30 + randint0(10);
	int width = 45 + randint0(50);

	/* Only try to build a huge room as the first room. */
	if (dun->cent_n > 0) return FALSE;

	/* Flat 5% chance */
	if (!one_in_(20)) return FALSE;

	/* This room is usually lit. */
	light = !one_in_(3);

	/* Find and reserve some space.  Get center of room. */
	if ((y0 >= c->height) || (x0 >= c->width)) {
		if (!find_space(&y0, &x0, height, width))
			return (FALSE);
	}

	/* Locate the room */
	y1 = y0 - height / 2;
	x1 = x0 - width / 2;
	y2 = y1 + height - 1;
	x2 = x1 + width - 1;

	/* Make a huge starburst room with optional light. */
	if (!generate_starburst_room(c, y1, x1, y2, x2, light, FEAT_FLOOR, FALSE))
		return (FALSE);


	/* Often, add rubble to break things up a bit. */
	if (randint1(5) > 2) {
		/* Determine how many rubble fields to add (between 1 and 6). */
		count = height * width * randint1(2) / 1100;

		/* Make the rubble fields. */
		for (i = 0; i < count; i++) {
			height_tmp = 8 + randint0(16);
			width_tmp = 10 + randint0(24);

			/* Semi-random location. */
			y1_tmp = y1 + randint0(height - height_tmp);
			x1_tmp = x1 + randint0(width - width_tmp);
			y2_tmp = y1_tmp + height_tmp;
			x2_tmp = x1_tmp + width_tmp;

			/* Make the rubble field. */
			generate_starburst_room(c, y1_tmp, x1_tmp, y2_tmp, x2_tmp,
									FALSE, FEAT_RUBBLE, FALSE);
		}
	}

	/* Describe */
	ROOM_LOG("Huge room");

	/* Success. */
	return (TRUE);
}

/**
 * Attempt to build a room of the given type at the given block
 *
 * \param c the chunk the room is being built in
 * \param by0
 * \param bx0 block co-ordinates of the top left block
 * \param profile the profile of the rooom we're trying to build
 * \param finds_own_space whether we are allowing the room to place itself
 * \return success
 *
 * Note that this code assumes that profile height and width are the maximum
 * possible grid sizes, and then allocates a number of blocks that will always
 * contain them.
 *
 * Note that we restrict the number of pits/nests to reduce
 * the chance of overflowing the monster list during level creation.
 */
bool room_build(struct chunk *c, int by0, int bx0, struct room_profile profile,
	bool finds_own_space)
{
	/* Extract blocks */
	int by1 = by0;
	int bx1 = bx0;
	int by2 = by0 + profile.height / dun->block_hgt;
	int bx2 = bx0 + profile.width / dun->block_wid;

	int y, x;
	int by, bx;

	/* Enforce the room profile's minimum depth */
	if (c->depth < profile.level) return FALSE;

	/* Only allow at most two pit/nests room per level */
	if ((dun->pit_num >= z_info->level_pit_max) && (profile.pit)) return FALSE;

	/* Expand the number of blocks if we might overflow */
	if (profile.height % dun->block_hgt) by2++;
	if (profile.width % dun->block_wid) bx2++;

	/* Does the profile allocate space, or the room find it? */
	if (finds_own_space) {
		/* Try to build a room, pass silly place so room finds its own */
		if (!profile.builder(c, c->height, c->width))
			return FALSE;
	} else {
		/* Never run off the screen */
		if (by1 < 0 || by2 >= dun->row_blocks) return FALSE;
		if (bx1 < 0 || bx2 >= dun->col_blocks) return FALSE;

		/* Verify open space */
		for (by = by1; by <= by2; by++) {
			for (bx = bx1; bx <= bx2; bx++) {
				/* previous rooms prevent new ones */
				if (dun->room_map[by][bx]) return FALSE;
			}
		}

		/* Get the location of the room */
		y = ((by1 + by2 + 1) * dun->block_hgt) / 2;
		x = ((bx1 + bx2 + 1) * dun->block_wid) / 2;

		/* Try to build a room */
		if (!profile.builder(c, y, x)) return FALSE;

		/* Save the room location */
		if (dun->cent_n < z_info->level_room_max) {
			dun->cent[dun->cent_n].y = y;
			dun->cent[dun->cent_n].x = x;
			dun->cent_n++;
		}

		/* Reserve some blocks */
		for (by = by1; by < by2; by++) {
			for (bx = bx1; bx < bx2; bx++) {
				dun->room_map[by][bx] = TRUE;
			}
		}
	}

	/* Count pit/nests rooms */
	if (profile.pit) dun->pit_num++;

	/* Success */
	return TRUE;
}
