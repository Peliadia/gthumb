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
#include <glib/gi18n.h>
#include <gthumb.h>
#include "gth-copy-task.h"
#include "gth-delete-task.h"
#include "gth-duplicate-task.h"


static void
_gth_browser_create_new_folder (GthBrowser *browser,
				GFile      *parent)
{
	char   *folder_name;
	GError *error = NULL;
	GFile  *new_folder;

	folder_name = _gtk_request_dialog_run (GTK_WINDOW (browser),
					       GTK_DIALOG_MODAL,
					       _("Enter the folder name: "),
				               "",
					       1024,
					       GTK_STOCK_CANCEL,
					       _("C_reate"));
	if (folder_name == NULL)
		return;

	new_folder = g_file_get_child_for_display_name (parent, folder_name, &error);
	if ((new_folder != NULL) && (g_file_make_directory (new_folder, NULL, &error))) {
		GList       *list;
		GtkWidget   *folder_tree;
		GtkTreePath *path;

		list = g_list_prepend (NULL, new_folder);
		gth_monitor_folder_changed (gth_main_get_default_monitor (),
					    parent,
					    list,
					    GTH_MONITOR_EVENT_CREATED);

		folder_tree = gth_browser_get_folder_tree (browser);
		path = gth_folder_tree_get_path (GTH_FOLDER_TREE (folder_tree), parent);
		gth_folder_tree_expand_row (GTH_FOLDER_TREE (folder_tree), path, FALSE);

		gtk_tree_path_free (path);
		g_list_free (list);
	}
	else
		_gtk_error_dialog_from_gerror_show (GTK_WINDOW (browser), _("Could not create the folder"), &error);

	_g_object_unref (new_folder);
	g_free (folder_name);
}


void
gth_browser_action_new_folder (GtkAction  *action,
			       GthBrowser *browser)
{
	GFile *parent;

	parent = g_object_ref (gth_browser_get_location (browser));
	_gth_browser_create_new_folder (browser, parent);
	g_object_unref (parent);
}


/* -- gth_browser_clipboard_copy / gth_browser_clipboard_cut -- */


typedef struct {
	char     **uris;
	int        n_uris;
	gboolean   cut;
} ClipboardData;


static char *
clipboard_data_convert_to_text (ClipboardData *clipboard_data,
				gboolean       formatted,
				gsize         *len)
{
	GString *uris;
	int      i;

	if (formatted)
		uris = g_string_new (clipboard_data->cut ? "cut" : "copy");
	else
		uris = g_string_new (NULL);

	for (i = 0; i < clipboard_data->n_uris; i++) {
		if (formatted) {
			g_string_append_c (uris, '\n');
			g_string_append (uris, clipboard_data->uris[i]);
		}
		else {
			GFile *file;
			char  *name;

			if (i > 0)
				g_string_append_c (uris, '\n');
			file = g_file_new_for_uri (clipboard_data->uris[i]);
			name = g_file_get_parse_name (file);
			g_string_append (uris, name);

			g_free (name);
			g_object_unref (file);
		}
	}

	if (len != NULL)
		*len = uris->len;

	return g_string_free (uris, FALSE);
}


static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          user_data_or_owner)
{
	ClipboardData *clipboard_data = user_data_or_owner;

	if (gtk_targets_include_uri (&selection_data->target, 1)) {
		gtk_selection_data_set_uris (selection_data, clipboard_data->uris);
	}
	else if (gtk_targets_include_text (&selection_data->target, 1)) {
		char  *str;
		gsize  len;

		str = clipboard_data_convert_to_text (clipboard_data, FALSE, &len);
		gtk_selection_data_set_text (selection_data, str, len);
		g_free (str);
	}
	else if (selection_data->target == GNOME_COPIED_FILES) {
		char  *str;
		gsize  len;

		str = clipboard_data_convert_to_text (clipboard_data, TRUE, &len);
		gtk_selection_data_set (selection_data, GNOME_COPIED_FILES, 8, (guchar *) str, len);
		g_free (str);
	}
}


static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer      user_data_or_owner)
{
	ClipboardData *data = user_data_or_owner;

	g_strfreev (data->uris);
	g_free (data);
}


static void
_gth_browser_clipboard_copy_or_cut (GthBrowser *browser,
				    GList      *file_list,
				    gboolean    cut)
{
	ClipboardData  *data;
	GtkTargetList  *target_list;
	GtkTargetEntry *targets;
	int             n_targets;
	GList          *scan;
	int             i;

	data = g_new0 (ClipboardData, 1);
	data->cut = cut;
	data->n_uris = g_list_length (file_list);
	data->uris = g_new (char *, data->n_uris + 1);
	for (scan = file_list, i = 0; scan; scan = scan->next, i++) {
		GthFileData *file_data = scan->data;
		data->uris[i] = g_file_get_uri (file_data->file);
	}
	data->uris[data->n_uris] = NULL;

	target_list = gtk_target_list_new (NULL, 0);
	gtk_target_list_add (target_list, GNOME_COPIED_FILES, 0, 0);
	gtk_target_list_add_uri_targets (target_list, 0);
	gtk_target_list_add_text_targets (target_list, 0);
	targets = gtk_target_table_new_from_list (target_list, &n_targets);
	gtk_clipboard_set_with_data (gtk_clipboard_get_for_display (gtk_widget_get_display (GTK_WIDGET (browser)), GDK_SELECTION_CLIPBOARD),
				     targets,
				     n_targets,
				     clipboard_get_cb,
				     clipboard_clear_cb,
				     data);

	gtk_target_list_unref (target_list);
	gtk_target_table_free (targets, n_targets);
}


void
gth_browser_activate_action_edit_cut_files (GtkAction  *action,
					    GthBrowser *browser)
{
	GList *items;
	GList *file_list;

	items = gth_file_selection_get_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));
	file_list = gth_file_list_get_files (GTH_FILE_LIST (gth_browser_get_file_list (browser)), items);
	_gth_browser_clipboard_copy_or_cut (browser, file_list, TRUE);

	_g_object_list_unref (file_list);
	_gtk_tree_path_list_free (items);
}


void
gth_browser_activate_action_edit_copy_files (GtkAction  *action,
					     GthBrowser *browser)
{
	GList *items;
	GList *file_list;

	items = gth_file_selection_get_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));
	file_list = gth_file_list_get_files (GTH_FILE_LIST (gth_browser_get_file_list (browser)), items);
	_gth_browser_clipboard_copy_or_cut (browser, file_list, FALSE);

	_g_object_list_unref (file_list);
	_gtk_tree_path_list_free (items);
}


/* -- gth_browser_clipboard_paste -- */


typedef struct {
	GthBrowser    *browser;
	GthFileData   *destination;
	GthFileSource *file_source;
	GList         *files;
	gboolean       cut;
} PasteData;


static void
paste_data_free (PasteData *paste_data)
{
	_g_object_list_unref (paste_data->files);
	_g_object_unref (paste_data->file_source);
	g_object_unref (paste_data->destination);
	g_object_unref (paste_data->browser);
	g_free (paste_data);
}


static void
clipboard_received_cb (GtkClipboard     *clipboard,
		       GtkSelectionData *selection_data,
		       gpointer          user_data)
{
	PasteData   *paste_data = user_data;
	GthBrowser  *browser = paste_data->browser;
	char       **clipboard_data;
	int          i;
	GthTask     *task;

	clipboard_data = g_strsplit_set ((const char *) gtk_selection_data_get_data (selection_data), "\n\r", -1);
	if (clipboard_data[0] == NULL) {
		g_strfreev (clipboard_data);
		paste_data_free (paste_data);
		return;
	}

	paste_data->cut = strcmp (clipboard_data[0], "cut") == 0;
	paste_data->files = NULL;
	for (i = 1; clipboard_data[i] != NULL; i++)
		if (strcmp (clipboard_data[i], "") != 0)
			paste_data->files = g_list_prepend (paste_data->files, g_file_new_for_uri (clipboard_data[i]));
	paste_data->files = g_list_reverse (paste_data->files);
	paste_data->file_source = gth_main_get_file_source (paste_data->destination->file);

	if (paste_data->cut && ! gth_file_source_can_cut (paste_data->file_source)) {
		GtkWidget *dialog;
		int        response;

		dialog = _gtk_message_dialog_new (GTK_WINDOW (browser),
						  GTK_DIALOG_MODAL,
						  GTK_STOCK_DIALOG_QUESTION,
						  _("Could not move the files"),
						  _("Files cannot be moved to the current location, as alternative you can choose to copy them."),
						  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						  GTK_STOCK_COPY, GTK_RESPONSE_OK,
						  NULL);
		response = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		if (response == GTK_RESPONSE_CANCEL) {
			paste_data_free (paste_data);
			return;
		}

		paste_data->cut = FALSE;
	}

	task = gth_copy_task_new (paste_data->file_source,
				  paste_data->destination,
				  paste_data->cut,
				  paste_data->files);
	gth_browser_exec_task (browser, task, FALSE);

	g_object_unref (task);
	paste_data_free (paste_data);
}


void
gth_browser_activate_action_edit_paste (GtkAction  *action,
					GthBrowser *browser)
{
	PasteData *paste_data;

	paste_data = g_new0 (PasteData, 1);
	paste_data->browser = g_object_ref (browser);
	paste_data->destination = g_object_ref (gth_browser_get_location_data (browser));

	gtk_clipboard_request_contents (gtk_widget_get_clipboard (GTK_WIDGET (browser), GDK_SELECTION_CLIPBOARD),
					GNOME_COPIED_FILES,
					clipboard_received_cb,
					paste_data);
}


void
gth_browser_activate_action_edit_duplicate (GtkAction  *action,
					    GthBrowser *browser)
{
	GList   *items;
	GList   *file_list;
	GthTask *task;

	items = gth_file_selection_get_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));
	file_list = gth_file_list_get_files (GTH_FILE_LIST (gth_browser_get_file_list (browser)), items);
	task = gth_duplicate_task_new (file_list);
	gth_browser_exec_task (browser, task, FALSE);

	g_object_unref (task);
	_g_object_list_unref (file_list);
	_gtk_tree_path_list_free (items);
}


static void
notify_files_delete (GtkWindow *window,
		     GList     *files)
{
	GFile *parent;

	parent = g_file_get_parent ((GFile*) files->data);
	gth_monitor_folder_changed (gth_main_get_default_monitor (),
				    parent,
				    files,
				    GTH_MONITOR_EVENT_DELETED);

	g_object_unref (parent);
}


static void
delete_file_permanently (GtkWindow *window,
			 GList     *file_list)
{
	GList  *files;
	GError *error = NULL;

	files = gth_file_data_list_to_file_list (file_list);
	if (! _g_delete_files (files, TRUE, &error))
		_gtk_error_dialog_from_gerror_show (window, _("Could not delete the files"), &error);
	else
		notify_files_delete (window, files);

	_g_object_list_unref (files);
}


static void
delete_permanently_response_cb (GtkDialog *dialog,
				int        response_id,
				gpointer   user_data)
{
	GList *file_list = user_data;

	if (response_id == GTK_RESPONSE_YES)
		delete_file_permanently (gtk_window_get_transient_for (GTK_WINDOW (dialog)), file_list);

	gtk_widget_destroy (GTK_WIDGET (dialog));
	_g_object_list_unref (file_list);
}


static void
trash_files (GtkWindow *window,
	     GList     *file_list)
{
	GList    *scan;
	gboolean  moved_to_trash = TRUE;
	GError   *error = NULL;

	for (scan = file_list; scan; scan = scan->next) {
		GthFileData *file_data = scan->data;

		if (! g_file_trash (file_data->file, NULL, &error)) {
			moved_to_trash = FALSE;
			if (g_error_matches (error, G_IO_ERROR,  G_IO_ERROR_NOT_SUPPORTED)) {
				GtkWidget *d;

				g_clear_error (&error);

				d = _gtk_yesno_dialog_new (window,
							   GTK_DIALOG_MODAL,
							   _("The files cannot be moved to the Trash. Do you want to delete them permanently?"),
							   GTK_STOCK_CANCEL,
							   GTK_STOCK_DELETE);
				g_signal_connect (d,
						  "response",
						  G_CALLBACK (delete_permanently_response_cb),
						  gth_file_data_list_dup (file_list));
				gtk_widget_show (d);

				break;
			}
			_gtk_error_dialog_from_gerror_show (window, _("Could not move the files to the Trash"), &error);
			break;
		}
	}

	if (moved_to_trash) {
		GList  *files;

		files = gth_file_data_list_to_file_list (file_list);
		notify_files_delete (window, files);

		_g_object_list_unref (files);
	}
}


static void
trash_files_response_cb (GtkDialog *dialog,
			 int        response_id,
			 gpointer   user_data)
{
	GList *file_list = user_data;

	if (response_id == GTK_RESPONSE_YES)
		trash_files (gtk_window_get_transient_for (GTK_WINDOW (dialog)), file_list);

	gtk_widget_destroy (GTK_WIDGET (dialog));
	_g_object_list_unref (file_list);
}


void
gth_browser_activate_action_edit_trash (GtkAction  *action,
					GthBrowser *browser)
{
	GList *items;
	GList *file_list = NULL;

	items = gth_file_selection_get_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));
	file_list = gth_file_list_get_files (GTH_FILE_LIST (gth_browser_get_file_list (browser)), items);

	if (eel_gconf_get_boolean (PREF_MSG_CONFIRM_DELETION, DEFAULT_MSG_CONFIRM_DELETION)) {
		int        file_count;
		char      *prompt;
		GtkWidget *d;

		file_count = g_list_length (file_list);
		if (file_count == 1) {
			GthFileData *file_data = file_list->data;
			prompt = g_strdup_printf (_("Are you sure you want to move \"%s\" to trash?"), g_file_info_get_display_name (file_data->info));
		}
		else
			prompt = g_strdup_printf (ngettext("Are you sure you want to move to trash "
							   "the %'d selected file?",
							   "Are you sure you want to move to trash "
							   "the %'d selected files?", file_count),
						  file_count);

		d = _gtk_message_dialog_new (GTK_WINDOW (browser),
					     GTK_DIALOG_MODAL,
					     GTK_STOCK_DIALOG_QUESTION,
					     prompt,
					     NULL,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     _("Mo_ve to Trash"), GTK_RESPONSE_YES,
					     NULL);
		g_signal_connect (d, "response", G_CALLBACK (trash_files_response_cb), file_list);
		gtk_widget_show (d);

		g_free (prompt);
	}
	else {
		trash_files (GTK_WINDOW (browser), file_list);
		_g_object_list_unref (file_list);
	}

	_gtk_tree_path_list_free (items);
}


void
gth_browser_activate_action_edit_delete (GtkAction  *action,
					 GthBrowser *browser)
{
	GList     *items;
	GList     *file_list;
	int        file_count;
	char      *prompt;
	GtkWidget *d;

	items = gth_file_selection_get_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));
	file_list = gth_file_list_get_files (GTH_FILE_LIST (gth_browser_get_file_list (browser)), items);

	file_count = g_list_length (file_list);
	if (file_count == 1) {
		GthFileData *file_data = file_list->data;
		prompt = g_strdup_printf (_("Are you sure you want to permanently delete \"%s\"?"), g_file_info_get_display_name (file_data->info));
	}
	else
		prompt = g_strdup_printf (ngettext("Are you sure you want to permanently delete "
						   "the %'d selected file?",
						   "Are you sure you want to permanently delete "
					  	   "the %'d selected files?", file_count),
					  file_count);

	d = _gtk_message_dialog_new (GTK_WINDOW (browser),
				     GTK_DIALOG_MODAL,
				     GTK_STOCK_DIALOG_QUESTION,
				     prompt,
				     _("If you delete a file, it will be permanently lost."),
				     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				     GTK_STOCK_DELETE, GTK_RESPONSE_YES,
				     NULL);
	g_signal_connect (d, "response", G_CALLBACK (delete_permanently_response_cb), file_list);
	gtk_widget_show (d);

	g_free (prompt);
	_gtk_tree_path_list_free (items);
}


void
gth_browser_activate_action_folder_open_in_file_manager (GtkAction  *action,
						         GthBrowser *browser)
{
	GthFileData *file_data;
	char        *uri;
	GError      *error = NULL;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	uri = g_file_get_uri (file_data->file);
	if (! gtk_show_uri (gtk_window_get_screen (GTK_WINDOW (browser)),
			    uri,
                            gtk_get_current_event_time (),
                            &error))
	{
		_gtk_error_dialog_from_gerror_run (GTK_WINDOW (browser), _("Could not open the location"), &error);
	}

	g_free (uri);
	g_object_unref (file_data);
}


void
gth_browser_activate_action_folder_create (GtkAction  *action,
					   GthBrowser *browser)
{
	GthFileData *parent;

	parent = gth_browser_get_folder_popup_file_data (browser);
	if (parent != NULL) {
		_gth_browser_create_new_folder (browser, parent->file);
		g_object_unref (parent);
	}
}


void
gth_browser_activate_action_folder_rename (GtkAction  *action,
					   GthBrowser *browser)
{
	GthFileData *file_data;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	gth_folder_tree_start_editing (GTH_FOLDER_TREE (gth_browser_get_folder_tree (browser)), file_data->file);

	g_object_unref (file_data);
}


void
gth_browser_activate_action_folder_cut (GtkAction  *action,
					GthBrowser *browser)
{
	GthFileData *file_data;
	GList       *file_list;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	file_list = g_list_prepend (NULL, file_data);
	_gth_browser_clipboard_copy_or_cut (browser, file_list, TRUE);

	g_list_free (file_list);
}


void
gth_browser_activate_action_folder_copy (GtkAction  *action,
					 GthBrowser *browser)
{
	GthFileData *file_data;
	GList       *file_list;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	file_list = g_list_prepend (NULL, file_data);
	_gth_browser_clipboard_copy_or_cut (browser, file_list, FALSE);

	_g_object_list_unref (file_list);
}


void
gth_browser_activate_action_folder_paste (GtkAction  *action,
					  GthBrowser *browser)
{
	GthFileData *file_data;
	PasteData   *paste_data;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	paste_data = g_new0 (PasteData, 1);
	paste_data->browser = g_object_ref (browser);
	paste_data->destination = gth_file_data_dup (file_data);

	gtk_clipboard_request_contents (gtk_widget_get_clipboard (GTK_WIDGET (browser), GDK_SELECTION_CLIPBOARD),
					GNOME_COPIED_FILES,
					clipboard_received_cb,
					paste_data);

	g_object_unref (file_data);
}


/* -- gth_browser_activate_action_folder_trash -- */


typedef struct {
	GthBrowser  *browser;
	GthFileData *file_data;
} DeleteFolderData;


static void
delete_data_free (DeleteFolderData *delete_data)
{
	_g_object_unref (delete_data->browser);
	_g_object_unref (delete_data->file_data);
	g_free (delete_data);
}


static void
delete_folder_permanently (GtkWindow        *window,
			   DeleteFolderData *delete_data)
{
	GthFileData *file_data = delete_data->file_data;
	GError      *error = NULL;
	GList       *files;

	files = g_list_prepend (NULL, file_data->file);
	if (! _g_delete_files (files, TRUE, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY)) {
			GtkWidget *d;
			int        response;

			d = _gtk_yesno_dialog_new (GTK_WINDOW (delete_data->browser),
					           GTK_DIALOG_MODAL,
					           _("The folder is not empty, do you want to delete the folder and its content permanently?"),
					           GTK_STOCK_CANCEL,
					           GTK_STOCK_DELETE);
			response = gtk_dialog_run (GTK_DIALOG (d));
			if (response == GTK_RESPONSE_YES) {
				GthTask *task;

				task = gth_delete_task_new (files);
				gth_browser_exec_task (delete_data->browser, task, FALSE);

				g_object_unref (task);
			}

			gtk_widget_destroy (d);
		}
		else
			_gtk_error_dialog_from_gerror_show (window, _("Could not delete the folder"), &error);
	}
	else {
		GFile *parent;

		parent = g_file_get_parent (file_data->file);
		gth_monitor_folder_changed (gth_main_get_default_monitor (),
					    parent,
					    files,
					    GTH_MONITOR_EVENT_DELETED);

		g_object_unref (parent);
	}

	g_list_free (files);
	delete_data_free (delete_data);
}


static void
delete_folder_permanently_response_cb (GtkDialog *dialog,
				       int        response_id,
				       gpointer   user_data)
{
	DeleteFolderData *delete_data = user_data;

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (response_id == GTK_RESPONSE_YES)
		delete_folder_permanently (GTK_WINDOW (delete_data->browser), delete_data);
	else
		delete_data_free (delete_data);
}


void
gth_browser_activate_action_folder_trash (GtkAction  *action,
					  GthBrowser *browser)
{
	GthFileData *file_data;
	GError      *error = NULL;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	if (! g_file_trash (file_data->file, NULL, &error)) {
		if (g_error_matches (error, G_IO_ERROR,  G_IO_ERROR_NOT_SUPPORTED)) {
			DeleteFolderData *delete_data;
			GtkWidget       *d;

			g_clear_error (&error);

			delete_data = g_new0 (DeleteFolderData, 1);
			delete_data->browser = g_object_ref (browser);
			delete_data->file_data = g_object_ref (file_data);

			d = _gtk_yesno_dialog_new (GTK_WINDOW (browser),
						   GTK_DIALOG_MODAL,
						   _("The folder cannot be moved to the Trash. Do you want to delete it permanently?"),
						   GTK_STOCK_CANCEL,
						   GTK_STOCK_DELETE);
			g_signal_connect (d, "response", G_CALLBACK (delete_folder_permanently_response_cb), delete_data);
			gtk_widget_show (d);
		}
		else
			_gtk_error_dialog_from_gerror_show (GTK_WINDOW (browser), _("Could not move the folder to the Trash"), &error);
	}
	else {
		GFile *parent;
		GList *files;

		parent = g_file_get_parent (file_data->file);
		files = g_list_prepend (NULL, file_data->file);
		gth_monitor_folder_changed (gth_main_get_default_monitor (),
					    parent,
					    files,
					    GTH_MONITOR_EVENT_DELETED);

		g_list_free (files);
		g_object_unref (parent);
	}

	_g_object_unref (file_data);
}


void
gth_browser_activate_action_folder_delete (GtkAction  *action,
					   GthBrowser *browser)
{
	GthFileData      *file_data;
	char             *prompt;
	DeleteFolderData *delete_data;
	GtkWidget        *d;

	file_data = gth_browser_get_folder_popup_file_data (browser);
	if (file_data == NULL)
		return;

	prompt = g_strdup_printf (_("Are you sure you want to permanently delete \"%s\"?"), g_file_info_get_display_name (file_data->info));

	delete_data = g_new0 (DeleteFolderData, 1);
	delete_data->browser = g_object_ref (browser);
	delete_data->file_data = g_object_ref (file_data);

	d = _gtk_message_dialog_new (GTK_WINDOW (browser),
				     GTK_DIALOG_MODAL,
				     GTK_STOCK_DIALOG_QUESTION,
				     prompt,
				     _("If you delete a file, it will be permanently lost."),
				     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				     GTK_STOCK_DELETE, GTK_RESPONSE_YES,
				     NULL);
	g_signal_connect (d, "response", G_CALLBACK (delete_folder_permanently_response_cb), delete_data);
	gtk_widget_show (d);

	g_free (prompt);
}
