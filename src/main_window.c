/*
 * Copyright (c) 2014-2015 gnome-mpv
 *
 * This file is part of GNOME MPV.
 *
 * GNOME MPV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GNOME MPV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNOME MPV.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>

#include "def.h"
#include "common.h"
#include "playlist_widget.h"
#include "main_window.h"
#include "control_box.h"

enum
{
	PROP_0,
	PROP_USE_OPENGL,
	N_PROPERTIES
};

struct _MainWindowPrivate
{
	gboolean use_opengl;
};

static void vid_area_init(MainWindow *wnd, gboolean use_opengl);
static GtkWidget *vid_area_new(gboolean use_opengl);
static gboolean timeout_handler(gpointer data);
static gboolean finalize_load_state(gpointer data);
static GMenu *menu_btn_build_menu(void);
static GMenu *open_btn_build_menu(void);

G_DEFINE_TYPE_WITH_PRIVATE(MainWindow, main_window, GTK_TYPE_APPLICATION_WINDOW)

static void main_window_set_property(	GObject *object,
					guint property_id,
					const GValue *value,
					GParamSpec *pspec )
{
	MainWindow *self = MAIN_WINDOW(object);

	/* PROP_USE_OPENGL can only be set once during construction */
	if(property_id == PROP_USE_OPENGL)
	{
		self->priv->use_opengl = g_value_get_boolean(value);

		vid_area_init(self, self->priv->use_opengl);
	}
	else
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void main_window_get_property(	GObject *object,
					guint property_id,
					GValue *value,
					GParamSpec *pspec )
{
	MainWindow *self = MAIN_WINDOW(object);

	if(property_id == PROP_USE_OPENGL)
	{
		g_value_set_boolean(value, self->priv->use_opengl);
	}
	else
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static gboolean fs_control_enter_handler(	GtkWidget *widget,
						GdkEvent *event,
						gpointer data )
{
	MAIN_WINDOW(data)->fs_control_hover = TRUE;

	return FALSE;
}

static gboolean fs_control_leave_handler(	GtkWidget *widget,
						GdkEvent *event,
						gpointer data )
{
	MAIN_WINDOW(data)->fs_control_hover = FALSE;

	return FALSE;
}

static gboolean motion_notify_handler(GtkWidget *widget, GdkEventMotion *event)
{
	MainWindow *wnd = MAIN_WINDOW(widget);
	GdkCursor *cursor;

	cursor = gdk_cursor_new_for_display(	gdk_display_get_default(),
						GDK_ARROW );

	gdk_window_set_cursor
		(gtk_widget_get_window(GTK_WIDGET(wnd->vid_area)), cursor);

	if(wnd->fullscreen)
	{
		gtk_widget_show(wnd->control_box);
	}

	if(wnd->timeout_tag > 0)
	{
		g_source_remove(wnd->timeout_tag);
	}

	wnd->timeout_tag = g_timeout_add_seconds(	FS_CONTROL_HIDE_DELAY,
							timeout_handler,
							wnd );

	return	GTK_WIDGET_CLASS(main_window_parent_class)
		->motion_notify_event(widget, event);
}

static gboolean configure_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data )
{
	MainWindow *wnd = data;
	GdkEventConfigure *conf_event = (GdkEventConfigure *)event;
	guint signal_id = g_signal_lookup("configure-event", MAIN_WINDOW_TYPE);

	if(wnd->init_width == conf_event->width)
	{
		g_signal_handlers_disconnect_matched(	wnd,
							G_SIGNAL_MATCH_ID
							|G_SIGNAL_MATCH_DATA,
							signal_id,
							0,
							0,
							NULL,
							data);

		g_idle_add((GSourceFunc)finalize_load_state, data);
	}

	return FALSE;
}

static void vid_area_init(MainWindow *wnd, gboolean use_opengl)
{
	/* vid_area cannot be initialized more than once */
	if(!wnd->vid_area)
	{
		GtkStyleContext *style_context;

		wnd->vid_area =	vid_area_new(use_opengl);
		style_context = gtk_widget_get_style_context(wnd->vid_area);

		gtk_widget_add_events(wnd->vid_area, GDK_BUTTON_PRESS_MASK);
		gtk_style_context_add_class(style_context, "gmpv-vid-area");

		gtk_container_add(	GTK_CONTAINER(wnd->vid_area_overlay),
					wnd->vid_area );

		gtk_paned_pack1(	GTK_PANED(wnd->vid_area_paned),
					wnd->vid_area_overlay,
					TRUE,
					TRUE );
	}
}

static GtkWidget *vid_area_new(gboolean use_opengl)
{
#ifdef OPENGL_CB_ENABLED
	return use_opengl?gtk_gl_area_new():gtk_drawing_area_new();
#else
	return gtk_drawing_area_new();
#endif
}

static gboolean timeout_handler(gpointer data)
{
	MainWindow *wnd;
	ControlBox *control_box;
	GtkScaleButton *button;
	GtkWidget *popup;

	wnd = data;
	control_box = CONTROL_BOX(wnd->control_box);
	button = GTK_SCALE_BUTTON(control_box->volume_button);
	popup = gtk_scale_button_get_popup(button);


	if(wnd->fullscreen
	&& !wnd->fs_control_hover
	&& !gtk_widget_is_visible(popup))
	{
		GdkWindow *window;
		GdkCursor *cursor;

		window = gtk_widget_get_window(GTK_WIDGET(wnd->vid_area));

		cursor = gdk_cursor_new_for_display
				(gdk_display_get_default(), GDK_BLANK_CURSOR);

		gdk_window_set_cursor(window, cursor);
		gtk_widget_hide(wnd->control_box);
	}

	wnd->timeout_tag = 0;

	return FALSE;
}

static gboolean finalize_load_state(gpointer data)
{
	MainWindow *wnd = data;

	gtk_paned_set_position(	GTK_PANED(wnd->vid_area_paned),
				wnd->init_width-wnd->playlist_width );

	if(wnd->init_playlist_visible != main_window_get_playlist_visible(wnd))
	{
		wnd->playlist_visible = wnd->init_playlist_visible;

		gtk_widget_set_visible(	wnd->playlist,
					wnd->init_playlist_visible );
	}

	return FALSE;
}

static GMenu *menu_btn_build_menu()
{
	GMenu *menu;
	GMenu *playlist_submenu;
	GMenu *sub_submenu;
	GMenu *view_submenu;
	GMenuItem *playlist_section;
	GMenuItem *sub_section;
	GMenuItem *view_section;
	GMenuItem *load_sub_menu_item;
	GMenuItem *playlist_toggle_menu_item;
	GMenuItem *playlist_save_menu_item;
	GMenuItem *normal_size_menu_item;
	GMenuItem *double_size_menu_item;
	GMenuItem *half_size_menu_item;

	menu = g_menu_new();
	playlist_submenu = g_menu_new();
	sub_submenu = g_menu_new();
	view_submenu = g_menu_new();

	playlist_section
		= g_menu_item_new_section(NULL, G_MENU_MODEL(playlist_submenu));

	sub_section
		= g_menu_item_new_section(NULL, G_MENU_MODEL(sub_submenu));

	view_section
		= g_menu_item_new_section(NULL, G_MENU_MODEL(view_submenu));

	playlist_toggle_menu_item
		= g_menu_item_new(_("_Toggle Playlist"), "app.playlist_toggle");

	playlist_save_menu_item
		= g_menu_item_new(_("_Save Playlist"), "app.playlist_save");

	load_sub_menu_item
		= g_menu_item_new(_("_Load Subtitle"), "app.loadsub");

	normal_size_menu_item
		= g_menu_item_new(_("_Normal Size"), "app.normalsize");

	double_size_menu_item
		= g_menu_item_new(_("_Double Size"), "app.doublesize");

	half_size_menu_item
		= g_menu_item_new(_("_Half Size"), "app.halfsize");

	g_menu_append_item(playlist_submenu, playlist_toggle_menu_item);
	g_menu_append_item(playlist_submenu, playlist_save_menu_item);
	g_menu_append_item(sub_submenu, load_sub_menu_item);
	g_menu_append_item(view_submenu, normal_size_menu_item);
	g_menu_append_item(view_submenu, double_size_menu_item);
	g_menu_append_item(view_submenu, half_size_menu_item);

	g_menu_append_item(menu, playlist_section);
	g_menu_append_item(menu, sub_section);
	g_menu_append_item(menu, view_section);

	return menu;
}

static GMenu *open_btn_build_menu()
{
	GMenu *menu;
	GMenuItem *open_menu_item;
	GMenuItem *open_loc_menu_item;

	menu = g_menu_new();

	open_menu_item
		= g_menu_item_new(_("_Open"), "app.open(false)");

	open_loc_menu_item
		= g_menu_item_new(_("Open _Location"), "app.openloc");

	g_menu_append_item(menu, open_menu_item);
	g_menu_append_item(menu, open_loc_menu_item);

	return menu;
}

void main_window_save_state(MainWindow *wnd)
{
	GSettings *settings;
	gint width;
	gint height;
	gint handle_pos;
	gdouble volume;

	settings = g_settings_new(CONFIG_WIN_STATE);
	handle_pos = gtk_paned_get_position(GTK_PANED(wnd->vid_area_paned));
	volume = control_box_get_volume(CONTROL_BOX(wnd->control_box));

	gtk_window_get_size(GTK_WINDOW(wnd), &width, &height);

	g_settings_set_int(settings, "width", width);
	g_settings_set_int(settings, "height", height);
	g_settings_set_double(settings, "volume", volume);

	g_settings_set_boolean(	settings,
				"show-playlist",
				wnd->playlist_visible );

	if(main_window_get_playlist_visible(wnd))
	{
		g_settings_set_int(	settings,
					"playlist-width",
					width-handle_pos );
	}
	else
	{
		g_settings_set_int(	settings,
					"playlist-width",
					wnd->playlist_width );
	}

	g_clear_object(&settings);
}

void main_window_load_state(MainWindow *wnd)
{
	GSettings *settings = g_settings_new(CONFIG_WIN_STATE);

	wnd->init_width = g_settings_get_int(settings, "width");
	wnd->init_height = g_settings_get_int(settings, "height");

	wnd->init_playlist_visible
		= g_settings_get_boolean(settings, "show-playlist");

	g_signal_connect(	wnd,
				"configure-event",
				G_CALLBACK(configure_handler),
				wnd );

	/* This is needed even when show_playlist==false to initialize some
	 * internal variables.
	 */
	main_window_set_playlist_visible(wnd, TRUE);

	wnd->playlist_width = g_settings_get_int(settings, "playlist-width");

	control_box_set_volume(	CONTROL_BOX(wnd->control_box),
				g_settings_get_double(settings, "volume") );

	gtk_window_resize(GTK_WINDOW(wnd), wnd->init_width, wnd->init_height);

	g_clear_object(&settings);
}

static void main_window_class_init(MainWindowClass *klass)
{
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);
	GtkWidgetClass *wgt_class = GTK_WIDGET_CLASS(klass);
	GParamSpec *pspec = NULL;

	obj_class->set_property = main_window_set_property;
	obj_class->get_property = main_window_get_property;
	wgt_class->motion_notify_event = motion_notify_handler;

	g_signal_new(	"mpv-init",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_RUN_FIRST,
			0,
			NULL,
			NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE,
			0 );

	g_signal_new(	"mpv-playback-restart",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_RUN_FIRST,
			0,
			NULL,
			NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE,
			0 );

	g_signal_new(	"mpv-prop-change",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_RUN_FIRST,
			0,
			NULL,
			NULL,
			g_cclosure_marshal_VOID__STRING,
			G_TYPE_NONE,
			1,
			G_TYPE_STRING );

	pspec = g_param_spec_boolean
		(	"use-opengl",
			"Use OpenGL",
			"Whether or not to set up video area for opengl-cb",
			FALSE,
			G_PARAM_CONSTRUCT_ONLY|G_PARAM_READWRITE );

	g_object_class_install_property(obj_class, PROP_USE_OPENGL, pspec);
}

static void main_window_init(MainWindow *wnd)
{
	/* wnd->vid_area will be initialized when use-opengl property is set */
	wnd->priv = main_window_get_instance_private(wnd);
	wnd->fullscreen = FALSE;
	wnd->playlist_visible = FALSE;
	wnd->fs_control_hover = FALSE;
	wnd->playlist_width = PLAYLIST_DEFAULT_WIDTH;
	wnd->timeout_tag = 0;
	wnd->init_width = MAIN_WINDOW_DEFAULT_WIDTH;
	wnd->init_height = MAIN_WINDOW_DEFAULT_HEIGHT;
	wnd->settings = gtk_settings_get_default();
	wnd->header_bar = gtk_header_bar_new();
	wnd->open_hdr_btn = NULL;
	wnd->fullscreen_hdr_btn = NULL;
	wnd->menu_hdr_btn = NULL;
	wnd->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	wnd->vid_area_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	wnd->vid_area_overlay = gtk_overlay_new();
	wnd->control_box = control_box_new();
	wnd->playlist = playlist_widget_new();

	gtk_widget_add_events(	wnd->vid_area_overlay,
				GDK_ENTER_NOTIFY_MASK
				|GDK_LEAVE_NOTIFY_MASK );

	gtk_header_bar_set_show_close_button(	GTK_HEADER_BAR(wnd->header_bar),
						TRUE );

	gtk_window_set_title(GTK_WINDOW(wnd), g_get_application_name());

	gtk_paned_set_position(	GTK_PANED(wnd->vid_area_paned),
				MAIN_WINDOW_DEFAULT_WIDTH
				-PLAYLIST_DEFAULT_WIDTH );

	gtk_window_set_default_size(	GTK_WINDOW(wnd),
					MAIN_WINDOW_DEFAULT_WIDTH,
					MAIN_WINDOW_DEFAULT_HEIGHT );

	gtk_box_pack_start
		(GTK_BOX(wnd->main_box), wnd->vid_area_paned, TRUE, TRUE, 0);

	gtk_paned_pack2
		(GTK_PANED(wnd->vid_area_paned), wnd->playlist, FALSE, TRUE);

	gtk_container_add
		(GTK_CONTAINER(wnd->main_box), wnd->control_box);

	gtk_container_add
		(GTK_CONTAINER(wnd), wnd->main_box);

	g_signal_connect(	wnd->vid_area_overlay,
				"enter-notify-event",
				G_CALLBACK(fs_control_enter_handler),
				wnd );

	g_signal_connect(	wnd->vid_area_overlay,
				"leave-notify-event",
				G_CALLBACK(fs_control_leave_handler),
				wnd );
}

GtkWidget *main_window_new(GtkApplication *app, gboolean use_opengl)
{
	return GTK_WIDGET(g_object_new(	main_window_get_type(),
					"application", app,
					"use-opengl", use_opengl,
					NULL ));
}

void main_window_toggle_fullscreen(MainWindow *wnd)
{
	ControlBox *control_box = CONTROL_BOX(wnd->control_box);
	GtkContainer* main_box = GTK_CONTAINER(wnd->main_box);
	GtkContainer *overlay = GTK_CONTAINER(wnd->vid_area_overlay);

	if(wnd->fullscreen)
	{
		gtk_widget_set_halign(wnd->control_box, GTK_ALIGN_FILL);
		gtk_widget_set_valign(wnd->control_box, GTK_ALIGN_FILL);
		gtk_widget_set_size_request(wnd->control_box, -1, -1);

		g_object_ref(wnd->control_box);
		gtk_container_remove(overlay, wnd->control_box);
		gtk_container_add(main_box, wnd->control_box);
		g_object_unref(wnd->control_box);

		control_box_set_fullscreen_state(control_box, FALSE);
		gtk_window_unfullscreen(GTK_WINDOW(wnd));
		gtk_widget_show(wnd->control_box);

		if(main_window_get_csd_enabled(wnd))
		{
			control_box_set_fullscreen_btn_visible
				(CONTROL_BOX(wnd->control_box), FALSE);
		}
		else
		{
			gtk_application_window_set_show_menubar
				(GTK_APPLICATION_WINDOW(wnd), TRUE);
		}

		if(wnd->playlist_visible)
		{
			gtk_widget_show(wnd->playlist);
		}

		wnd->fullscreen = FALSE;
	}
	else
	{
		GdkScreen *screen;
		GdkWindow *window;
		GdkRectangle monitor_geom;
		gint width;
		gint monitor;

		screen = gtk_window_get_screen(GTK_WINDOW(wnd));
		window = gtk_widget_get_window(GTK_WIDGET(wnd));
		monitor = gdk_screen_get_monitor_at_window(screen, window);

		gdk_screen_get_monitor_geometry(screen, monitor, &monitor_geom);

		width = monitor_geom.width/2;

		gtk_widget_set_halign(wnd->control_box, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(wnd->control_box, GTK_ALIGN_END);
		gtk_widget_set_size_request(wnd->control_box, width, -1);

		g_object_ref(wnd->control_box);
		gtk_container_remove(main_box, wnd->control_box);
		gtk_overlay_add_overlay(GTK_OVERLAY(overlay), wnd->control_box);
		g_object_unref(wnd->control_box);

		control_box_set_fullscreen_state(control_box, TRUE);
		gtk_window_fullscreen(GTK_WINDOW(wnd));
		gtk_window_present(GTK_WINDOW(wnd));
		gtk_widget_hide(wnd->control_box);
		timeout_handler(wnd);

		if(main_window_get_csd_enabled(wnd))
		{
			control_box_set_fullscreen_btn_visible
				(CONTROL_BOX(wnd->control_box), TRUE);
		}
		else
		{
			gtk_application_window_set_show_menubar
				(GTK_APPLICATION_WINDOW(wnd), FALSE);
		}

		if(wnd->playlist_visible)
		{
			gtk_widget_hide(wnd->playlist);
		}

		wnd->fullscreen = TRUE;
	}
}

void main_window_reset(MainWindow *wnd)
{
	gtk_window_set_title(GTK_WINDOW(wnd), g_get_application_name());
	control_box_reset_control(CONTROL_BOX(wnd->control_box));
}

gint main_window_get_width_margin(MainWindow *wnd)
{
	return	gtk_widget_get_allocated_width(GTK_WIDGET(wnd))
		- gtk_widget_get_allocated_width(wnd->vid_area);
}

gint main_window_get_height_margin(MainWindow *wnd)
{
	return	gtk_widget_get_allocated_height(GTK_WIDGET(wnd))
		- gtk_widget_get_allocated_height(wnd->vid_area);
}

gboolean main_window_get_use_opengl(MainWindow *wnd)
{
	return wnd->priv->use_opengl;
}

void main_window_enable_csd(MainWindow *wnd)
{
	GIcon *open_icon;
	GIcon *fullscreen_icon;
	GIcon *menu_icon;

	open_icon = g_themed_icon_new_with_default_fallbacks
				("list-add-symbolic");

	fullscreen_icon = g_themed_icon_new_with_default_fallbacks
				("view-fullscreen-symbolic");

	menu_icon = g_themed_icon_new_with_default_fallbacks
				("view-list-symbolic");

	wnd->playlist_width = PLAYLIST_DEFAULT_WIDTH+PLAYLIST_CSD_WIDTH_OFFSET;
	wnd->open_hdr_btn = gtk_menu_button_new();
	wnd->fullscreen_hdr_btn = gtk_button_new();
	wnd->menu_hdr_btn = gtk_menu_button_new();

	gtk_widget_set_can_focus(wnd->open_hdr_btn, FALSE);
	gtk_widget_set_can_focus(wnd->fullscreen_hdr_btn, FALSE);
	gtk_widget_set_can_focus(wnd->menu_hdr_btn, FALSE);

	gtk_button_set_image
		(	GTK_BUTTON(wnd->fullscreen_hdr_btn),
			gtk_image_new_from_gicon
				(fullscreen_icon, GTK_ICON_SIZE_MENU ));

	gtk_button_set_image
		(	GTK_BUTTON(wnd->open_hdr_btn),
			gtk_image_new_from_gicon
				(open_icon, GTK_ICON_SIZE_MENU ));

	gtk_button_set_image
		(	GTK_BUTTON(wnd->menu_hdr_btn),
			gtk_image_new_from_gicon
				(menu_icon, GTK_ICON_SIZE_MENU ));

	gtk_menu_button_set_menu_model
		(	GTK_MENU_BUTTON(wnd->open_hdr_btn),
			G_MENU_MODEL(open_btn_build_menu()) );

	gtk_menu_button_set_menu_model
		(	GTK_MENU_BUTTON(wnd->menu_hdr_btn),
			G_MENU_MODEL(menu_btn_build_menu()) );

	gtk_header_bar_pack_start
		(GTK_HEADER_BAR(wnd->header_bar), wnd->open_hdr_btn);

	gtk_header_bar_pack_end
		(GTK_HEADER_BAR(wnd->header_bar), wnd->menu_hdr_btn);

	gtk_header_bar_pack_end
		(GTK_HEADER_BAR(wnd->header_bar), wnd->fullscreen_hdr_btn);

	gtk_actionable_set_action_name
		(GTK_ACTIONABLE(wnd->fullscreen_hdr_btn), "app.fullscreen");

	gtk_paned_set_position(	GTK_PANED(wnd->vid_area_paned),
				MAIN_WINDOW_DEFAULT_WIDTH
				-PLAYLIST_DEFAULT_WIDTH
				-PLAYLIST_CSD_WIDTH_OFFSET );

	gtk_window_set_titlebar(GTK_WINDOW(wnd), wnd->header_bar);
	gtk_window_set_title(GTK_WINDOW(wnd), g_get_application_name());
}

gboolean main_window_get_csd_enabled(MainWindow *wnd)
{
	return	wnd->open_hdr_btn &&
		wnd->fullscreen_hdr_btn &&
		wnd->menu_hdr_btn;
}

void main_window_set_playlist_visible(MainWindow *wnd, gboolean visible)
{
	gint offset;
	gint handle_pos;
	gint width;
	gint height;

	offset = main_window_get_csd_enabled(wnd)?PLAYLIST_CSD_WIDTH_OFFSET:0;
	handle_pos = gtk_paned_get_position(GTK_PANED(wnd->vid_area_paned));

	gtk_window_get_size(GTK_WINDOW(wnd), &width, &height);

	if(!visible && wnd->playlist_visible)
	{
		wnd->playlist_width = width-handle_pos;
	}

	wnd->playlist_visible = visible;

	gtk_widget_set_visible(wnd->playlist, visible);

	/* For some unknown reason, width needs to be adjusted by some offset
	 * (50px) when CSD is enabled for the resulting size to be correct.
	 */
	gtk_window_resize(	GTK_WINDOW(wnd),
				visible
				?width+wnd->playlist_width-offset
				:handle_pos+offset,
				height );
}

gboolean main_window_get_playlist_visible(MainWindow *wnd)
{
	return gtk_widget_get_visible(GTK_WIDGET(wnd->playlist));
}
