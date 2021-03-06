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
#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gthumb.h>
#include <extensions/gstreamer_utils/gstreamer-utils.h>
#include "actions.h"
#include "gth-media-viewer-page.h"


#define GTH_MEDIA_VIEWER_PAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GTH_TYPE_MEDIA_VIEWER_PAGE, GthMediaViewerPagePrivate))
#define GET_WIDGET(x) (_gtk_builder_get_widget (self->priv->builder, (x)))
#define PROGRESS_DELAY 500


struct _GthMediaViewerPagePrivate {
	GthBrowser     *browser;
	GtkActionGroup *actions;
	guint           merge_id;
	GthFileData    *file_data;
	GstElement     *playbin;
	GtkBuilder     *builder;
	GtkWidget      *area;
	GtkWidget      *area_box;
	gboolean        playing;
	gboolean        paused;
	gdouble         last_volume;
	gint64          duration;
	int             video_fps_n;
	int             video_fps_d;
	gboolean        has_video;
	gboolean        has_audio;
	gulong          update_progress_id;
	gdouble         rate;
	GtkWidget      *mediabar;
	GtkWidget      *fullscreen_toolbar;
	gboolean        xwin_assigned;
	GdkPixbuf      *icon;
	PangoLayout    *caption_layout;
	gboolean        block_next_jump;
};


static gpointer gth_media_viewer_page_parent_class = NULL;


static const char *media_viewer_ui_info =
"<ui>"
"  <toolbar name='ViewerToolBar'>"
"    <placeholder name='ViewerCommands'>"
"      <toolitem action='View_Fullscreen'/>"
"      <toolitem action='MediaViewer_Screenshot'/>"
"    </placeholder>"
"  </toolbar>"
"  <toolbar name='Fullscreen_ToolBar'>"
"    <placeholder name='ViewerCommands'>"
"      <toolitem action='MediaViewer_Screenshot'/>"
"    </placeholder>"
"  </toolbar>"
"</ui>";


static GtkActionEntry media_viewer_action_entries[] = {
	{ "MediaViewer_Screenshot", "camera-photo",
	  N_("Screenshot"), NULL,
	  N_("Take a screenshot"),
	  G_CALLBACK (media_viewer_activate_action_screenshot) },
};


static void
_gth_media_viewer_page_update_caption (GthMediaViewerPage *self)
{
	if (self->priv->caption_layout == NULL)
		return;

	if (self->priv->file_data != NULL) {
		const char  *text;
		GthMetadata *metadata;

		text = NULL;
		metadata = (GthMetadata *) g_file_info_get_attribute_object (self->priv->file_data->info, "general::title");
		if (metadata != NULL)
			text = gth_metadata_get_formatted (metadata);
		else
			text = g_file_info_get_display_name (self->priv->file_data->info);

		if (text != NULL)
			pango_layout_set_text (self->priv->caption_layout, text, -1);
	}
	else
		pango_layout_set_text (self->priv->caption_layout, "", -1);

	gtk_widget_queue_draw (GTK_WIDGET (self->priv->area));
}


static void
video_area_realize_cb (GtkWidget *widget,
		       gpointer   user_data)
{
	GthMediaViewerPage *self = user_data;

	self->priv->caption_layout = gtk_widget_create_pango_layout (widget, "");
	_gth_media_viewer_page_update_caption (self);
}


static void
video_area_unrealize_cb (GtkWidget *widget,
			 gpointer   user_data)
{
	GthMediaViewerPage *self = user_data;

	g_object_unref (self->priv->caption_layout);
	self->priv->caption_layout = NULL;
}


static gboolean
video_area_expose_event_cb (GtkWidget      *widget,
			    GdkEventExpose *event,
			    gpointer        user_data)
{
	GthMediaViewerPage *self = user_data;

	if (event->count > 0)
		return FALSE;

	if (self->priv->xwin_assigned && self->priv->has_video)
		return FALSE;

	if (self->priv->icon == NULL) {
		char  *type;
		GIcon *icon;
		int    size;

		type = NULL;
		if (self->priv->file_data != NULL)
			type = g_content_type_from_mime_type (gth_file_data_get_mime_type (self->priv->file_data));
		if (type == NULL)
			type = g_content_type_from_mime_type ("text/plain");
		icon = g_content_type_get_icon (type);
		size = widget->allocation.width;
		if (size > widget->allocation.height)
			size = widget->allocation.height;
		size = size / 3;
		self->priv->icon = _g_icon_get_pixbuf (icon, size, gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget)));

		g_object_unref (icon);
		g_free (type);
	}

	gdk_draw_rectangle (gtk_widget_get_window (widget),
			    self->priv->has_video ? widget->style->black_gc : widget->style->text_gc[GTK_WIDGET_STATE (widget)],
			    TRUE,
			    event->area.x,
			    event->area.y,
			    event->area.width,
			    event->area.height);

	if (self->priv->icon != NULL) {
		int            icon_w, icon_h;
		int            icon_x, icon_y;
		PangoRectangle logical_rect;
		int            x, y;

		icon_w = gdk_pixbuf_get_width (self->priv->icon);
		icon_h = gdk_pixbuf_get_height (self->priv->icon);
		pango_layout_set_width (self->priv->caption_layout, PANGO_SCALE * (icon_w * 3 / 2));
		pango_layout_get_extents (self->priv->caption_layout, NULL, &logical_rect);
		icon_x = (widget->allocation.width - icon_w) / 2;
		x = (widget->allocation.width - PANGO_PIXELS (logical_rect.width)) / 2 + PANGO_PIXELS (logical_rect.x);
		icon_y = (widget->allocation.height - (icon_h + PANGO_PIXELS (logical_rect.height))) / 2;
		y = icon_y + icon_h;

		gdk_draw_pixbuf (gtk_widget_get_window (widget),
				 widget->style->base_gc[GTK_WIDGET_STATE (widget)],
				 self->priv->icon,
				 0, 0,
				 icon_x, icon_y,
				 icon_w, icon_h,
				 GDK_RGB_DITHER_NORMAL,
				 0, 0);
		gdk_draw_layout (gtk_widget_get_window (widget),
				 widget->style->base_gc[GTK_WIDGET_STATE (widget)],
				 x, y,
				 self->priv->caption_layout);
	}

	return FALSE;
}


static gboolean
video_area_button_press_cb (GtkWidget          *widget,
			    GdkEventButton     *event,
			    GthMediaViewerPage *self)
{
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1) ) {
		gtk_button_clicked (GTK_BUTTON (GET_WIDGET ("button_play")));
		return TRUE;
	}

	return gth_browser_viewer_button_press_cb (self->priv->browser, event);
}


static gboolean
video_area_scroll_event_cb (GtkWidget 	       *widget,
			    GdkEventScroll     *event,
			    GthMediaViewerPage *self)
{
	return gth_browser_viewer_scroll_event_cb (self->priv->browser, event);
}


static gboolean
video_area_key_press_cb (GtkWidget          *widget,
			 GdkEventKey        *event,
			 GthMediaViewerPage *self)
{
	return gth_browser_viewer_key_press_cb (self->priv->browser, event);
}


static void
volume_value_changed_cb (GtkAdjustment *adjustment,
			 gpointer       user_data)
{
	GthMediaViewerPage *self = user_data;
	if (self->priv->playbin != NULL)
		g_object_set (self->priv->playbin,
			      "volume",
			      gtk_adjustment_get_value (adjustment) / 10.0,
			      NULL);
}


static void position_value_changed_cb (GtkAdjustment *adjustment,
				       gpointer       user_data);


static void
update_current_position_bar (GthMediaViewerPage *self,
			     gboolean            update_progressbar)
{
	GstFormat format;
        gint64    current_value = 0;

        format = GST_FORMAT_TIME;
        if (gst_element_query_position (self->priv->playbin, &format, &current_value)) {
        	char *s;

        	if (self->priv->duration <= 0) {
        		gst_element_query_duration (self->priv->playbin, &format, &self->priv->duration);
        		s = _g_format_duration_for_display (GST_TIME_AS_MSECONDS (self->priv->duration));
        		gtk_label_set_text (GTK_LABEL (GET_WIDGET ("label_duration")), s);

        		g_free (s);
        	}

        	/*
        	g_print ("==> %" G_GINT64_FORMAT " / %" G_GINT64_FORMAT " (%0.3g)\n" ,
        		 current_value,
        		 self->priv->duration,
        		 ((double) current_value / self->priv->duration) * 100.0);
		*/

        	if (update_progressbar) {
			g_signal_handlers_block_by_func(GET_WIDGET ("adjustment_position"), position_value_changed_cb, self);
			gtk_adjustment_set_value (GTK_ADJUSTMENT (GET_WIDGET ("adjustment_position")), (self->priv->duration > 0) ? ((double) current_value / self->priv->duration) * 100.0 : 0.0);
			g_signal_handlers_unblock_by_func(GET_WIDGET ("adjustment_position"), position_value_changed_cb, self);
        	}

        	s = _g_format_duration_for_display (GST_TIME_AS_MSECONDS (current_value));
        	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("label_position")), s);

        	g_free (s);
        }
}


static void
position_value_changed_cb (GtkAdjustment *adjustment,
			   gpointer       user_data)
{
	GthMediaViewerPage *self = user_data;
	gint64              current_value;
	char               *s;

	if (self->priv->playbin == NULL)
		return;

	current_value = (gint64) (gtk_adjustment_get_value (adjustment) / 100.0 * self->priv->duration);
	if (! gst_element_seek_simple (self->priv->playbin,
				       GST_FORMAT_TIME,
				       GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
				       current_value))
	{
		g_warning ("seek failed");
	}

	s = _g_format_duration_for_display (GST_TIME_AS_MSECONDS (current_value));
	gtk_label_set_text (GTK_LABEL (GET_WIDGET ("label_position")), s);

	g_free (s);
}


static char *
hscale_volume_format_value_cb (GtkScale *scale,
			       double    value,
			       gpointer  user_data)
{
	return g_strdup_printf ("%0.0f%%", value);
}


static gboolean
hscale_position_change_value_cb (GtkRange      *range,
				 GtkScrollType  scroll,
				 gdouble        value,
				 gpointer       user_data)
{
	GthMediaViewerPage *self = user_data;

	if (self->priv->block_next_jump && (scroll == GTK_SCROLL_JUMP)) {
		self->priv->block_next_jump = FALSE;
		return TRUE;
	}

	return FALSE;
}


static gboolean
hscale_position_button_release_event_cb (GtkWidget      *widget,
					 GdkEventButton *event,
					 gpointer        user_data)
{
	GthMediaViewerPage *self = user_data;

	if (self->priv->playing)
		self->priv->block_next_jump = TRUE;

	return FALSE;
}


static void
update_player_rate (GthMediaViewerPage *self)
{
	self->priv->rate = CLAMP (self->priv->rate, 0.25, 2.0);

	if (self->priv->playbin == NULL)
		return;

	if (! self->priv->playing)
		return;

	if (! gst_element_seek (self->priv->playbin,
				self->priv->rate,
				GST_FORMAT_TIME,
				(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
				GST_SEEK_TYPE_NONE,
				0.0,
				GST_SEEK_TYPE_NONE,
				0.0))
	{
		g_warning ("seek failed");
	}
}


static void
button_play_clicked_cb (GtkButton *button,
			gpointer   user_data)
{
	GthMediaViewerPage *self = user_data;

	if (self->priv->playbin == NULL)
		return;
	if (! self->priv->playing) {
		if (! self->priv->paused) {
			gst_element_set_state (self->priv->playbin, GST_STATE_PAUSED);
			gst_element_seek_simple (self->priv->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0);
		}
		gst_element_set_state (self->priv->playbin, GST_STATE_PLAYING);
	}
	else
		gst_element_set_state (self->priv->playbin, GST_STATE_PAUSED);
}


static void
togglebutton_volume_toggled_cb (GtkToggleButton *button,
				gpointer         user_data)
{
	GthMediaViewerPage *self = user_data;

	if (self->priv->playbin == NULL)
		return;

	if (gtk_toggle_button_get_active (button)) {
		g_object_get (self->priv->playbin, "volume", &self->priv->last_volume, NULL);
		g_object_set (self->priv->playbin, "volume", 0.0, NULL);
	}
	else
		g_object_set (self->priv->playbin, "volume", self->priv->last_volume, NULL);
}


static void
button_play_slower_clicked_cb (GtkButton *button,
			       gpointer   user_data)
{
	GthMediaViewerPage *self = user_data;

	self->priv->rate -= 0.25;
	update_player_rate (self);
}


static void
button_play_faster_clicked_cb (GtkButton *button,
			       gpointer   user_data)
{
	GthMediaViewerPage *self = user_data;

	self->priv->rate += 0.25;
	update_player_rate (self);
}


static void
update_volume_from_playbin (GthMediaViewerPage *self)
{
	double volume;

	if ((self->priv->builder == NULL) || (self->priv->playbin == NULL))
		return;

	g_object_get (self->priv->playbin, "volume", &volume, NULL);
	if (volume == 0.0)
		gtk_image_set_from_icon_name (GTK_IMAGE (GET_WIDGET ("togglebutton_volume_image")), "audio-volume-muted", GTK_ICON_SIZE_BUTTON);
	else if (volume < 3.3)
		gtk_image_set_from_icon_name (GTK_IMAGE (GET_WIDGET ("togglebutton_volume_image")), "audio-volume-low", GTK_ICON_SIZE_BUTTON);
	else if (volume < 6.6)
		gtk_image_set_from_icon_name (GTK_IMAGE (GET_WIDGET ("togglebutton_volume_image")), "audio-volume-medium", GTK_ICON_SIZE_BUTTON);
	else
		gtk_image_set_from_icon_name (GTK_IMAGE (GET_WIDGET ("togglebutton_volume_image")), "audio-volume-high", GTK_ICON_SIZE_BUTTON);

	g_signal_handlers_block_by_func(GET_WIDGET ("adjustment_volume"), volume_value_changed_cb, self);
	gtk_adjustment_set_value (GTK_ADJUSTMENT (GET_WIDGET ("adjustment_volume")), volume * 10.0);
	g_signal_handlers_unblock_by_func(GET_WIDGET ("adjustment_volume"), volume_value_changed_cb, self);
}


static gboolean
update_progress_cb (gpointer user_data)
{
	GthMediaViewerPage *self = user_data;

        if (self->priv->update_progress_id != 0) {
                g_source_remove (self->priv->update_progress_id);
                self->priv->update_progress_id = 0;
        }

        update_current_position_bar (self, TRUE);

        self->priv->update_progress_id = gdk_threads_add_timeout (PROGRESS_DELAY, update_progress_cb, self);

        return FALSE;
}


static void
update_play_button (GthMediaViewerPage *self,
		    GstState            new_state)
{
	if (! self->priv->playing && (new_state == GST_STATE_PLAYING)) {
		self->priv->playing = TRUE;
		gtk_image_set_from_stock (GTK_IMAGE (GET_WIDGET ("button_play_image")), GTK_STOCK_MEDIA_PAUSE, GTK_ICON_SIZE_LARGE_TOOLBAR);
		gtk_widget_set_tooltip_text (GET_WIDGET ("button_play_image"), _("Pause"));

		if (self->priv->update_progress_id == 0)
			self->priv->update_progress_id = gdk_threads_add_timeout (PROGRESS_DELAY, update_progress_cb, self);
	}
	else if (self->priv->playing && (new_state != GST_STATE_PLAYING)) {
		self->priv->playing = FALSE;
		gtk_image_set_from_stock (GTK_IMAGE (GET_WIDGET ("button_play_image")), GTK_STOCK_MEDIA_PLAY, GTK_ICON_SIZE_LARGE_TOOLBAR);
		gtk_widget_set_tooltip_text (GET_WIDGET ("button_play_image"), _("Play"));

		if (self->priv->update_progress_id != 0) {
			 g_source_remove (self->priv->update_progress_id);
			 self->priv->update_progress_id = 0;
		}
	}

	gth_viewer_page_update_sensitivity (GTH_VIEWER_PAGE (self));
}


static void
gth_media_viewer_page_real_activate (GthViewerPage *base,
				     GthBrowser    *browser)
{
	GthMediaViewerPage *self;

	if (! gstreamer_init ())
		return;

	self = (GthMediaViewerPage*) base;

	self->priv->browser = browser;

	self->priv->actions = gtk_action_group_new ("Video Viewer Actions");
	gtk_action_group_set_translation_domain (self->priv->actions, NULL);
	gtk_action_group_add_actions (self->priv->actions,
				      media_viewer_action_entries,
				      G_N_ELEMENTS (media_viewer_action_entries),
				      self);
	gtk_ui_manager_insert_action_group (gth_browser_get_ui_manager (browser), self->priv->actions, 0);

	self->priv->area_box = gtk_vbox_new (FALSE, 0);

	/* video area */

	self->priv->area = gtk_drawing_area_new ();
	gtk_widget_set_double_buffered (self->priv->area, FALSE);
	gtk_widget_add_events (self->priv->area, (gtk_widget_get_events (self->priv->area)
						  | GDK_EXPOSURE_MASK
						  | GDK_BUTTON_PRESS_MASK
						  | GDK_BUTTON_RELEASE_MASK
						  | GDK_POINTER_MOTION_MASK
						  | GDK_POINTER_MOTION_HINT_MASK
						  | GDK_BUTTON_MOTION_MASK));
	GTK_WIDGET_SET_FLAGS (self->priv->area, GTK_CAN_FOCUS);
	gtk_widget_show (self->priv->area);
	gtk_box_pack_start (GTK_BOX (self->priv->area_box), self->priv->area, TRUE, TRUE, 0);

	g_signal_connect (G_OBJECT (self->priv->area),
			  "realize",
			  G_CALLBACK (video_area_realize_cb),
			  self);
	g_signal_connect (G_OBJECT (self->priv->area),
			  "unrealize",
			  G_CALLBACK (video_area_unrealize_cb),
			  self);
	g_signal_connect (G_OBJECT (self->priv->area),
			  "expose_event",
			  G_CALLBACK (video_area_expose_event_cb),
			  self);
	g_signal_connect (G_OBJECT (self->priv->area),
			  "button_press_event",
			  G_CALLBACK (video_area_button_press_cb),
			  self);
	g_signal_connect (G_OBJECT (self->priv->area),
			  "scroll_event",
			  G_CALLBACK (video_area_scroll_event_cb),
			  self);
	g_signal_connect (G_OBJECT (self->priv->area),
			  "key_press_event",
			  G_CALLBACK (video_area_key_press_cb),
			  self);

	/* mediabar */

	self->priv->builder = _gtk_builder_new_from_file ("mediabar.ui", "gstreamer_tools");
	self->priv->mediabar = GET_WIDGET ("mediabar");
	gtk_widget_show (self->priv->mediabar);
	gtk_box_pack_start (GTK_BOX (self->priv->area_box), self->priv->mediabar, FALSE, FALSE, 0);

	g_signal_connect (GET_WIDGET ("adjustment_volume"), "value-changed", G_CALLBACK (volume_value_changed_cb), self);
	g_signal_connect (GET_WIDGET ("adjustment_position"), "value-changed", G_CALLBACK (position_value_changed_cb), self);
	g_signal_connect (GET_WIDGET ("hscale_volume"), "format-value", G_CALLBACK (hscale_volume_format_value_cb), self);
	g_signal_connect (GET_WIDGET ("hscale_position"), "change-value", G_CALLBACK (hscale_position_change_value_cb), self);
	g_signal_connect (GET_WIDGET ("hscale_position"), "button-release-event", G_CALLBACK (hscale_position_button_release_event_cb), self);
	g_signal_connect (GET_WIDGET ("button_play"), "clicked", G_CALLBACK (button_play_clicked_cb), self);
	g_signal_connect (GET_WIDGET ("togglebutton_volume"), "toggled", G_CALLBACK (togglebutton_volume_toggled_cb), self);
	g_signal_connect (GET_WIDGET ("button_play_slower"), "clicked", G_CALLBACK (button_play_slower_clicked_cb), self);
	g_signal_connect (GET_WIDGET ("button_play_faster"), "clicked", G_CALLBACK (button_play_faster_clicked_cb), self);

	gtk_widget_show (self->priv->area_box);
	gth_browser_set_viewer_widget (browser, self->priv->area_box);

	gtk_widget_realize (self->priv->area);
	gdk_window_ensure_native (gtk_widget_get_window (self->priv->area));
	gth_viewer_page_focus (GTH_VIEWER_PAGE (self));
}


static void
gth_media_viewer_page_real_deactivate (GthViewerPage *base)
{
	GthMediaViewerPage *self;

	self = (GthMediaViewerPage*) base;

	if (self->priv->builder != NULL) {
		g_object_unref (self->priv->builder);
		self->priv->builder = NULL;
	}

        if (self->priv->update_progress_id != 0) {
                g_source_remove (self->priv->update_progress_id);
                self->priv->update_progress_id = 0;
        }

	if (self->priv->playbin != NULL) {
		gst_element_set_state (self->priv->playbin, GST_STATE_NULL);
		gst_object_unref (GST_OBJECT (self->priv->playbin));
		self->priv->playbin = NULL;
	}

	gth_browser_set_viewer_widget (self->priv->browser, NULL);
}


static GstBusSyncReply
set_playbin_window (GstBus             *bus,
		    GstMessage         *message,
		    GthMediaViewerPage *self)
{
	GstXOverlay *image_sink;

	/* ignore anything but 'prepare-xwindow-id' element messages */

	if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
		return GST_BUS_PASS;
	if (! gst_structure_has_name (message->structure, "prepare-xwindow-id"))
		return GST_BUS_PASS;

	image_sink = GST_X_OVERLAY (GST_MESSAGE_SRC (message));
	gst_x_overlay_set_xwindow_id (image_sink, GDK_WINDOW_XID (gtk_widget_get_window (self->priv->area)));
	g_object_set (image_sink, "force-aspect-ratio", TRUE, NULL);
	self->priv->xwin_assigned = TRUE;

	gst_message_unref (message);

	return GST_BUS_DROP;
}


static void
reset_player_state (GthMediaViewerPage *self)
{
        if (self->priv->update_progress_id != 0) {
                g_source_remove (self->priv->update_progress_id);
                self->priv->update_progress_id = 0;
        }

	update_play_button (self, GST_STATE_NULL);
	self->priv->playing = FALSE;
	self->priv->rate = 1.0;
}


static void
update_stream_info (GthMediaViewerPage *self)
{
	GList  *streaminfo;
	GstPad *videopad;

	streaminfo = NULL;
	videopad = NULL;

	g_object_get (self->priv->playbin, "stream-info", &streaminfo, NULL);
	streaminfo = g_list_copy (streaminfo);
	g_list_foreach (streaminfo, (GFunc) g_object_ref, NULL);

	for (/* void */ ; streaminfo; streaminfo = streaminfo->next) {
		GObject    *info;
		int         type;
		GParamSpec *pspec;
		GEnumValue *val;

		info = streaminfo->data;
		if (info == NULL)
			continue;

                type = -1;

		g_object_get (info, "type", &type, NULL);
		pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (info), "type");
		val = g_enum_get_value (G_PARAM_SPEC_ENUM (pspec)->enum_class, type);

		if (strcmp (val->value_nick, "audio") == 0) {
			self->priv->has_audio = TRUE;
		}
		else if (strcmp (val->value_nick, "video") == 0) {
			self->priv->has_video = TRUE;
			if (videopad == NULL)
				g_object_get (info, "object", &videopad, NULL);
		}
	}

	if (videopad != NULL) {
		GstCaps *caps;

		if ((caps = gst_pad_get_negotiated_caps (videopad)) != NULL) {
			GstStructure *structure;

			structure = gst_caps_get_structure (caps, 0);
			gst_structure_get_fraction (structure, "framerate", &self->priv->video_fps_n, &self->priv->video_fps_d);

			gst_caps_unref (caps);
		}
	}

	g_list_foreach (streaminfo, (GFunc) g_object_unref, NULL);
	g_list_free (streaminfo);
}


static void
bus_message_cb (GstBus     *bus,
                GstMessage *message,
                gpointer    user_data)
{
	GthMediaViewerPage *self = user_data;

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_STATE_CHANGED: {
		GstState old_state;
		GstState new_state;
		GstState pending_state;

		old_state = new_state = GST_STATE_NULL;
		gst_message_parse_state_changed (message, &old_state, &new_state, &pending_state);

		self->priv->paused = (new_state == GST_STATE_PAUSED);

		if (old_state == new_state)
			break;
		if (GST_MESSAGE_SRC (message) != GST_OBJECT (self->priv->playbin))
			break;

		update_current_position_bar (self, TRUE);

		if ((old_state == GST_STATE_READY) && (new_state == GST_STATE_PAUSED)) {
			update_stream_info (self);
			gth_viewer_page_update_sensitivity (GTH_VIEWER_PAGE (self));
		}
		if ((old_state == GST_STATE_READY) || (new_state == GST_STATE_PAUSED))
			update_volume_from_playbin (self);
		if ((old_state == GST_STATE_PLAYING) || (new_state == GST_STATE_PLAYING))
			update_play_button (self, new_state);
		break;
	}

	case GST_MESSAGE_DURATION: {
		GstFormat format;

		format = GST_FORMAT_TIME;
		gst_message_parse_duration (message, &format, &self->priv->duration);
		update_current_position_bar (self, TRUE);
		break;
	}

	case GST_MESSAGE_EOS:
		reset_player_state (self);
		break;

	case GST_MESSAGE_BUFFERING: {
		int percent = 0;
		gst_message_parse_buffering (message, &percent);
		g_print ("Buffering (%%%u percent done)", percent);
		break;
	}

	default:
		break;
	}
}


static void
playbin_notify_volume_cb (GObject    *playbin,
			  GParamSpec *pspec,
			  gpointer    user_data)
{
	update_volume_from_playbin ((GthMediaViewerPage *) user_data);
}


static void
gth_media_viewer_page_real_show (GthViewerPage *base)
{
	GthMediaViewerPage *self;
	GError             *error = NULL;
	GstBus             *bus;
	char               *uri;

	self = (GthMediaViewerPage*) base;

	if (self->priv->merge_id != 0)
		return;

	self->priv->merge_id = gtk_ui_manager_add_ui_from_string (gth_browser_get_ui_manager (self->priv->browser), media_viewer_ui_info, -1, &error);
	if (self->priv->merge_id == 0) {
		g_warning ("ui building failed: %s", error->message);
		g_error_free (error);
	}

	gth_viewer_page_focus (GTH_VIEWER_PAGE (self));

	if (self->priv->playbin != NULL)
		return;

	self->priv->playbin = gst_element_factory_make ("playbin", "playbin");
	g_signal_connect (self->priv->playbin, "notify::volume", G_CALLBACK (playbin_notify_volume_cb), self);

	bus = gst_pipeline_get_bus (GST_PIPELINE (self->priv->playbin));
	gst_bus_enable_sync_message_emission (bus);
	gst_bus_set_sync_handler (bus, (GstBusSyncHandler) set_playbin_window, self);
	gst_bus_add_signal_watch (bus);
	g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), self);

	if (self->priv->file_data == NULL)
		return;

	uri = g_file_get_uri (self->priv->file_data->file);
	g_object_set (G_OBJECT (self->priv->playbin), "uri", uri, NULL);
	gst_element_set_state (self->priv->playbin, GST_STATE_PAUSED);

	g_free (uri);
}


static void
gth_media_viewer_page_real_hide (GthViewerPage *base)
{
	GthMediaViewerPage *self;

	self = (GthMediaViewerPage*) base;

	if (self->priv->merge_id != 0) {
		gtk_ui_manager_remove_ui (gth_browser_get_ui_manager (self->priv->browser), self->priv->merge_id);
		self->priv->merge_id = 0;
	}

	if ((self->priv->playbin != NULL) && self->priv->playing)
		gst_element_set_state (self->priv->playbin, GST_STATE_PAUSED);
}


static gboolean
gth_media_viewer_page_real_can_view (GthViewerPage *base,
				     GthFileData   *file_data)
{
	GthMediaViewerPage *self;

	self = (GthMediaViewerPage*) base;
	g_return_val_if_fail (file_data != NULL, FALSE);

	return _g_mime_type_is_video (gth_file_data_get_mime_type (file_data)) || _g_mime_type_is_audio (gth_file_data_get_mime_type (file_data));
}


static gboolean
set_to_paused (gpointer user_data)
{
	GthMediaViewerPage *self = user_data;

	if (self->priv->playbin != NULL)
		gst_element_set_state (self->priv->playbin, GST_STATE_PAUSED);
	return FALSE;
}


static void
gth_media_viewer_page_real_view (GthViewerPage *base,
				 GthFileData   *file_data)
{
	GthMediaViewerPage *self;
	char               *uri;

	self = (GthMediaViewerPage*) base;
	g_return_if_fail (file_data != NULL);

	if (! gstreamer_init ())
		return;

	gth_viewer_page_focus (GTH_VIEWER_PAGE (self));

	if ((self->priv->file_data != NULL)
	    && g_file_equal (file_data->file, self->priv->file_data->file)
	    && (gth_file_data_get_mtime (file_data) == gth_file_data_get_mtime (self->priv->file_data)))
	{
		return;
	}

	/**/

	_g_object_unref (self->priv->file_data);
	self->priv->file_data = gth_file_data_dup (file_data);

	self->priv->duration = 0;

	_g_object_unref (self->priv->icon);
	self->priv->icon = NULL;

	_gth_media_viewer_page_update_caption (self);

	/**/

	gth_viewer_page_file_loaded (GTH_VIEWER_PAGE (self), TRUE);
	g_signal_handlers_block_by_func(GET_WIDGET ("adjustment_position"), position_value_changed_cb, self);
	gtk_adjustment_set_value (GTK_ADJUSTMENT (GET_WIDGET ("adjustment_position")), 0.0);
	g_signal_handlers_unblock_by_func(GET_WIDGET ("adjustment_position"), position_value_changed_cb, self);
	reset_player_state (self);

	if (self->priv->playbin == NULL)
		return;

	gst_element_set_state (self->priv->playbin, GST_STATE_NULL);
	uri = g_file_get_uri (self->priv->file_data->file);
	g_object_set (G_OBJECT (self->priv->playbin), "uri", uri, NULL);

	gdk_threads_add_idle (set_to_paused, self);

	g_free (uri);
}


static void
gth_media_viewer_page_real_focus (GthViewerPage *base)
{
	GtkWidget *widget;

	widget = GTH_MEDIA_VIEWER_PAGE (base)->priv->area;
	if (GTK_WIDGET_REALIZED (widget) && GTK_WIDGET_MAPPED (widget))
		gtk_widget_grab_focus (widget);
}


static void
gth_media_viewer_page_real_fullscreen (GthViewerPage *base,
				       gboolean       active)
{
	GthMediaViewerPage *self = (GthMediaViewerPage*) base;
	GdkScreen          *screen;

	if (! active) {
		g_object_ref (self->priv->mediabar);
		gtk_container_remove (GTK_CONTAINER (self->priv->fullscreen_toolbar), self->priv->mediabar);
		gtk_box_pack_start (GTK_BOX (self->priv->area_box), self->priv->mediabar, FALSE, FALSE, 0);
		g_object_unref (self->priv->mediabar);

		gtk_widget_destroy (self->priv->fullscreen_toolbar);
		self->priv->fullscreen_toolbar = NULL;

		return;
	}

	/* active == TRUE */

	screen = gtk_widget_get_screen (GTK_WIDGET (self->priv->browser));

	if (self->priv->fullscreen_toolbar == NULL) {
		self->priv->fullscreen_toolbar = gtk_window_new (GTK_WINDOW_POPUP);
		gtk_window_set_screen (GTK_WINDOW (self->priv->fullscreen_toolbar), screen);
		gtk_window_set_default_size (GTK_WINDOW (self->priv->fullscreen_toolbar), gdk_screen_get_width (screen), -1);
		gtk_container_set_border_width (GTK_CONTAINER (self->priv->fullscreen_toolbar), 0);
	}

	g_object_ref (self->priv->mediabar);
	gtk_container_remove (GTK_CONTAINER (self->priv->area_box), self->priv->mediabar);
	gtk_container_add (GTK_CONTAINER (self->priv->fullscreen_toolbar), self->priv->mediabar);
	g_object_unref (self->priv->mediabar);

	gtk_widget_realize (self->priv->mediabar);
	gtk_window_set_gravity (GTK_WINDOW (self->priv->fullscreen_toolbar), GDK_GRAVITY_SOUTH_EAST);
	gtk_window_move (GTK_WINDOW (self->priv->fullscreen_toolbar), 0, gdk_screen_get_height (screen) - self->priv->mediabar->allocation.height);

	gth_browser_register_fullscreen_control (self->priv->browser, self->priv->fullscreen_toolbar);
}


static void
gth_media_viewer_page_real_show_pointer (GthViewerPage *base,
				         gboolean       show)
{
	GthMediaViewerPage *self = (GthMediaViewerPage*) base;

	if (self->priv->fullscreen_toolbar != NULL) {
		if (show && ! GTK_WIDGET_VISIBLE (self->priv->fullscreen_toolbar)) {
			gtk_widget_show (self->priv->fullscreen_toolbar);
		}
		else if (! show && GTK_WIDGET_VISIBLE (self->priv->fullscreen_toolbar)) {
			gtk_widget_hide (self->priv->fullscreen_toolbar);
		}
	}
}


static void
set_action_sensitive (GthMediaViewerPage *self,
		      const char         *action_name,
		      gboolean            sensitive)
{
	GtkAction *action;

	action = gtk_action_group_get_action (self->priv->actions, action_name);
	g_object_set (action, "sensitive", sensitive, NULL);
}


static void
gth_media_viewer_page_real_update_sensitivity (GthViewerPage *base)
{
	GthMediaViewerPage *self = (GthMediaViewerPage *) base;

	gtk_widget_set_sensitive (GET_WIDGET ("button_play_slower"), self->priv->playing);
	gtk_widget_set_sensitive (GET_WIDGET ("button_play_faster"), self->priv->playing);
	gtk_widget_set_sensitive (GET_WIDGET ("volume_box"), self->priv->has_audio);
	set_action_sensitive (self, "MediaViewer_Screenshot", self->priv->has_video);
}


static gboolean
gth_media_viewer_page_real_can_save (GthViewerPage *base)
{
	return FALSE;
}


static void
gth_media_viewer_page_real_save (GthViewerPage *base,
				 GFile         *file,
				 FileSavedFunc  func,
				 gpointer       user_data)
{
	/* void */
}


static void
gth_media_viewer_page_real_save_as (GthViewerPage *base,
				    FileSavedFunc  func,
				    gpointer       user_data)
{
	/* void */
}


static void
gth_media_viewer_page_real_revert (GthViewerPage *base)
{
	/* void */
}


static void
gth_media_viewer_page_finalize (GObject *obj)
{
	GthMediaViewerPage *self;

	self = GTH_MEDIA_VIEWER_PAGE (obj);

        if (self->priv->update_progress_id != 0) {
                g_source_remove (self->priv->update_progress_id);
                self->priv->update_progress_id = 0;
        }

	if (self->priv->playbin != NULL) {
		gst_element_set_state (self->priv->playbin, GST_STATE_NULL);
		gst_object_unref (GST_OBJECT (self->priv->playbin));
		self->priv->playbin = NULL;
	}
	_g_object_unref (self->priv->icon);
	_g_object_unref (self->priv->file_data);

	G_OBJECT_CLASS (gth_media_viewer_page_parent_class)->finalize (obj);
}


static void
gth_media_viewer_page_class_init (GthMediaViewerPageClass *klass)
{
	gth_media_viewer_page_parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (GthMediaViewerPagePrivate));

	G_OBJECT_CLASS (klass)->finalize = gth_media_viewer_page_finalize;
}


static void
gth_viewer_page_interface_init (GthViewerPageIface *iface)
{
	iface->activate = gth_media_viewer_page_real_activate;
	iface->deactivate = gth_media_viewer_page_real_deactivate;
	iface->show = gth_media_viewer_page_real_show;
	iface->hide = gth_media_viewer_page_real_hide;
	iface->can_view = gth_media_viewer_page_real_can_view;
	iface->view = gth_media_viewer_page_real_view;
	iface->focus = gth_media_viewer_page_real_focus;
	iface->fullscreen = gth_media_viewer_page_real_fullscreen;
	iface->show_pointer = gth_media_viewer_page_real_show_pointer;
	iface->update_sensitivity = gth_media_viewer_page_real_update_sensitivity;
	iface->can_save = gth_media_viewer_page_real_can_save;
	iface->save = gth_media_viewer_page_real_save;
	iface->save_as = gth_media_viewer_page_real_save_as;
	iface->revert = gth_media_viewer_page_real_revert;
}


static void
gth_media_viewer_page_instance_init (GthMediaViewerPage *self)
{
	self->priv = GTH_MEDIA_VIEWER_PAGE_GET_PRIVATE (self);
	self->priv->update_progress_id = 0;
	self->priv->xwin_assigned = FALSE;
	self->priv->has_video = FALSE;
	self->priv->has_audio = FALSE;
	self->priv->video_fps_n = 0;
	self->priv->video_fps_d = 0;
	self->priv->icon = NULL;
}


GType
gth_media_viewer_page_get_type (void) {
	static GType gth_media_viewer_page_type_id = 0;
	if (gth_media_viewer_page_type_id == 0) {
		static const GTypeInfo g_define_type_info = {
			sizeof (GthMediaViewerPageClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gth_media_viewer_page_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,
			sizeof (GthMediaViewerPage),
			0,
			(GInstanceInitFunc) gth_media_viewer_page_instance_init,
			NULL
		};
		static const GInterfaceInfo gth_viewer_page_info = {
			(GInterfaceInitFunc) gth_viewer_page_interface_init,
			(GInterfaceFinalizeFunc) NULL,
			NULL
		};
		gth_media_viewer_page_type_id = g_type_register_static (G_TYPE_OBJECT, "GthMediaViewerPage", &g_define_type_info, 0);
		g_type_add_interface_static (gth_media_viewer_page_type_id, GTH_TYPE_VIEWER_PAGE, &gth_viewer_page_info);
	}
	return gth_media_viewer_page_type_id;
}


GthBrowser *
gth_media_viewer_page_get_browser (GthMediaViewerPage *self)
{
	return self->priv->browser;
}


GstElement *
gth_media_viewer_page_get_playbin (GthMediaViewerPage *self)
{
	return self->priv->playbin;
}


gboolean
gth_media_viewer_page_is_playing (GthMediaViewerPage *self)
{
	return self->priv->playing;
}


void
gth_media_viewer_page_get_video_fps (GthMediaViewerPage *self,
				     int                *video_fps_n,
				     int                *video_fps_d)
{
	if (video_fps_n != NULL)
		*video_fps_n = self->priv->video_fps_n;
	if (video_fps_d != NULL)
		*video_fps_d = self->priv->video_fps_d;
}


GthFileData *
gth_media_viewer_page_get_file_data (GthMediaViewerPage *self)
{
	return self->priv->file_data;
}
