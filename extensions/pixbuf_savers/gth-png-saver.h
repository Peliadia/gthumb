/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2009 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#ifndef GTH_PNG_SAVER_H
#define GTH_PNG_SAVER_H

#include <gtk/gtk.h>
#include <gthumb.h>

G_BEGIN_DECLS

#define GTH_TYPE_PNG_SAVER              (gth_png_saver_get_type ())
#define GTH_PNG_SAVER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTH_TYPE_PNG_SAVER, GthPngSaver))
#define GTH_PNG_SAVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GTH_PNG_SAVER_TYPE, GthPngSaverClass))
#define GTH_IS_PNG_SAVER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTH_TYPE_PNG_SAVER))
#define GTH_IS_PNG_SAVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GTH_TYPE_PNG_SAVER))
#define GTH_PNG_SAVER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj), GTH_TYPE_PNG_SAVER, GthPngSaverClass))

typedef struct _GthPngSaver         GthPngSaver;
typedef struct _GthPngSaverClass    GthPngSaverClass;
typedef struct _GthPngSaverPrivate  GthPngSaverPrivate;

struct _GthPngSaver
{
	GthPixbufSaver __parent;
	GthPngSaverPrivate *priv;
};

struct _GthPngSaverClass
{
	GthPixbufSaverClass __parent_class;
};

GType  gth_png_saver_get_type  (void);

G_END_DECLS

#endif /* GTH_PNG_SAVER_H */
