/**
 * \file guid.h
 * \brief Entity guids.
 *
 * Copyright (c) 2011 elly+angband@leptoquark.net
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
 */

#ifndef GUID_H
#define GUID_H

typedef unsigned int guid;

extern int guid_eq(guid a, guid b);

#endif /* !GUID_H */
