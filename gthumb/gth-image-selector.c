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

#include <config.h>
#include <stdlib.h>
#include <math.h>
#include "glib-utils.h"
#include "gth-cursors.h"
#include "gth-image-selector.h"
#include "gth-marshal.h"


#define BORDER         3
#define BORDER2        (BORDER * 2)
#define DRAG_THRESHOLD 1
#define STEP_INCREMENT 20.0  /* scroll increment. */
#define SCROLL_TIMEOUT 30    /* autoscroll timeout in milliseconds */
#define GOLDEN_RATIO   1.6180339887


typedef struct {
	int           ref_count;
	int           id;
	GdkRectangle  area;
	GdkCursor    *cursor;
} EventArea;


static EventArea *
event_area_new (int           id,
		GdkCursorType cursor_type)
{
	EventArea *event_area;

	event_area = g_new0 (EventArea, 1);

	event_area->ref_count = 1;
	event_area->id = id;
	event_area->area.x = 0;
	event_area->area.y = 0;
	event_area->area.width = 0;
	event_area->area.height = 0;
	event_area->cursor = gdk_cursor_new_for_display (gdk_display_get_default (), cursor_type);

	return event_area;
}


G_GNUC_UNUSED
static void
event_area_ref (EventArea *event_area)
{
	event_area->ref_count++;
}


static void
event_area_unref (EventArea *event_area)
{
	event_area->ref_count--;

	if (event_area->ref_count > 0)
		return;

	if (event_area->cursor != NULL)
		gdk_cursor_unref (event_area->cursor);
	g_free (event_area);
}


/**/


typedef enum {
	C_SELECTION_AREA,
	C_TOP_AREA,
	C_BOTTOM_AREA,
	C_LEFT_AREA,
	C_RIGHT_AREA,
	C_TOP_LEFT_AREA,
	C_TOP_RIGHT_AREA,
	C_BOTTOM_LEFT_AREA,
	C_BOTTOM_RIGHT_AREA
} GthEventAreaType;


static GthEventAreaType
get_opposite_event_area_on_x (GthEventAreaType type)
{
	GthEventAreaType opposite_type = C_SELECTION_AREA;
	switch (type) {
		case C_SELECTION_AREA:
			opposite_type = C_SELECTION_AREA;
			break;
		case C_TOP_AREA:
			opposite_type = C_TOP_AREA;
			break;
		case C_BOTTOM_AREA:
			opposite_type = C_BOTTOM_AREA;
			break;
		case C_LEFT_AREA:
			opposite_type = C_RIGHT_AREA;
			break;
		case C_RIGHT_AREA:
			opposite_type = C_LEFT_AREA;
			break;
		case C_TOP_LEFT_AREA:
			opposite_type = C_TOP_RIGHT_AREA;
			break;
		case C_TOP_RIGHT_AREA:
			opposite_type = C_TOP_LEFT_AREA;
			break;
		case C_BOTTOM_LEFT_AREA:
			opposite_type = C_BOTTOM_RIGHT_AREA;
			break;
		case C_BOTTOM_RIGHT_AREA:
			opposite_type = C_BOTTOM_LEFT_AREA;
			break;
	}
	return opposite_type;
}


static GthEventAreaType
get_opposite_event_area_on_y (GthEventAreaType type)
{
	GthEventAreaType opposite_type = C_SELECTION_AREA;
	switch (type) {
		case C_SELECTION_AREA:
			opposite_type = C_SELECTION_AREA;
			break;
		case C_TOP_AREA:
			opposite_type = C_BOTTOM_AREA;
			break;
		case C_BOTTOM_AREA:
			opposite_type = C_TOP_AREA;
			break;
		case C_LEFT_AREA:
			opposite_type = C_LEFT_AREA;
			break;
		case C_RIGHT_AREA:
			opposite_type = C_RIGHT_AREA;
			break;
		case C_TOP_LEFT_AREA:
			opposite_type = C_BOTTOM_LEFT_AREA;
			break;
		case C_TOP_RIGHT_AREA:
			opposite_type = C_BOTTOM_RIGHT_AREA;
			break;
		case C_BOTTOM_LEFT_AREA:
			opposite_type = C_TOP_LEFT_AREA;
			break;
		case C_BOTTOM_RIGHT_AREA:
			opposite_type = C_TOP_RIGHT_AREA;
			break;
	}
	return opposite_type;
}


enum {
	SELECTION_CHANGED,
	SELECTED,
	MOTION_NOTIFY,
	MASK_VISIBILITY_CHANGED,
	GRID_VISIBILITY_CHANGED,
	LAST_SIGNAL
};


static guint signals[LAST_SIGNAL] = { 0 };
static gpointer parent_class = NULL;


struct _GthImageSelectorPrivate {
	GthImageViewer  *viewer;
	GthSelectorType  type;

	GdkPixbuf       *pixbuf;
	GdkPixbuf       *background;
	GdkRectangle     pixbuf_area;

	gboolean         use_ratio;
	double           ratio;
	gboolean         mask_visible;
	GthGridType      grid_type;
	gboolean         active;

	GdkRectangle     drag_start_selection_area;
	GdkGC           *selection_gc;
	GdkRectangle     selection_area;
	GdkRectangle     selection;

	GdkCursor       *default_cursor;
	GdkCursor       *current_cursor;
	GList           *event_list;
	EventArea       *current_area;

	guint            timer_id; 	    /* Timeout ID for
					     * autoscrolling */
	double           x_value_diff;      /* Change the adjustment value
					     * by this
					     * amount when autoscrolling */
	double           y_value_diff;
};


static gboolean
point_in_rectangle (int          x,
		    int          y,
		    GdkRectangle rect)
{
	return ((x >= rect.x)
		&& (x <= rect.x + rect.width)
		&& (y >= rect.y)
		&& (y <= rect.y + rect.height));
}


static gboolean
rectangle_in_rectangle (GdkRectangle r1,
			GdkRectangle r2)
{
	return (point_in_rectangle (r1.x, r1.y, r2)
		&& point_in_rectangle (r1.x + r1.width,
				       r1.y + r1.height,
				       r2));
}


static gboolean
rectangle_equal (GdkRectangle r1,
		 GdkRectangle r2)
{
	return ((r1.x == r2.x)
		&& (r1.y == r2.y)
		&& (r1.width == r2.width)
		&& (r1.height == r2.height));
}


static int
real_to_selector (GthImageSelector *self,
		  int               value)
{
	return IROUND (gth_image_viewer_get_zoom (self->priv->viewer) * value);
}


static void
convert_to_selection_area (GthImageSelector *self,
			   GdkRectangle      real_area,
			   GdkRectangle     *selection_area)
{
	selection_area->x = real_to_selector (self, real_area.x);
	selection_area->y = real_to_selector (self, real_area.y);
	selection_area->width = real_to_selector (self, real_area.width);
	selection_area->height = real_to_selector (self, real_area.height);
}


static void
add_event_area (GthImageSelector *self,
		int               area_id,
		GdkCursorType     cursor_type)
{
	EventArea *event_area;

	event_area = event_area_new (area_id, cursor_type);
	self->priv->event_list = g_list_prepend (self->priv->event_list, event_area);
}


static void
free_event_area_list (GthImageSelector *self)
{
	if (self->priv->event_list != NULL) {
		g_list_foreach (self->priv->event_list, (GFunc) event_area_unref, NULL);
		g_list_free (self->priv->event_list);
		self->priv->event_list = NULL;
	}
}


static EventArea *
get_event_area_from_position (GthImageSelector *self,
			      int               x,
			      int               y)
{
	GList *scan;

	for (scan = self->priv->event_list; scan; scan = scan->next) {
		EventArea    *event_area = scan->data;
		GdkRectangle  widget_area;

		widget_area = event_area->area;
		widget_area.x += self->priv->viewer->image_area.x;
		widget_area.y += self->priv->viewer->image_area.y;

		if (point_in_rectangle (x, y, widget_area))
			return event_area;
	}

	return NULL;
}


static EventArea *
get_event_area_from_id (GthImageSelector *self,
			int               event_id)
{
	GList *scan;

	for (scan = self->priv->event_list; scan; scan = scan->next) {
		EventArea *event_area = scan->data;
		if (event_area->id == event_id)
			return event_area;
	}

	return NULL;
}


/**/


static void
update_event_areas (GthImageSelector *self)
{
	EventArea *event_area;
	int        x, y, width, height;

	if (! GTK_WIDGET_REALIZED (self->priv->viewer))
		return;

	x = self->priv->selection_area.x - 1;
	y = self->priv->selection_area.y - 1;
	width = self->priv->selection_area.width + 1;
	height = self->priv->selection_area.height + 1;

	event_area = get_event_area_from_id (self, C_SELECTION_AREA);
	event_area->area.x = x + BORDER;
	event_area->area.y = y + BORDER;
	event_area->area.width = width - BORDER2;
	event_area->area.height = height - BORDER2;

	event_area = get_event_area_from_id (self, C_TOP_AREA);
	event_area->area.x = x + BORDER;
	event_area->area.y = y - BORDER;
	event_area->area.width = width - BORDER2;
	event_area->area.height = BORDER2;

	event_area = get_event_area_from_id (self, C_BOTTOM_AREA);
	event_area->area.x = x + BORDER;
	event_area->area.y = y + height - BORDER;
	event_area->area.width = width - BORDER2;
	event_area->area.height = BORDER2;

	event_area = get_event_area_from_id (self, C_LEFT_AREA);
	event_area->area.x = x - BORDER;
	event_area->area.y = y + BORDER;
	event_area->area.width = BORDER2;
	event_area->area.height = height - BORDER2;

	event_area = get_event_area_from_id (self, C_RIGHT_AREA);
	event_area->area.x = x + width - BORDER;
	event_area->area.y = y + BORDER;
	event_area->area.width = BORDER2;
	event_area->area.height = height - BORDER2;

	event_area = get_event_area_from_id (self, C_TOP_LEFT_AREA);
	event_area->area.x = x - BORDER;
	event_area->area.y = y - BORDER;
	event_area->area.width = BORDER2;
	event_area->area.height = BORDER2;

	event_area = get_event_area_from_id (self, C_TOP_RIGHT_AREA);
	event_area->area.x = x + width - BORDER;
	event_area->area.y = y - BORDER;
	event_area->area.width = BORDER2;
	event_area->area.height = BORDER2;

	event_area = get_event_area_from_id (self, C_BOTTOM_LEFT_AREA);
	event_area->area.x = x - BORDER;
	event_area->area.y = y + height - BORDER;
	event_area->area.width = BORDER2;
	event_area->area.height = BORDER2;

	event_area = get_event_area_from_id (self, C_BOTTOM_RIGHT_AREA);
	event_area->area.x = x + width - BORDER;
	event_area->area.y = y + height - BORDER;
	event_area->area.width = BORDER2;
	event_area->area.height = BORDER2;
}


static void
queue_draw (GthImageSelector *self,
	    GdkRectangle      area)
{
	if (! GTK_WIDGET_REALIZED (self->priv->viewer))
		return;

	gtk_widget_queue_draw_area (GTK_WIDGET (self->priv->viewer),
				    self->priv->viewer->image_area.x + area.x - self->priv->viewer->x_offset - BORDER,
				    self->priv->viewer->image_area.y + area.y - self->priv->viewer->y_offset - BORDER,
				    area.width + BORDER2,
				    area.height + BORDER2);
}


static void
selection_changed (GthImageSelector *self)
{
	update_event_areas (self);
	g_signal_emit (G_OBJECT (self), signals[SELECTION_CHANGED], 0);
}


static void
set_selection_area (GthImageSelector *self,
		    GdkRectangle      new_selection,
		    gboolean          force_update)
{
	GdkRectangle old_selection_area;
	GdkRectangle dirty_region;

	if (! force_update && rectangle_equal (self->priv->selection_area, new_selection))
		return;

	old_selection_area = self->priv->selection_area;
	self->priv->selection_area = new_selection;
	gdk_rectangle_union (&old_selection_area, &self->priv->selection_area, &dirty_region);
	queue_draw (self, dirty_region);

	selection_changed (self);
}


static void
set_selection (GthImageSelector *self,
	       GdkRectangle      new_selection,
	       gboolean          force_update)
{
	GdkRectangle new_area;

	if (! force_update && rectangle_equal (self->priv->selection, new_selection))
		return;

	self->priv->selection = new_selection;
	convert_to_selection_area (self, new_selection, &new_area);
	set_selection_area (self, new_area, force_update);
}


static void
init_selection (GthImageSelector *self)
{
	GdkRectangle area;

	/*
	if (! self->priv->use_ratio) {
		area.width = IROUND (self->priv->pixbuf_area.width * 0.5);
		area.height = IROUND (self->priv->pixbuf_area.height * 0.5);
	}
	else {
		if (self->priv->ratio > 1.0) {
			area.width = IROUND (self->priv->pixbuf_area.width * 0.5);
			area.height = IROUND (area.width / self->priv->ratio);
		}
		else {
			area.height = IROUND (self->priv->pixbuf_area.height * 0.5);
			area.width = IROUND (area.height * self->priv->ratio);
		}
	}
	area.x = IROUND ((self->priv->pixbuf_area.width - area.width) / 2.0);
	area.y = IROUND ((self->priv->pixbuf_area.height - area.height) / 2.0);
	*/

	area.x = 0;
	area.y = 0;
	area.height = 0;
	area.width = 0;
	set_selection (self, area, FALSE);
}


static void
gth_image_selector_realize (GthImageViewerTool *base)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);
	GtkWidget        *widget;

	widget = (GtkWidget *) self->priv->viewer;

	self->priv->selection_gc = gdk_gc_new (widget->window);
	gdk_gc_copy (self->priv->selection_gc, widget->style->white_gc);
	gdk_gc_set_line_attributes (self->priv->selection_gc,
				    1,
				    GDK_LINE_ON_OFF_DASH /*GDK_LINE_SOLID*/,
				    GDK_CAP_BUTT,
				    GDK_JOIN_MITER);
	gdk_gc_set_function (self->priv->selection_gc, GDK_INVERT);

	if (self->priv->type == GTH_SELECTOR_TYPE_REGION)
		self->priv->default_cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_CROSSHAIR /*GDK_LEFT_PTR*/);
	else if (self->priv->type == GTH_SELECTOR_TYPE_POINT)
		self->priv->default_cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_CROSSHAIR);
	gth_image_viewer_set_cursor (self->priv->viewer, self->priv->default_cursor);

	add_event_area (self, C_SELECTION_AREA, GDK_FLEUR);
	add_event_area (self, C_TOP_AREA, GDK_TOP_SIDE);
	add_event_area (self, C_BOTTOM_AREA, GDK_BOTTOM_SIDE);
	add_event_area (self, C_LEFT_AREA, GDK_LEFT_SIDE);
	add_event_area (self, C_RIGHT_AREA, GDK_RIGHT_SIDE);
	add_event_area (self, C_TOP_LEFT_AREA, GDK_TOP_LEFT_CORNER);
	add_event_area (self, C_TOP_RIGHT_AREA, GDK_TOP_RIGHT_CORNER);
	add_event_area (self, C_BOTTOM_LEFT_AREA, GDK_BOTTOM_LEFT_CORNER);
	add_event_area (self, C_BOTTOM_RIGHT_AREA, GDK_BOTTOM_RIGHT_CORNER);

	init_selection (self);
	update_event_areas (self);
}


static void
gth_image_selector_unrealize (GthImageViewerTool *base)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);

	if (self->priv->default_cursor != NULL) {
		gdk_cursor_unref (self->priv->default_cursor);
		self->priv->default_cursor = NULL;
	}

	if (self->priv->selection_gc != NULL) {
		g_object_unref (self->priv->selection_gc);
		self->priv->selection_gc = NULL;
	}

	free_event_area_list (self);
}


static void
gth_image_selector_size_allocate (GthImageViewerTool *base,
				  GtkAllocation      *allocation)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);

	if (self->priv->pixbuf != NULL)
		selection_changed (self);
}


static void
gth_image_selector_map (GthImageViewerTool *base)
{
	/* void */
}


static void
gth_image_selector_unmap (GthImageViewerTool *base)
{
	/* void */
}


G_GNUC_UNUSED
static void
print_rectangle (const char   *name,
		 GdkRectangle *r)
{
	g_print ("%s ==> (%d,%d) [%d,%d]\n", name, r->x, r->y, r->width, r->height);
}


static void
paint_background (GthImageSelector *self,
		  GdkRectangle     *event_area)
{
	GdkRectangle paint_area;

	if (! gdk_rectangle_intersect (&self->priv->viewer->image_area, event_area, &paint_area))
		return;

	gth_image_viewer_paint (self->priv->viewer,
				self->priv->background,
				self->priv->viewer->x_offset + paint_area.x - self->priv->viewer->image_area.x,
				self->priv->viewer->y_offset + paint_area.y - self->priv->viewer->image_area.y,
				paint_area.x,
				paint_area.y,
				paint_area.width,
				paint_area.height,
				GDK_INTERP_TILES);
}


static void
paint_selection (GthImageSelector *self,
		 GdkRectangle     *event_area)
{
	GdkRectangle selection_area, paint_area;

	selection_area = self->priv->selection_area;
	selection_area.x += self->priv->viewer->image_area.x - self->priv->viewer->x_offset;
	selection_area.y += self->priv->viewer->image_area.y - self->priv->viewer->y_offset;

	if (! gdk_rectangle_intersect (&selection_area, event_area, &paint_area))
		return;

	gth_image_viewer_paint (self->priv->viewer,
				self->priv->pixbuf,
				self->priv->viewer->x_offset + paint_area.x - self->priv->viewer->image_area.x,
				self->priv->viewer->y_offset + paint_area.y - self->priv->viewer->image_area.y,
				paint_area.x,
				paint_area.y,
				paint_area.width,
				paint_area.height,
				GDK_INTERP_TILES);

	if (self->priv->grid_type != GTH_GRID_NONE) {
		int grid_x0, grid_x1, grid_x2, grid_x3;
	        int grid_y0, grid_y1, grid_y2, grid_y3;
		int x_delta, y_delta;

		grid_x0 = paint_area.x;
		grid_x3 = paint_area.x + paint_area.width;

                grid_y0 = paint_area.y;
                grid_y3 = paint_area.y + paint_area.height;

		if (self->priv->grid_type == GTH_GRID_THIRDS) {
			x_delta = paint_area.width / 3;
			y_delta = paint_area.height /3;
		}
		else if (self->priv->grid_type == GTH_GRID_GOLDEN) {
			x_delta = paint_area.width * (GOLDEN_RATIO / (1.0 + 2.0 * GOLDEN_RATIO));
			y_delta = paint_area.height * (GOLDEN_RATIO / (1.0 + 2.0 * GOLDEN_RATIO));
		}

		grid_x1 = grid_x0 + x_delta;
		grid_x2 = grid_x3 - x_delta;
                grid_y1 = grid_y0 + y_delta;
                grid_y2 = grid_y3 - y_delta;

		gdk_draw_line (GTK_WIDGET (self->priv->viewer)->window,
			       self->priv->selection_gc,
			       grid_x1, grid_y0,
			       grid_x1, grid_y3);
	        gdk_draw_line (GTK_WIDGET (self->priv->viewer)->window,
       		               self->priv->selection_gc,
 			       grid_x2, grid_y0,
			       grid_x2, grid_y3);
        	gdk_draw_line (GTK_WIDGET (self->priv->viewer)->window,
       	        	       self->priv->selection_gc,
                               grid_x0, grid_y1,
                               grid_x3, grid_y1);
	        gdk_draw_line (GTK_WIDGET (self->priv->viewer)->window,
       		               self->priv->selection_gc,
                               grid_x0, grid_y2,
                               grid_x3, grid_y2);
	}
}


static void
paint_image (GthImageSelector *self,
	     GdkRectangle     *event_area)
{
	GdkRectangle paint_area;

	if (! gdk_rectangle_intersect (&self->priv->viewer->image_area, event_area, &paint_area))
		return;

	gth_image_viewer_paint (self->priv->viewer,
				self->priv->pixbuf,
				self->priv->viewer->x_offset + paint_area.x - self->priv->viewer->image_area.x,
				self->priv->viewer->y_offset + paint_area.y - self->priv->viewer->image_area.y,
				paint_area.x,
				paint_area.y,
				paint_area.width,
				paint_area.height,
				GDK_INTERP_TILES);
}


static void
gth_image_selector_expose (GthImageViewerTool *base,
			   GdkRectangle       *event_area)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);

	if (self->priv->pixbuf == NULL)
		return;

	if (! self->priv->mask_visible) {
		paint_image (self, event_area);
		return;
	}

	paint_background (self, event_area);
	paint_selection (self, event_area);

	if (GTK_WIDGET_HAS_FOCUS (self->priv->viewer)) {
		GdkRectangle area;

		area = self->priv->selection_area;
		area.x += self->priv->viewer->image_area.x - self->priv->viewer->x_offset;
		area.y += self->priv->viewer->image_area.y - self->priv->viewer->y_offset;

		gdk_draw_rectangle (GTK_WIDGET (self->priv->viewer)->window,
				    self->priv->selection_gc,
				    FALSE,
				    area.x,
				    area.y,
				    area.width,
				    area.height);
	}
}


static int
selector_to_real (GthImageSelector *self,
		  int               value)
{
	return IROUND ((double) value / gth_image_viewer_get_zoom (self->priv->viewer));
}


static void
convert_to_real_selection (GthImageSelector *self,
			   GdkRectangle      selection_area,
			   GdkRectangle     *real_area)
{
	real_area->x = selector_to_real (self, selection_area.x);
	real_area->y = selector_to_real (self, selection_area.y);
	real_area->width = selector_to_real (self, selection_area.width);
	real_area->height = selector_to_real (self, selection_area.height);
}


static void
set_active_area (GthImageSelector *self,
		 EventArea        *event_area)
{
	if (self->priv->active != (event_area != NULL)) {
		self->priv->active = ! self->priv->active;
		/* queue_draw (self, self->priv->selection_area); FIXME */
	}

	if (self->priv->current_area != event_area)
		self->priv->current_area = event_area;

	if (self->priv->current_area == NULL)
		gth_image_viewer_set_cursor (self->priv->viewer, self->priv->default_cursor);
	else
		gth_image_viewer_set_cursor (self->priv->viewer, self->priv->current_area->cursor);
}


static void
update_cursor (GthImageSelector *self,
	       int               x,
	       int               y)
{
	if (! self->priv->mask_visible)
		return;
	set_active_area (self, get_event_area_from_position (self, x, y));
}


static gboolean
gth_image_selector_button_release (GthImageViewerTool *base,
				   GdkEventButton     *event)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);

	if (self->priv->timer_id != 0) {
		g_source_remove (self->priv->timer_id);
		self->priv->timer_id = 0;
	}

	update_cursor (self,
		       event->x + self->priv->viewer->x_offset,
		       event->y + self->priv->viewer->y_offset);

	return FALSE;
}


static void
grow_upward (GdkRectangle *bound,
	     GdkRectangle *r,
	     int           dy,
	     gboolean      check)
{
	if (check && (r->y + dy < 0))
		dy = -r->y;
	r->y += dy;
	r->height -= dy;
}


static void
grow_downward (GdkRectangle *bound,
	       GdkRectangle *r,
	       int           dy,
	       gboolean      check)
{
	if (check && (r->y + r->height + dy > bound->height))
		dy = bound->height - (r->y + r->height);
	r->height += dy;
}


static void
grow_leftward (GdkRectangle *bound,
	       GdkRectangle *r,
	       int           dx,
	       gboolean      check)
{
	if (check && (r->x + dx < 0))
		dx = -r->x;
	r->x += dx;
	r->width -= dx;
}


static void
grow_rightward (GdkRectangle *bound,
		GdkRectangle *r,
		int           dx,
		gboolean      check)
{
	if (check && (r->x + r->width + dx > bound->width))
		dx = bound->width - (r->x + r->width);
	r->width += dx;
}


static int
get_semiplane_no (int x1,
		  int y1,
		  int x2,
		  int y2,
		  int px,
		  int py)
{
	double a, b;

	a = atan ((double) (y1 - y2) / (x2 - x1));
	b = atan ((double) (y1 - py) / (px - x1));

	return (a <= b) && (b <= a + G_PI);
}


static void
check_and_set_new_selection (GthImageSelector *self,
			     GdkRectangle      new_selection)
{
	new_selection.width = MAX (0, new_selection.width);
	new_selection.height = MAX (0, new_selection.height);

	if (((self->priv->current_area == NULL) || (self->priv->current_area->id != C_SELECTION_AREA))
	    && self->priv->use_ratio)
	{
		if (rectangle_in_rectangle (new_selection, self->priv->pixbuf_area))
			set_selection (self, new_selection, FALSE);
		return;
	}

	if (new_selection.x < 0)
		new_selection.x = 0;
	if (new_selection.y < 0)
		new_selection.y = 0;
	if (new_selection.width > self->priv->pixbuf_area.width)
		new_selection.width = self->priv->pixbuf_area.width;
	if (new_selection.height > self->priv->pixbuf_area.height)
		new_selection.height = self->priv->pixbuf_area.height;

	if (new_selection.x + new_selection.width > self->priv->pixbuf_area.width)
		new_selection.x = self->priv->pixbuf_area.width - new_selection.width;
	if (new_selection.y + new_selection.height > self->priv->pixbuf_area.height)
		new_selection.y = self->priv->pixbuf_area.height - new_selection.height;

	set_selection (self, new_selection, FALSE);
}


static gboolean
gth_image_selector_button_press (GthImageViewerTool *base,
				 GdkEventButton     *event)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);
	gboolean          retval = FALSE;
	int               x, y;

	if (event->button != 1)
		return FALSE;

	if (! point_in_rectangle (event->x, event->y, self->priv->viewer->image_area))
		return FALSE;

	x = event->x + self->priv->viewer->x_offset;
	y = event->y + self->priv->viewer->y_offset;

	if (self->priv->current_area == NULL) {
		GdkRectangle new_selection;

		new_selection.x = selector_to_real (self, x - self->priv->viewer->image_area.x);
		new_selection.y = selector_to_real (self, y - self->priv->viewer->image_area.y);
		new_selection.width = selector_to_real (self, 1);
		new_selection.height = selector_to_real (self, 1);

		if (self->priv->type == GTH_SELECTOR_TYPE_REGION) {
			check_and_set_new_selection (self, new_selection);
			set_active_area (self, get_event_area_from_id (self, C_BOTTOM_RIGHT_AREA));
		}
		else if (self->priv->type == GTH_SELECTOR_TYPE_POINT) {
			retval = TRUE;
			g_signal_emit (G_OBJECT (self),
				       signals[SELECTED],
				       0,
				       new_selection.x,
				       new_selection.y);
		}
	}

	if (self->priv->current_area != NULL) {
		self->priv->viewer->pressed = TRUE;
		self->priv->viewer->dragging = TRUE;
		self->priv->drag_start_selection_area = self->priv->selection_area;
		retval = TRUE;
	}

	return retval;
}


static void
update_mouse_selection (GthImageSelector *self,
			int               new_x,
			int               new_y)
{
	gboolean          check = ! self->priv->use_ratio;
	int               dx, dy;
	GdkRectangle      new_selection, tmp;
	int               semiplane;
	GthEventAreaType  area_type = self->priv->current_area->id;
	EventArea        *event_area;

	dx = selector_to_real (self, self->priv->viewer->drag_x - self->priv->viewer->drag_x_start);
	dy = selector_to_real (self, self->priv->viewer->drag_y - self->priv->viewer->drag_y_start);

	convert_to_real_selection (self,
				   self->priv->drag_start_selection_area,
				   &new_selection);

	if (((area_type == C_LEFT_AREA)
	     || (area_type == C_TOP_LEFT_AREA)
	     || (area_type == C_BOTTOM_LEFT_AREA))
	    && (dx > new_selection.width))
	{
		new_selection.x += new_selection.width;
       		dx = - (2 * new_selection.width) + dx;
       		area_type = get_opposite_event_area_on_x (area_type);
	}
	else if (((area_type == C_RIGHT_AREA)
		  || (area_type == C_TOP_RIGHT_AREA)
		  || (area_type == C_BOTTOM_RIGHT_AREA))
		 && (-dx > new_selection.width))
	{
	    	new_selection.x -= new_selection.width;
       		dx = (2 * new_selection.width) + dx;
       		area_type = get_opposite_event_area_on_x (area_type);
	}

	if (((area_type == C_TOP_AREA)
	     || (area_type == C_TOP_LEFT_AREA)
	     || (area_type == C_TOP_RIGHT_AREA))
	    && (dy > new_selection.height))
	{
	    	new_selection.y += new_selection.height;
       		dy = - (2 * new_selection.height) + dy;
       		area_type = get_opposite_event_area_on_y (area_type);
	}
	else if (((area_type == C_BOTTOM_AREA)
		  || (area_type == C_BOTTOM_LEFT_AREA)
		  || (area_type == C_BOTTOM_RIGHT_AREA))
		 && (-dy > new_selection.height))
	{
       		new_selection.y -= new_selection.height;
       		dy = (2 * new_selection.height) + dy;
       		area_type = get_opposite_event_area_on_y (area_type);
       	}

	event_area = get_event_area_from_id (self, area_type);
	if (event_area != NULL)
		gth_image_viewer_set_cursor (self->priv->viewer, event_area->cursor);

	switch (area_type) {
	case C_SELECTION_AREA:
		new_selection.x += dx;
		new_selection.y += dy;
		break;

	case C_TOP_AREA:
		grow_upward (&self->priv->pixbuf_area, &new_selection, dy, check);
		if (self->priv->use_ratio)
			grow_rightward (&self->priv->pixbuf_area,
					&new_selection,
					IROUND (-dy * self->priv->ratio),
					check);
		break;

	case C_BOTTOM_AREA:
		grow_downward (&self->priv->pixbuf_area, &new_selection, dy, check);
		if (self->priv->use_ratio)
			grow_leftward (&self->priv->pixbuf_area,
				       &new_selection,
				       IROUND (-dy * self->priv->ratio),
				       check);
		break;

	case C_LEFT_AREA:
		grow_leftward (&self->priv->pixbuf_area, &new_selection, dx, check);
		if (self->priv->use_ratio)
			grow_downward (&self->priv->pixbuf_area,
				       &new_selection,
				       IROUND (-dx / self->priv->ratio),
				       check);
		break;

	case C_RIGHT_AREA:
		grow_rightward (&self->priv->pixbuf_area, &new_selection, dx, check);
		if (self->priv->use_ratio)
			grow_upward (&self->priv->pixbuf_area,
				     &new_selection,
				     IROUND (-dx / self->priv->ratio),
				     check);
		break;

	case C_TOP_LEFT_AREA:
		if (self->priv->use_ratio) {
			tmp = self->priv->selection_area;
			semiplane = get_semiplane_no (tmp.x + tmp.width,
						      tmp.y + tmp.height,
						      tmp.x,
						      tmp.y,
						      self->priv->viewer->drag_x - self->priv->viewer->image_area.x,
						      self->priv->viewer->drag_y - self->priv->viewer->image_area.y);
			if (semiplane == 1)
				dy = IROUND (dx / self->priv->ratio);
			else
				dx = IROUND (dy * self->priv->ratio);
		}
		grow_upward (&self->priv->pixbuf_area, &new_selection, dy, check);
		grow_leftward (&self->priv->pixbuf_area, &new_selection, dx, check);
		break;

	case C_TOP_RIGHT_AREA:
		if (self->priv->use_ratio) {
			tmp = self->priv->selection_area;
			semiplane = get_semiplane_no (tmp.x,
						      tmp.y + tmp.height,
						      tmp.x + tmp.width,
						      tmp.y,
						      self->priv->viewer->drag_x - self->priv->viewer->image_area.x,
						      self->priv->viewer->drag_y - self->priv->viewer->image_area.y);
			if (semiplane == 1)
				dx = IROUND (-dy * self->priv->ratio);
			else
				dy = IROUND (-dx / self->priv->ratio);
		}
		grow_upward (&self->priv->pixbuf_area, &new_selection, dy, check);
		grow_rightward (&self->priv->pixbuf_area, &new_selection, dx, check);
		break;

	case C_BOTTOM_LEFT_AREA:
		if (self->priv->use_ratio) {
			tmp = self->priv->selection_area;
			semiplane = get_semiplane_no (tmp.x + tmp.width,
						      tmp.y,
						      tmp.x,
						      tmp.y + tmp.height,
						      self->priv->viewer->drag_x - self->priv->viewer->image_area.x,
						      self->priv->viewer->drag_y - self->priv->viewer->image_area.y);
			if (semiplane == 1)
				dx = IROUND (-dy * self->priv->ratio);
			else
				dy = IROUND (-dx / self->priv->ratio);
		}
		grow_downward (&self->priv->pixbuf_area, &new_selection, dy, check);
		grow_leftward (&self->priv->pixbuf_area, &new_selection, dx, check);
		break;

	case C_BOTTOM_RIGHT_AREA:
		if (self->priv->use_ratio) {
			tmp = self->priv->selection_area;
			semiplane = get_semiplane_no (tmp.x,
						      tmp.y,
						      tmp.x + tmp.width,
						      tmp.y + tmp.height,
						      self->priv->viewer->drag_x - self->priv->viewer->image_area.x,
						      self->priv->viewer->drag_y - self->priv->viewer->image_area.y);

			if (semiplane == 1)
				dy = IROUND (dx / self->priv->ratio);
			else
				dx = IROUND (dy * self->priv->ratio);
		}
		grow_downward (&self->priv->pixbuf_area, &new_selection, dy, check);
		grow_rightward (&self->priv->pixbuf_area, &new_selection, dx, check);
		break;

	default:
		break;
	}

	check_and_set_new_selection (self, new_selection);
}


static gboolean
autoscroll_cb (gpointer data)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (data);
	double            max_value;
	double            value;

	GDK_THREADS_ENTER ();

	max_value = self->priv->viewer->hadj->upper - self->priv->viewer->hadj->page_size;
	value = self->priv->viewer->hadj->value + self->priv->x_value_diff;
	if (value > max_value)
		value = max_value;
	gtk_adjustment_set_value (self->priv->viewer->hadj, value);
	self->priv->viewer->drag_x = self->priv->viewer->drag_x + self->priv->x_value_diff;

	max_value = self->priv->viewer->vadj->upper - self->priv->viewer->vadj->page_size;
	value = self->priv->viewer->vadj->value + self->priv->y_value_diff;
	if (value > max_value)
		value = max_value;
	gtk_adjustment_set_value (self->priv->viewer->vadj, value);
	self->priv->viewer->drag_y = self->priv->viewer->drag_y + self->priv->y_value_diff;

	update_mouse_selection (self, self->priv->viewer->drag_x, self->priv->viewer->drag_y);
	gtk_widget_queue_draw (GTK_WIDGET (self->priv->viewer));

	GDK_THREADS_LEAVE();

	return TRUE;
}


static gboolean
gth_image_selector_motion_notify (GthImageViewerTool *base,
				  GdkEventMotion     *event)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);
	GtkWidget        *widget;
	int               x, y;
	int               absolute_x, absolute_y;

	widget = GTK_WIDGET (self->priv->viewer);
	x = event->x + self->priv->viewer->x_offset;
	y = event->y + self->priv->viewer->y_offset;

	if (self->priv->type == GTH_SELECTOR_TYPE_POINT) {
		x = selector_to_real (self, x - self->priv->viewer->image_area.x);
		y = selector_to_real (self, y - self->priv->viewer->image_area.y);
		if (point_in_rectangle (x, y, self->priv->pixbuf_area))
			g_signal_emit (G_OBJECT (self), signals[MOTION_NOTIFY], 0, x, y);
		return TRUE;
	}

	/* type == GTH_SELECTOR_TYPE_REGION */

	if (! self->priv->viewer->dragging
	    && self->priv->viewer->pressed
	    && ((abs (self->priv->viewer->drag_x - self->priv->viewer->drag_x_prev) > DRAG_THRESHOLD)
		|| (abs (self->priv->viewer->drag_y - self->priv->viewer->drag_y_prev) > DRAG_THRESHOLD))
	    && (self->priv->current_area != NULL))
	{
		int retval;

		retval = gdk_pointer_grab (widget->window,
					   FALSE,
					   (GDK_POINTER_MOTION_MASK
					    | GDK_POINTER_MOTION_HINT_MASK
					    | GDK_BUTTON_RELEASE_MASK),
					   NULL,
					   self->priv->current_area->cursor,
					   event->time);
		if (retval == 0)
			self->priv->viewer->dragging = TRUE;

		return FALSE;
	}

	if (! self->priv->viewer->dragging) {
		update_cursor (self, x, y);
		return FALSE;
	}

	/* dragging == TRUE */

	update_mouse_selection (self, x, y);

	/* If we are out of bounds, schedule a timeout that will do
	 * the scrolling */

	absolute_x = event->x;
	absolute_y = event->y;

	if ((absolute_y < 0) || (absolute_y > widget->allocation.height)
	    || (absolute_x < 0) || (absolute_x > widget->allocation.width))
	{

		/* Make the steppings be relative to the mouse
		 * distance from the canvas.
		 * Also notice the timeout is small to give a smoother
		 * movement.
		 */
		if (absolute_x < 0)
			self->priv->x_value_diff = absolute_x;
		else if (absolute_x > widget->allocation.width)
			self->priv->x_value_diff = absolute_x - widget->allocation.width;
		else
			self->priv->x_value_diff = 0.0;
		self->priv->x_value_diff /= 2;

		if (absolute_y < 0)
			self->priv->y_value_diff = absolute_y;
		else if (absolute_y > widget->allocation.height)
			self->priv->y_value_diff = absolute_y - widget->allocation.height;
		else
			self->priv->y_value_diff = 0.0;
		self->priv->y_value_diff /= 2;

		if (self->priv->timer_id == 0)
			self->priv->timer_id = g_timeout_add (SCROLL_TIMEOUT,
							      autoscroll_cb,
							      self);
	}
	else if (self->priv->timer_id != 0) {
		g_source_remove (self->priv->timer_id);
		self->priv->timer_id = 0;
	}

	return FALSE;
}


static void
gth_image_selector_image_changed (GthImageViewerTool *base)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);

	_g_object_unref (self->priv->pixbuf);
	self->priv->pixbuf = gth_image_viewer_get_current_pixbuf (self->priv->viewer);

	_g_object_unref (self->priv->background);
	self->priv->background = NULL;

	if (self->priv->pixbuf == NULL) {
		self->priv->pixbuf_area.width = 0;
		self->priv->pixbuf_area.height = 0;
		return;
	}

	self->priv->pixbuf = g_object_ref (self->priv->pixbuf);
	self->priv->pixbuf_area.width = gdk_pixbuf_get_width (self->priv->pixbuf);
	self->priv->pixbuf_area.height = gdk_pixbuf_get_height (self->priv->pixbuf);

	self->priv->background = gdk_pixbuf_composite_color_simple (
					self->priv->pixbuf,
					gdk_pixbuf_get_width (self->priv->pixbuf),
					gdk_pixbuf_get_height (self->priv->pixbuf),
					GDK_INTERP_TILES,
					128,
					10,
					0x00000000,
					0x00000000);
	init_selection (self);
}


static void
gth_image_selector_zoom_changed (GthImageViewerTool *base)
{
	GthImageSelector *self = GTH_IMAGE_SELECTOR (base);
	GdkRectangle      selection;

	gth_image_selector_get_selection (self, &selection);
	set_selection (self, selection, TRUE);
}


static void
gth_image_selector_instance_init (GthImageSelector *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GTH_TYPE_IMAGE_SELECTOR, GthImageSelectorPrivate);

	self->priv->type = GTH_SELECTOR_TYPE_REGION;
	self->priv->ratio = 1.0;
	self->priv->mask_visible = TRUE;
	self->priv->grid_type = GTH_GRID_NONE;
}


static void
gth_image_selector_finalize (GObject *object)
{
	GthImageSelector *self;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GTH_IS_IMAGE_SELECTOR (object));

	self = (GthImageSelector *) object;

	_g_object_unref (self->priv->pixbuf);
	_g_object_unref (self->priv->background);

	/* Chain up */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gth_image_selector_class_init (GthImageSelectorClass *class)
{
	GObjectClass *gobject_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (GthImageSelectorPrivate));

	gobject_class = (GObjectClass*) class;
	gobject_class->finalize = gth_image_selector_finalize;

	signals[SELECTION_CHANGED] = g_signal_new ("selection_changed",
						   G_TYPE_FROM_CLASS (class),
						   G_SIGNAL_RUN_LAST,
			 			   G_STRUCT_OFFSET (GthImageSelectorClass, selection_changed),
			 		 	   NULL, NULL,
			 			   g_cclosure_marshal_VOID__VOID,
			 			   G_TYPE_NONE,
			 			   0);
	signals[MOTION_NOTIFY] = g_signal_new ("motion_notify",
					       G_TYPE_FROM_CLASS (class),
					       G_SIGNAL_RUN_LAST,
					       G_STRUCT_OFFSET (GthImageSelectorClass, motion_notify),
					       NULL, NULL,
					       gth_marshal_VOID__INT_INT,
					       G_TYPE_NONE,
					       2,
					       G_TYPE_INT,
					       G_TYPE_INT);
	signals[SELECTED] = g_signal_new ("selected",
					  G_TYPE_FROM_CLASS (class),
					  G_SIGNAL_RUN_LAST,
					  G_STRUCT_OFFSET (GthImageSelectorClass, selected),
					  NULL, NULL,
					  gth_marshal_VOID__INT_INT,
					  G_TYPE_NONE,
					  2,
					  G_TYPE_INT,
					  G_TYPE_INT);
	signals[MASK_VISIBILITY_CHANGED] = g_signal_new ("mask_visibility_changed",
							 G_TYPE_FROM_CLASS (class),
							 G_SIGNAL_RUN_LAST,
							 G_STRUCT_OFFSET (GthImageSelectorClass, mask_visibility_changed),
							 NULL, NULL,
							 g_cclosure_marshal_VOID__VOID,
							 G_TYPE_NONE,
							 0);
        signals[GRID_VISIBILITY_CHANGED] = g_signal_new ("grid_visibility_changed",
                                                         G_TYPE_FROM_CLASS (class),
                                                         G_SIGNAL_RUN_LAST,
                                                         G_STRUCT_OFFSET (GthImageSelectorClass, grid_visibility_changed),
                                                         NULL, NULL,
                                                         g_cclosure_marshal_VOID__VOID,
                                                         G_TYPE_NONE,
                                                         0);
}


static void
gth_image_selector_gth_image_tool_interface_init (GthImageViewerToolIface *iface)
{
	iface->realize = gth_image_selector_realize;
	iface->unrealize = gth_image_selector_unrealize;
	iface->size_allocate = gth_image_selector_size_allocate;
	iface->map = gth_image_selector_map;
	iface->unmap = gth_image_selector_unmap;
	iface->expose = gth_image_selector_expose;
	iface->button_press = gth_image_selector_button_press;
	iface->button_release = gth_image_selector_button_release;
	iface->motion_notify = gth_image_selector_motion_notify;
	iface->image_changed = gth_image_selector_image_changed;
	iface->zoom_changed = gth_image_selector_zoom_changed;
}


GType
gth_image_selector_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo type_info = {
			sizeof (GthImageSelectorClass),
			NULL,
			NULL,
			(GClassInitFunc) gth_image_selector_class_init,
			NULL,
			NULL,
			sizeof (GthImageSelector),
			0,
			(GInstanceInitFunc) gth_image_selector_instance_init
		};
		static const GInterfaceInfo gth_image_tool_info = {
			(GInterfaceInitFunc) gth_image_selector_gth_image_tool_interface_init,
			(GInterfaceFinalizeFunc) NULL,
			NULL
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "GthImageSelector",
					       &type_info,
					       0);
		g_type_add_interface_static (type, GTH_TYPE_IMAGE_VIEWER_TOOL, &gth_image_tool_info);
	}

	return type;
}


GthImageViewerTool *
gth_image_selector_new (GthImageViewer  *viewer,
			GthSelectorType  type)
{
	GthImageSelector *selector;

	selector = g_object_new (GTH_TYPE_IMAGE_SELECTOR, NULL);
	selector->priv->viewer = viewer;
	selector->priv->type = type;

	return GTH_IMAGE_VIEWER_TOOL (selector);
}


void
gth_image_selector_set_selection_x (GthImageSelector *self,
				    int               x)
{
	GdkRectangle new_selection;

	new_selection = self->priv->selection;
	new_selection.x = x;
	check_and_set_new_selection (self, new_selection);
}


void
gth_image_selector_set_selection_y (GthImageSelector *self,
				    int               y)
{
	GdkRectangle new_selection;

	new_selection = self->priv->selection;
	new_selection.y = y;
	check_and_set_new_selection (self, new_selection);
}


void
gth_image_selector_set_selection_width (GthImageSelector *self,
					int               width)
{
	GdkRectangle new_selection;

	new_selection = self->priv->selection;
	new_selection.width = width;
	if (self->priv->use_ratio)
		new_selection.height = IROUND (width / self->priv->ratio);
	check_and_set_new_selection (self, new_selection);
}


void
gth_image_selector_set_selection_height (GthImageSelector *self,
					 int               height)
{
	GdkRectangle new_selection;

	new_selection = self->priv->selection;
	new_selection.height = height;
	if (self->priv->use_ratio)
		new_selection.width = IROUND (height * self->priv->ratio);
	check_and_set_new_selection (self, new_selection);
}


void
gth_image_selector_set_selection (GthImageSelector *self,
				  GdkRectangle      selection)
{
	set_selection (self, selection, FALSE);
}


void
gth_image_selector_get_selection (GthImageSelector *self,
				  GdkRectangle     *selection)
{
	selection->x = MAX (self->priv->selection.x, 0);
	selection->y = MAX (self->priv->selection.y, 0);
	selection->width = MIN (self->priv->selection.width, self->priv->pixbuf_area.width - self->priv->selection.x);
	selection->height = MIN (self->priv->selection.height, self->priv->pixbuf_area.height - self->priv->selection.y);
}


void
gth_image_selector_set_ratio (GthImageSelector *self,
			      gboolean          use_ratio,
			      double            ratio,
			      gboolean		swap_x_and_y_to_start)
{
	int new_starting_width;

	self->priv->use_ratio = use_ratio;
	self->priv->ratio = ratio;

	if (self->priv->use_ratio) {
		/* When changing the cropping aspect ratio, it looks more natural
		   to swap the height and width, rather than (for example) keeping
		   the width constant and shrinking the height. */
		if (swap_x_and_y_to_start == TRUE)
			new_starting_width = self->priv->selection.height;
		else
	       		new_starting_width = self->priv->selection.width;

		gth_image_selector_set_selection_width (self, new_starting_width);
		gth_image_selector_set_selection_height (self, self->priv->selection.height);

		/* However, if swapping the height and width fails because it exceeds
		   the image size, then revert to keeping the width constant and shrinking
		   the height. That is guaranteed to fit inside the old selection. */
		if ( (swap_x_and_y_to_start == TRUE) &&
		     (self->priv->selection.width != new_starting_width))
		{
			gth_image_selector_set_selection_width (self, self->priv->selection.width);
			gth_image_selector_set_selection_height (self, self->priv->selection.height);
		}
	}
}


double
gth_image_selector_get_ratio (GthImageSelector *self)
{
	return self->priv->ratio;
}


gboolean
gth_image_selector_get_use_ratio (GthImageSelector *self)
{
	return self->priv->use_ratio;
}


void
gth_image_selector_set_mask_visible (GthImageSelector *self,
				     gboolean          visible)
{
	if (visible == self->priv->mask_visible)
		return;

	self->priv->mask_visible = visible;
	gtk_widget_queue_draw (GTK_WIDGET (self->priv->viewer));
	g_signal_emit (G_OBJECT (self),
		       signals[MASK_VISIBILITY_CHANGED],
		       0);
}


void
gth_image_selector_set_grid_type (GthImageSelector *self,
                                  GthGridType       grid_type)
{
        if (grid_type == self->priv->grid_type)
                return;

        self->priv->grid_type = grid_type;
        gtk_widget_queue_draw (GTK_WIDGET (self->priv->viewer));
        g_signal_emit (G_OBJECT (self),
                       signals[GRID_VISIBILITY_CHANGED],
                       0);
}


gboolean
gth_image_selector_get_mask_visible (GthImageSelector *self)
{
	return self->priv->mask_visible;
}


GthGridType
gth_image_selector_get_grid_type (GthImageSelector *self)
{
        return self->priv->grid_type;
}
