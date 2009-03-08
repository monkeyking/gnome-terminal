/*
 * Copyright © 2001 Havoc Pennington
 * Copyright © 2002 Red Hat, Inc.
 * Copyright © 2007, 2008 Christian Persch
 *
 * Gnome-terminal is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Gnome-terminal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <libsn/sn-launchee.h>

#include "skey-popup.h"
#include "terminal-accels.h"
#include "terminal-app.h"
#include "terminal-encoding.h"
#include "terminal-intl.h"
#include "terminal-screen-container.h"
#include "terminal-tabs-menu.h"
#include "terminal-util.h"
#include "terminal-window.h"

struct _TerminalWindowPrivate
{
  GtkActionGroup *action_group;
  GtkUIManager *ui_manager;
  guint ui_id;

  GtkActionGroup *profiles_action_group;
  guint profiles_ui_id;

  GtkActionGroup *encodings_action_group;
  guint encodings_ui_id;

  TerminalTabsMenu *tabs_menu;

  TerminalScreenPopupInfo *popup_info;
  guint remove_popup_info_idle;

  GtkActionGroup *new_terminal_action_group;
  guint new_terminal_ui_id;

  GtkWidget *menubar;
  GtkWidget *notebook;
  guint terms;
  TerminalScreen *active_screen;
  int old_char_width;
  int old_char_height;
  void *old_geometry_widget; /* only used for pointer value as it may be freed */
  char *startup_id;

  guint menubar_visible : 1;
  guint use_default_menubar_visibility : 1;

  /* Compositing manager integration */
  guint have_argb_visual : 1;

  guint disposed : 1;
  guint present_on_insert : 1;
};

#define PROFILE_DATA_KEY "GT::Profile"

#define FILE_NEW_TERMINAL_TAB_UI_PATH     "/menubar/File/FileNewTabProfiles"
#define FILE_NEW_TERMINAL_WINDOW_UI_PATH  "/menubar/File/FileNewWindowProfiles"
#define SET_ENCODING_UI_PATH              "/menubar/Terminal/TerminalSetEncoding/EncodingsPH"
#define SET_ENCODING_ACTION_NAME_PREFIX   "TerminalSetEncoding"

#define PROFILES_UI_PATH        "/menubar/Terminal/TerminalProfiles"
#define PROFILES_POPUP_UI_PATH  "/Popup/PopupTerminalProfiles/ProfilesPH"

#define STOCK_NEW_WINDOW  "window-new"
#define STOCK_NEW_TAB     "tab-new"

static void terminal_window_init        (TerminalWindow      *window);
static void terminal_window_class_init  (TerminalWindowClass *klass);
static void terminal_window_dispose     (GObject             *object);
static void terminal_window_finalize    (GObject             *object);
static gboolean terminal_window_state_event (GtkWidget            *widget,
                                             GdkEventWindowState  *event);

static gboolean terminal_window_delete_event (GtkWidget *widget,
                                              GdkEvent *event,
                                              gpointer data);

static gboolean notebook_button_press_cb     (GtkWidget *notebook,
                                              GdkEventButton *event,
                                              TerminalWindow *window);
static gboolean notebook_popup_menu_cb       (GtkWidget *notebook,
                                              TerminalWindow *window);
static void notebook_page_selected_callback  (GtkWidget       *notebook,
                                              GtkNotebookPage *page,
                                              guint            page_num,
                                              TerminalWindow  *window);
static void notebook_page_added_callback     (GtkWidget       *notebook,
                                              GtkWidget       *container,
                                              guint            page_num,
                                              TerminalWindow  *window);
static void notebook_page_removed_callback   (GtkWidget       *notebook,
                                              GtkWidget       *container,
                                              guint            page_num,
                                              TerminalWindow  *window);

/* Menu action callbacks */
static void file_new_window_callback          (GtkAction *action,
                                               TerminalWindow *window);
static void file_new_tab_callback             (GtkAction *action,
                                               TerminalWindow *window);
static void file_close_window_callback        (GtkAction *action,
                                               TerminalWindow *window);
static void file_close_tab_callback           (GtkAction *action,
                                               TerminalWindow *window);
static void edit_copy_callback                (GtkAction *action,
                                               TerminalWindow *window);
static void edit_paste_callback               (GtkAction *action,
                                               TerminalWindow *window);
static void edit_keybindings_callback         (GtkAction *action,
                                               TerminalWindow *window);
static void edit_profiles_callback            (GtkAction *action,
                                               TerminalWindow *window);
static void edit_current_profile_callback     (GtkAction *action,
                                               TerminalWindow *window);
static void file_new_profile_callback         (GtkAction *action,
                                               TerminalWindow *window);
static void view_menubar_toggled_callback     (GtkToggleAction *action,
                                               TerminalWindow *window);
static void view_fullscreen_toggled_callback  (GtkToggleAction *action,
                                               TerminalWindow *window);
static void view_zoom_in_callback             (GtkAction *action,
                                               TerminalWindow *window);
static void view_zoom_out_callback            (GtkAction *action,
                                               TerminalWindow *window);
static void view_zoom_normal_callback         (GtkAction *action,
                                               TerminalWindow *window);
static void terminal_set_title_callback       (GtkAction *action,
                                               TerminalWindow *window);
static void terminal_add_encoding_callback    (GtkAction *action,
                                               TerminalWindow *window);
static void terminal_reset_callback           (GtkAction *action,
                                               TerminalWindow *window);
static void terminal_reset_clear_callback     (GtkAction *action,
                                               TerminalWindow *window);
static void tabs_next_tab_callback            (GtkAction *action,
                                               TerminalWindow *window);
static void tabs_previous_tab_callback        (GtkAction *action,
                                               TerminalWindow *window);
static void tabs_move_left_callback           (GtkAction *action,
                                               TerminalWindow *window);
static void tabs_move_right_callback          (GtkAction *action,
                                               TerminalWindow *window);
static void tabs_detach_tab_callback          (GtkAction *action,
                                               TerminalWindow *window);
static void help_contents_callback        (GtkAction *action,
                                           TerminalWindow *window);
static void help_about_callback           (GtkAction *action,
                                           TerminalWindow *window);

static gboolean find_larger_zoom_factor  (double  current,
                                          double *found);
static gboolean find_smaller_zoom_factor (double  current,
                                          double *found);

static void terminal_window_show (GtkWidget *widget);

static gboolean confirm_close_window (TerminalWindow *window);
static void
profile_set_callback (TerminalScreen *screen,
                      TerminalProfile *old_profile,
                      TerminalWindow *window);

G_DEFINE_TYPE (TerminalWindow, terminal_window, GTK_TYPE_WINDOW)

/* Menubar mnemonics & accel settings handling */

static void
app_setting_notify_cb (TerminalApp *app,
                       GParamSpec *pspec,
                       GdkScreen *screen)
{
  GtkSettings *settings;
  const char *prop_name;

  if (pspec)
    prop_name = pspec->name;
  else
    prop_name = NULL;

  settings = gtk_settings_get_for_screen (screen);

  if (!prop_name || prop_name == I_(TERMINAL_APP_ENABLE_MNEMONICS))
    {
      gboolean enable_mnemonics;

      g_object_get (app, TERMINAL_APP_ENABLE_MNEMONICS, &enable_mnemonics, NULL);
      g_object_set (settings, "gtk-enable-mnemonics", enable_mnemonics, NULL);
    }

  if (!prop_name || prop_name == I_(TERMINAL_APP_ENABLE_MENU_BAR_ACCEL))
    {
      /* const */ char *saved_menubar_accel;
      gboolean enable_menubar_accel;

      /* FIXME: Once gtk+ bug 507398 is fixed, use that to reset the property instead */
      /* Now this is a bad hack on so many levels. */
      saved_menubar_accel = g_object_get_data (G_OBJECT (settings), "GT::gtk-menu-bar-accel");
      if (!saved_menubar_accel)
        {
          g_object_get (settings, "gtk-menu-bar-accel", &saved_menubar_accel, NULL);
          g_object_set_data_full (G_OBJECT (settings), "GT::gtk-menu-bar-accel",
                                  saved_menubar_accel, (GDestroyNotify) g_free);
        }

      g_object_get (app, TERMINAL_APP_ENABLE_MENU_BAR_ACCEL, &enable_menubar_accel, NULL);
      if (enable_menubar_accel)
        g_object_set (settings, "gtk-menu-bar-accel", saved_menubar_accel, NULL);
      else
        g_object_set (settings, "gtk-menu-bar-accel", NULL, NULL);
    }
}

static void
app_setting_notify_destroy_cb (GdkScreen *screen)
{
  g_signal_handlers_disconnect_by_func (terminal_app_get (),
                                        G_CALLBACK (app_setting_notify_cb),
                                        screen);
}

/* utility functions */

static char *
escape_underscores (const char *name)
{
  GString *escaped_name;

  g_assert (name != NULL);

  /* Who'd use more that 4 underscores in a profile name... */
  escaped_name = g_string_sized_new (strlen (name) + 4 + 1);

  while (*name)
    {
      if (*name == '_')
        g_string_append (escaped_name, "__");
      else
        g_string_append_c (escaped_name, *name);
      name++;
    }

  return g_string_free (escaped_name, FALSE);
}

static int
find_tab_num_at_pos (GtkNotebook *notebook,
                     int screen_x, 
                     int screen_y)
{
  GtkPositionType tab_pos;
  int page_num = 0;
  GtkNotebook *nb = GTK_NOTEBOOK (notebook);
  GtkWidget *page;

  tab_pos = gtk_notebook_get_tab_pos (GTK_NOTEBOOK (notebook));

  if (GTK_NOTEBOOK (notebook)->first_tab == NULL)
    return -1;

  while ((page = gtk_notebook_get_nth_page (nb, page_num)))
    {
      GtkWidget *tab;
      int max_x, max_y, x_root, y_root;

      tab = gtk_notebook_get_tab_label (nb, page);
      g_return_val_if_fail (tab != NULL, -1);

      if (!GTK_WIDGET_MAPPED (GTK_WIDGET (tab)))
        {
          page_num++;
          continue;
        }

      gdk_window_get_origin (tab->window, &x_root, &y_root);

      max_x = x_root + tab->allocation.x + tab->allocation.width;
      max_y = y_root + tab->allocation.y + tab->allocation.height;

      if ((tab_pos == GTK_POS_TOP || tab_pos == GTK_POS_BOTTOM) && screen_x <= max_x)
        return page_num;

      if ((tab_pos == GTK_POS_LEFT || tab_pos == GTK_POS_RIGHT) && screen_y <= max_y)
        return page_num;

      page_num++;
    }

  return -1;
}

static void
position_menu_under_widget (GtkMenu *menu,
                            int *x,
                            int *y,
                            gboolean *push_in,
                            gpointer user_data)
{
  /* Adapted from gtktoolbar.c */
  GtkWidget *widget = GTK_WIDGET (user_data);
  GtkWidget *container;
  GtkRequisition req;
  GtkRequisition menu_req;
  GdkRectangle monitor;
  int monitor_num;
  GdkScreen *screen;

  container = gtk_widget_get_ancestor (widget, GTK_TYPE_CONTAINER);

  gtk_widget_size_request (widget, &req);
  gtk_widget_size_request (GTK_WIDGET (menu), &menu_req);

  screen = gtk_widget_get_screen (GTK_WIDGET (menu));
  monitor_num = gdk_screen_get_monitor_at_window (screen, widget->window);
  if (monitor_num < 0)
          monitor_num = 0;
  gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

  gdk_window_get_origin (widget->window, x, y);
  if (GTK_WIDGET_NO_WINDOW (widget))
    {
      *x += widget->allocation.x;
      *y += widget->allocation.y;
    }
  if (gtk_widget_get_direction (container) == GTK_TEXT_DIR_LTR) 
    *x += widget->allocation.width - req.width;
  else 
    *x += req.width - menu_req.width;

  if ((*y + widget->allocation.height + menu_req.height) <= monitor.y + monitor.height)
    *y += widget->allocation.height;
  else if ((*y - menu_req.height) >= monitor.y)
    *y -= menu_req.height;
  else if (monitor.y + monitor.height - (*y + widget->allocation.height) > *y)
    *y += widget->allocation.height;
  else
    *y -= menu_req.height;

  *push_in = FALSE;
}

static void
terminal_set_profile_toggled_callback (GtkToggleAction *action,
                                       TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalProfile *profile;

  if (!gtk_toggle_action_get_active (action))
    return;

  if (priv->active_screen == NULL)
    return;
  
  profile = g_object_get_data (G_OBJECT (action), PROFILE_DATA_KEY);
  g_assert (profile);

  if (_terminal_profile_get_forgotten (profile))
    return;

  g_signal_handlers_block_by_func (priv->active_screen, G_CALLBACK (profile_set_callback), window);
  terminal_screen_set_profile (priv->active_screen, profile);
  g_signal_handlers_unblock_by_func (priv->active_screen, G_CALLBACK (profile_set_callback), window);
}

static void
profile_visible_name_notify_cb (TerminalProfile *profile,
                                GParamSpec *pspec,
                                GtkAction *action)
{
  const char *visible_name;
  char *dot, *display_name;
  guint num;

  visible_name = terminal_profile_get_property_string (profile, TERMINAL_PROFILE_VISIBLE_NAME);
  display_name = escape_underscores (visible_name);

  dot = strchr (gtk_action_get_name (action), '.');
  if (dot != NULL)
    {
      char *free_me;

      num = g_ascii_strtoll (dot + 1, NULL, 10);

      free_me = display_name;
      if (num < 10)
        display_name = g_strdup_printf (_("_%d. %s"), num, display_name);
      else if (num < 36)
        display_name = g_strdup_printf (_("_%c. %s"), ('A' + num - 10), display_name);
      else
        free_me = NULL;

      g_free (free_me);
    }

  g_object_set (action, "label", display_name, NULL);
  g_free (display_name);
}

static void
disconnect_profiles_from_actions_in_group (GtkActionGroup *action_group)
{
  GList *actions, *l;

  actions = gtk_action_group_list_actions (action_group);
  for (l = actions; l != NULL; l = l->next)
    {
      GObject *action = G_OBJECT (l->data);
      TerminalProfile *profile;

      profile = g_object_get_data (action, PROFILE_DATA_KEY);
      if (!profile)
        continue;

      g_signal_handlers_disconnect_by_func (profile, G_CALLBACK (profile_visible_name_notify_cb), action);
    }
  g_list_free (actions);
}

static void
terminal_window_update_set_profile_menu_active_profile (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalProfile *new_active_profile;
  GList *actions, *l;

  if (!priv->profiles_action_group)
    return;

  if (!priv->active_screen)
    return;

  new_active_profile = terminal_screen_get_profile (priv->active_screen);

  actions = gtk_action_group_list_actions (priv->profiles_action_group);
  for (l = actions; l != NULL; l = l->next)
    {
      GObject *action = G_OBJECT (l->data);
      TerminalProfile *profile;

      profile = g_object_get_data (action, PROFILE_DATA_KEY);
      if (profile != new_active_profile)
        continue;

      g_signal_handlers_block_by_func (action, G_CALLBACK (terminal_set_profile_toggled_callback), window);
      gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
      g_signal_handlers_unblock_by_func (action, G_CALLBACK (terminal_set_profile_toggled_callback), window);
    }
  g_list_free (actions);
}

static void
terminal_window_update_set_profile_menu (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalProfile *active_profile;
  GtkActionGroup *action_group;
  GtkAction *action;
  GList *profiles, *p;
  GSList *group;
  guint n;
  gboolean single_profile;

  /* Remove the old UI */
  if (priv->profiles_ui_id != 0)
    {
      gtk_ui_manager_remove_ui (priv->ui_manager, priv->profiles_ui_id);
      priv->profiles_ui_id = 0;
    }

  if (priv->profiles_action_group != NULL)
    {
      disconnect_profiles_from_actions_in_group (priv->profiles_action_group);
      gtk_ui_manager_remove_action_group (priv->ui_manager,
                                          priv->profiles_action_group);
      priv->profiles_action_group = NULL;
    }

  profiles = terminal_app_get_profile_list (terminal_app_get ());

  action = gtk_action_group_get_action (priv->action_group, "TerminalProfiles");
  single_profile = !profiles || profiles->next == NULL; /* list length <= 1 */
  gtk_action_set_sensitive (action, !single_profile);
  if (profiles == NULL)
    return;

  if (priv->active_screen)
    active_profile = terminal_screen_get_profile (priv->active_screen);
  else
    active_profile = NULL;

  action_group = priv->profiles_action_group = gtk_action_group_new ("Profiles");
  gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, -1);
  g_object_unref (action_group);

  priv->profiles_ui_id = gtk_ui_manager_new_merge_id (priv->ui_manager);

  group = NULL;
  n = 0;
  for (p = profiles; p != NULL; p = p->next)
    {
      TerminalProfile *profile = (TerminalProfile *) p->data;
      GtkRadioAction *profile_action;
      char name[32];

      g_snprintf (name, sizeof (name), "TerminalSetProfile%u", n++);

      profile_action = gtk_radio_action_new (name,
                                             NULL,
                                             NULL,
                                             NULL,
                                             n);

      gtk_radio_action_set_group (profile_action, group);
      group = gtk_radio_action_get_group (profile_action);

      if (profile == active_profile)
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (profile_action), TRUE);

      g_object_set_data_full (G_OBJECT (profile_action),
                              PROFILE_DATA_KEY,
                              g_object_ref (profile),
                              (GDestroyNotify) g_object_unref);
      profile_visible_name_notify_cb (profile, NULL, GTK_ACTION (profile_action));
      g_signal_connect (profile, "notify::" TERMINAL_PROFILE_VISIBLE_NAME,
                        G_CALLBACK (profile_visible_name_notify_cb), profile_action);
      g_signal_connect (profile_action, "toggled",
                        G_CALLBACK (terminal_set_profile_toggled_callback), window);

      gtk_action_group_add_action (action_group, GTK_ACTION (profile_action));
      g_object_unref (profile_action);

      gtk_ui_manager_add_ui (priv->ui_manager, priv->profiles_ui_id,
                             PROFILES_UI_PATH,
                             name, name,
                             GTK_UI_MANAGER_MENUITEM, FALSE);
      gtk_ui_manager_add_ui (priv->ui_manager, priv->profiles_ui_id,
                             PROFILES_POPUP_UI_PATH,
                             name, name,
                             GTK_UI_MANAGER_MENUITEM, FALSE);
    }

  g_list_free (profiles);
}

static void
terminal_window_create_new_terminal_action (TerminalWindow *window,
                                            TerminalProfile *profile,
                                            const char *name,
                                            guint num,
                                            GCallback callback)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkAction *action;

  action = gtk_action_new (name, NULL, NULL, NULL);

  g_object_set_data_full (G_OBJECT (action),
                          PROFILE_DATA_KEY,
                          g_object_ref (profile),
                          (GDestroyNotify) g_object_unref);
  profile_visible_name_notify_cb (profile, NULL, action);
  g_signal_connect (profile, "notify::" TERMINAL_PROFILE_VISIBLE_NAME,
                    G_CALLBACK (profile_visible_name_notify_cb), action);
  g_signal_connect (action, "activate", callback, window);

  gtk_action_group_add_action (priv->new_terminal_action_group, action);
  g_object_unref (action);
}

static void
terminal_window_update_new_terminal_menus (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkActionGroup *action_group;
  GtkAction *action;
  GList *profiles, *p;
  guint n;
  gboolean have_single_profile;

  /* Remove the old UI */
  if (priv->new_terminal_ui_id != 0)
    {
      gtk_ui_manager_remove_ui (priv->ui_manager, priv->new_terminal_ui_id);
      priv->new_terminal_ui_id = 0;
    }

  if (priv->new_terminal_action_group != NULL)
    {
      disconnect_profiles_from_actions_in_group (priv->new_terminal_action_group);
      gtk_ui_manager_remove_action_group (priv->ui_manager,
                                          priv->new_terminal_action_group);
      priv->new_terminal_action_group = NULL;
    }

  profiles = terminal_app_get_profile_list (terminal_app_get ());
  have_single_profile = !profiles || !profiles->next;

  action = gtk_action_group_get_action (priv->action_group, "FileNewTab");
  gtk_action_set_visible (action, have_single_profile);
  action = gtk_action_group_get_action (priv->action_group, "FileNewWindow");
  gtk_action_set_visible (action, have_single_profile);

  if (have_single_profile)
    {
      g_list_free (profiles);
      return;
    }

  /* Now build the submenus */

  action_group = priv->new_terminal_action_group = gtk_action_group_new ("NewTerminal");
  gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, -1);
  g_object_unref (action_group);

  priv->new_terminal_ui_id = gtk_ui_manager_new_merge_id (priv->ui_manager);

  n = 0;
  for (p = profiles; p != NULL; p = p->next)
    {
      TerminalProfile *profile = (TerminalProfile *) p->data;
      char name[32];

      g_snprintf (name, sizeof (name), "FileNewTab.%u", n);
      terminal_window_create_new_terminal_action (window,
                                                  profile,
                                                  name,
                                                  n,
                                                  G_CALLBACK (file_new_tab_callback));

      gtk_ui_manager_add_ui (priv->ui_manager, priv->new_terminal_ui_id,
                             FILE_NEW_TERMINAL_TAB_UI_PATH,
                             name, name,
                             GTK_UI_MANAGER_MENUITEM, FALSE);

      g_snprintf (name, sizeof (name), "FileNewWindow.%u", n);
      terminal_window_create_new_terminal_action (window,
                                                  profile,
                                                  name,
                                                  n,
                                                  G_CALLBACK (file_new_window_callback));

      gtk_ui_manager_add_ui (priv->ui_manager, priv->new_terminal_ui_id,
                             FILE_NEW_TERMINAL_WINDOW_UI_PATH,
                             name, name,
                             GTK_UI_MANAGER_MENUITEM, FALSE);

      ++n;
    }

  g_list_free (profiles);
}

static void
terminal_set_encoding_callback (GtkToggleAction *action,
                                TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  const char *name, *charset;
  
  if (!gtk_toggle_action_get_active (action))
    return;

  if (priv->active_screen == NULL)
    return;

  name = gtk_action_get_name (GTK_ACTION (action));
  g_assert (g_str_has_prefix (name, SET_ENCODING_ACTION_NAME_PREFIX));
  charset = name + strlen (SET_ENCODING_ACTION_NAME_PREFIX);

  vte_terminal_set_encoding (VTE_TERMINAL (priv->active_screen), charset);
}

static void
terminal_window_update_encoding_menu (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkActionGroup *action_group;
  GSList *group;
  guint n;
  GSList *encodings, *l;
  const char *charset;

  /* Remove the old UI */
  if (priv->encodings_ui_id != 0)
    {
      gtk_ui_manager_remove_ui (priv->ui_manager, priv->encodings_ui_id);
      priv->encodings_ui_id = 0;
    }

  if (priv->encodings_action_group != NULL)
    {
      gtk_ui_manager_remove_action_group (priv->ui_manager,
                                          priv->encodings_action_group);
      priv->encodings_action_group = NULL;
    }

  action_group = priv->encodings_action_group = gtk_action_group_new ("Encodings");
  gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, -1);
  g_object_unref (action_group);

  priv->encodings_ui_id = gtk_ui_manager_new_merge_id (priv->ui_manager);

  if (priv->active_screen)
    charset = vte_terminal_get_encoding (VTE_TERMINAL (priv->active_screen));
  else
    charset = NULL;
  
  encodings = terminal_app_get_active_encodings (terminal_app_get ());

  group = NULL;
  n = 0;
  for (l = encodings; l != NULL; l = l->next)
    {
      TerminalEncoding *e = (TerminalEncoding *) l->data;
      GtkRadioAction *encoding_action;
      char name[128];
      char *display_name;
      
      g_snprintf (name, sizeof (name), SET_ENCODING_ACTION_NAME_PREFIX "%s", e->charset);
      display_name = g_strdup_printf ("%s (%s)", e->name, e->charset);

      encoding_action = gtk_radio_action_new (name,
                                              display_name,
                                              NULL,
                                              NULL,
                                              n);
      g_free (display_name);

      gtk_radio_action_set_group (encoding_action, group);
      group = gtk_radio_action_get_group (encoding_action);

      if (charset && strcmp (e->charset, charset) == 0)
        gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (encoding_action), TRUE);

      g_signal_connect (encoding_action, "toggled",
                        G_CALLBACK (terminal_set_encoding_callback), window);

      gtk_action_group_add_action (action_group, GTK_ACTION (encoding_action));
      g_object_unref (encoding_action);

      gtk_ui_manager_add_ui (priv->ui_manager, priv->encodings_ui_id,
                             SET_ENCODING_UI_PATH,
                             name, name,
                             GTK_UI_MANAGER_MENUITEM, FALSE);
    }

  g_slist_foreach (encodings, (GFunc) terminal_encoding_unref, NULL);
  g_slist_free (encodings);
}

static void
terminal_window_update_encoding_menu_active_encoding (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkAction *action;
  char name[128];

  if (!priv->active_screen)
    return;
  if (!priv->encodings_action_group)
    return;

  g_snprintf (name, sizeof (name), SET_ENCODING_ACTION_NAME_PREFIX "%s",
              vte_terminal_get_encoding (VTE_TERMINAL (priv->active_screen)));
  action = gtk_action_group_get_action (priv->encodings_action_group, name);
  if (!action)
    return;

  g_signal_handlers_block_by_func (action, G_CALLBACK (terminal_set_encoding_callback), window);
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
  g_signal_handlers_unblock_by_func (action, G_CALLBACK (terminal_set_encoding_callback), window);
}

/* Actions stuff */

static void
terminal_window_update_copy_sensitivity (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkAction *action;
  gboolean can_copy = FALSE;

  if (priv->active_screen)
    can_copy = vte_terminal_get_has_selection (VTE_TERMINAL (priv->active_screen));

  action = gtk_action_group_get_action (priv->action_group, "EditCopy");
  gtk_action_set_sensitive (action, can_copy);
}

static void
terminal_window_update_zoom_sensitivity (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalScreen *screen;
  GtkAction *action;
  double current, zoom;
  
  screen = priv->active_screen;
  if (screen == NULL)
    return;

  current = terminal_screen_get_font_scale (screen);

  action = gtk_action_group_get_action (priv->action_group, "ViewZoomIn");
  gtk_action_set_sensitive (action, find_smaller_zoom_factor (current, &zoom));
  action = gtk_action_group_get_action (priv->action_group, "ViewZoomIn");
  gtk_action_set_sensitive (action, find_larger_zoom_factor (current, &zoom));
}

static void
update_edit_menu_cb (GtkClipboard *clipboard,
                     GdkAtom *targets,
                     int n_targets,
                     TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkAction *action;
  gboolean can_paste, can_paste_uris;

  can_paste = targets != NULL && gtk_targets_include_text (targets, n_targets);
  can_paste_uris = targets != NULL && gtk_targets_include_uri (targets, n_targets);

  action = gtk_action_group_get_action (priv->action_group, "EditPaste");
  gtk_action_set_sensitive (action, can_paste);
  action = gtk_action_group_get_action (priv->action_group, "EditPasteURIPaths");
  gtk_action_set_visible (action, can_paste_uris);
  gtk_action_set_sensitive (action, can_paste_uris);

  /* Ref was added in gtk_clipboard_request_targets below */
  g_object_unref (window);
}

static void
edit_menu_activate_callback (GtkMenuItem *menuitem,
                             gpointer     user_data)
{
  TerminalWindow *window = (TerminalWindow *) user_data;
  GtkClipboard *clipboard;

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_request_targets (clipboard,
                                 (GtkClipboardTargetsReceivedFunc) update_edit_menu_cb,
                                 g_object_ref (window));
}

static void
terminal_window_update_tabs_menu_sensitivity (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkNotebook *notebook = GTK_NOTEBOOK (priv->notebook);
  GtkActionGroup *action_group = priv->action_group;
  GtkAction *action;
  int num_pages, page_num;
  gboolean not_first, not_last;

  if (priv->disposed)
    return;

  num_pages = gtk_notebook_get_n_pages (notebook);
  page_num = gtk_notebook_get_current_page (notebook);
  not_first = page_num > 0;
  not_last = page_num + 1 < num_pages;

#if 1
  /* NOTE: We always make next/prev actions sensitive except in
   * single-tab windows, so the corresponding shortcut key escape code
   * isn't sent to the terminal. See bug #453193 and bug #138609.
   * This also makes tab cycling work, bug #92139.
   * FIXME: Find a better way to do this.
   */
  action = gtk_action_group_get_action (action_group, "TabsPrevious");
  gtk_action_set_sensitive (action, num_pages > 1);
  action = gtk_action_group_get_action (action_group, "TabsNext");
  gtk_action_set_sensitive (action, num_pages > 1);
#else
  /* This would be correct, but see the comment above. */
  action = gtk_action_group_get_action (action_group, "TabsPrevious");
  gtk_action_set_sensitive (action, not_first);
  action = gtk_action_group_get_action (action_group, "TabsNext");
  gtk_action_set_sensitive (action, not_last);
#endif

  action = gtk_action_group_get_action (action_group, "TabsMoveLeft");
  gtk_action_set_sensitive (action, not_first);
  action = gtk_action_group_get_action (action_group, "TabsMoveRight");
  gtk_action_set_sensitive (action, not_last);
  action = gtk_action_group_get_action (action_group, "TabsDetach");
  gtk_action_set_sensitive (action, num_pages > 1);
  action = gtk_action_group_get_action (action_group, "FileCloseTab");
  gtk_action_set_sensitive (action, num_pages > 1);
}

static void
initialize_alpha_mode (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GdkScreen *screen;
  GdkColormap *colormap;

  /* FIXME: update the TerminalScreen's for this change! */
  
  screen = gtk_widget_get_screen (GTK_WIDGET (window));
  colormap = gdk_screen_get_rgba_colormap (screen);
  if (colormap != NULL && gdk_screen_is_composited (screen))
    {
      /* Set RGBA colormap if possible so VTE can use real alpha
       * channels for transparency. */

      gtk_widget_set_colormap(GTK_WIDGET (window), colormap);
      priv->have_argb_visual = TRUE;
    }
  else
    {
      priv->have_argb_visual = FALSE;
    }
}

gboolean
terminal_window_uses_argb_visual (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  return priv->have_argb_visual;
}

static void
update_tab_visibility (TerminalWindow *window,
                       int             change)
{
  TerminalWindowPrivate *priv = window->priv;
  gboolean show_tabs;
  guint num;

  num = gtk_notebook_get_n_pages (GTK_NOTEBOOK (priv->notebook));

  show_tabs = (num + change) > 1;
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (priv->notebook), show_tabs);
}

static GtkNotebook *
handle_tab_droped_on_desktop (GtkNotebook *source_notebook,
                              GtkWidget   *container,
                              gint         x,
                              gint         y,
                              gpointer     data)
{
  TerminalScreen *screen;
  TerminalWindow *source_window;
  TerminalWindow *new_window;
  TerminalWindowPrivate *new_priv;

  screen = terminal_screen_container_get_screen (container);
  source_window = TERMINAL_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source_notebook)));
  g_return_val_if_fail (TERMINAL_IS_WINDOW (source_window), NULL);

  new_window = terminal_app_new_window (terminal_app_get (),
                                        gtk_widget_get_screen (GTK_WIDGET (source_window)));
  new_priv = new_window->priv;
  new_priv->present_on_insert = TRUE;

  update_tab_visibility (source_window, -1);
  update_tab_visibility (new_window, +1);

  return GTK_NOTEBOOK (new_priv->notebook);
}

/* Terminal screen popup menu handling */

static void
popup_open_url_callback (GtkAction *action,
                         TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalScreenPopupInfo *info = priv->popup_info;

  if (info == NULL)
    return;

  terminal_util_open_url (GTK_WIDGET (window), info->string, info->flavour,
                          gtk_get_current_event_time ());
}

static void
popup_copy_url_callback (GtkAction *action,
                         TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalScreenPopupInfo *info = priv->popup_info;
  GtkClipboard *clipboard;

  if (info == NULL)
    return;

  if (info->string == NULL)
    return;

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_text (clipboard, info->string, -1);
}

static void
remove_popup_info (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (priv->remove_popup_info_idle != 0)
    {
      g_source_remove (priv->remove_popup_info_idle);
      priv->remove_popup_info_idle = 0;
    }

  if (priv->popup_info != NULL)
    {
      terminal_screen_popup_info_unref (priv->popup_info);
      priv->popup_info = NULL;
    }
}

static gboolean
idle_remove_popup_info (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  priv->remove_popup_info_idle = 0;
  remove_popup_info (window);
  return FALSE;
}

static void
unset_popup_info (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  /* Unref the event from idle since we still need it
   * from the action callbacks which will run before idle.
   */
  if (priv->remove_popup_info_idle == 0 &&
      priv->popup_info != NULL)
    {
      priv->remove_popup_info_idle =
        g_idle_add ((GSourceFunc) idle_remove_popup_info, window);
    }
}

static void
popup_menu_deactivate_callback (GtkWidget *popup,
                                TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *im_menu_item;

  g_signal_handlers_disconnect_by_func
    (popup, G_CALLBACK (popup_menu_deactivate_callback), window);

  im_menu_item = gtk_ui_manager_get_widget (priv->ui_manager,
                                            "/Popup/PopupInputMethods");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (im_menu_item), NULL);

  unset_popup_info (window);
}

static void
popup_clipboard_targets_received_cb (GtkClipboard *clipboard,
                                     GdkAtom *targets,
                                     int n_targets,
                                     TerminalScreenPopupInfo *info)
{
  TerminalWindow *window = info->window;
  TerminalWindowPrivate *priv = window->priv;
  TerminalScreen *screen = info->screen;
  GtkWidget *popup_menu, *im_menu, *im_menu_item;
  GtkAction *action;
  gboolean can_paste, can_paste_uris, show_link, show_email_link, show_call_link, show_input_method_menu;
  
  remove_popup_info (window);

  if (!GTK_WIDGET_REALIZED (info->screen))
    {
      terminal_screen_popup_info_unref (info);
      return;
    }

  priv->popup_info = info; /* adopt the ref added when requesting the clipboard */

  can_paste = targets != NULL && gtk_targets_include_text (targets, n_targets);
  can_paste_uris = targets != NULL && gtk_targets_include_uri (targets, n_targets);
  show_link = info->string != NULL && (info->flavour == FLAVOR_AS_IS || info->flavour == FLAVOR_DEFAULT_TO_HTTP);
  show_email_link = info->string != NULL && info->flavour == FLAVOR_EMAIL;
  show_call_link = info->string != NULL && info->flavour == FLAVOR_VOIP_CALL;

  action = gtk_action_group_get_action (priv->action_group, "PopupSendEmail");
  gtk_action_set_visible (action, show_email_link);
  action = gtk_action_group_get_action (priv->action_group, "PopupCopyEmailAddress");
  gtk_action_set_visible (action, show_email_link);
  action = gtk_action_group_get_action (priv->action_group, "PopupCall");
  gtk_action_set_visible (action, show_call_link);
  action = gtk_action_group_get_action (priv->action_group, "PopupCopyCallAddress");
  gtk_action_set_visible (action, show_call_link);
  action = gtk_action_group_get_action (priv->action_group, "PopupOpenLink");
  gtk_action_set_visible (action, show_link);
  action = gtk_action_group_get_action (priv->action_group, "PopupCopyLinkAddress");
  gtk_action_set_visible (action, show_link);

  action = gtk_action_group_get_action (priv->action_group, "PopupCloseWindow");
  gtk_action_set_visible (action, priv->terms <= 1);
  action = gtk_action_group_get_action (priv->action_group, "PopupCloseTab");
  gtk_action_set_visible (action, priv->terms > 1);

  action = gtk_action_group_get_action (priv->action_group, "PopupCopy");
  gtk_action_set_sensitive (action, vte_terminal_get_has_selection (VTE_TERMINAL (screen)));
  action = gtk_action_group_get_action (priv->action_group, "PopupPaste");
  gtk_action_set_sensitive (action, can_paste);
  action = gtk_action_group_get_action (priv->action_group, "PopupPasteURIPaths");
  gtk_action_set_visible (action, can_paste_uris);
  
  g_object_get (gtk_widget_get_settings (GTK_WIDGET (window)),
                "gtk-show-input-method-menu", &show_input_method_menu,
                NULL);

  action = gtk_action_group_get_action (priv->action_group, "PopupInputMethods");
  gtk_action_set_visible (action, show_input_method_menu);

  im_menu_item = gtk_ui_manager_get_widget (priv->ui_manager,
                                            "/Popup/PopupInputMethods");
  /* FIXME: fix this when gtk+ bug #500065 is done, use vte_terminal_im_merge_ui */
  if (show_input_method_menu)
    {
      im_menu = gtk_menu_new ();
      vte_terminal_im_append_menuitems (VTE_TERMINAL (screen),
                                        GTK_MENU_SHELL (im_menu));
      gtk_widget_show (im_menu);
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (im_menu_item), im_menu);
    }
  else
    {
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (im_menu_item), NULL);
    }

  popup_menu = gtk_ui_manager_get_widget (priv->ui_manager, "/Popup");
  g_signal_connect (popup_menu, "deactivate",
                    G_CALLBACK (popup_menu_deactivate_callback), window);

  /* Pseudo activation of the popup menu's action */
  action = gtk_action_group_get_action (priv->action_group, "Popup");
  gtk_action_activate (action);

  if (info->button == 0)
    gtk_menu_shell_select_first (GTK_MENU_SHELL (popup_menu), FALSE);

  gtk_menu_popup (GTK_MENU (popup_menu),
                  NULL, NULL,
                  NULL, NULL, 
                  info->button,
                  info->timestamp);
}

static void
screen_show_popup_menu_callback (TerminalScreen *screen,
                                 TerminalScreenPopupInfo *info,
                                 TerminalWindow *window)
{
  GtkClipboard *clipboard;

  g_return_if_fail (info->window == window);

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_request_targets (clipboard,
                                  (GtkClipboardTargetsReceivedFunc) popup_clipboard_targets_received_cb,
                                  terminal_screen_popup_info_ref (info));
}

static gboolean
screen_match_clicked_cb (TerminalScreen *screen,
                         const char *match,
                         int flavour,
                         guint state,
                         TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (screen != priv->active_screen)
    return FALSE;

  switch (flavour)
    {
      case FLAVOR_SKEY:
        terminal_skey_do_popup (GTK_WINDOW (window), screen, match);
        break;
      default:
        gtk_widget_grab_focus (GTK_WIDGET (screen));
        terminal_util_open_url (GTK_WIDGET (window), match, flavour,
                                gtk_get_current_event_time ());
        break;
    }

  return TRUE;
}

static void
screen_close_cb (TerminalScreen *screen,
                 TerminalWindow *window)
{
  terminal_window_remove_screen (window, screen);
}

/*****************************************/

static gboolean
terminal_window_state_event (GtkWidget            *widget,
                             GdkEventWindowState  *event)
{
  gboolean (* window_state_event) (GtkWidget *, GdkEventWindowState *event) =
      GTK_WIDGET_CLASS (terminal_window_parent_class)->window_state_event;

  if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    {
      TerminalWindow *window = TERMINAL_WINDOW (widget);
      TerminalWindowPrivate *priv = window->priv;
      GtkAction *action;
      gboolean is_fullscreen;

      is_fullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;

      action = gtk_action_group_get_action (priv->action_group, "ViewFullscreen");
      gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), is_fullscreen);
    }
  
  if (window_state_event)
    return window_state_event (widget, event);

  return FALSE;
}

static void
terminal_window_window_manager_changed_cb (GdkScreen *screen,
                                           TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkAction *action;
  gboolean supports_fs;

  supports_fs = gdk_x11_screen_supports_net_wm_hint (screen, gdk_atom_intern ("_NET_WM_STATE_FULLSCREEN", FALSE));

  action = gtk_action_group_get_action (priv->action_group, "ViewFullscreen");
  gtk_action_set_sensitive (action, supports_fs);
}

static void
terminal_window_screen_update (TerminalWindow *window,
                               GdkScreen *screen)
{
  TerminalApp *app;

  terminal_window_window_manager_changed_cb (screen, window);
  g_signal_connect (screen, "window-manager-changed",
                    G_CALLBACK (terminal_window_window_manager_changed_cb), window);

  if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (screen), "GT::HasSettingsConnection")))
    return;

  g_object_set_data_full (G_OBJECT (screen), "GT::HasSettingsConnection",
                          GINT_TO_POINTER (TRUE),
                          (GDestroyNotify) app_setting_notify_destroy_cb);

  app = terminal_app_get ();
  app_setting_notify_cb (app, NULL, screen);
  g_signal_connect (app, "notify::" TERMINAL_APP_ENABLE_MNEMONICS,
                    G_CALLBACK (app_setting_notify_cb), screen);
  g_signal_connect (app, "notify::" TERMINAL_APP_ENABLE_MENU_BAR_ACCEL,
                    G_CALLBACK (app_setting_notify_cb), screen);
}

static void
terminal_window_screen_changed (GtkWidget *widget,
                                GdkScreen *previous_screen)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);
  void (* screen_changed) (GtkWidget *, GdkScreen *) =
    GTK_WIDGET_CLASS (terminal_window_parent_class)->screen_changed;
  GdkScreen *screen;

  if (screen_changed)
    screen_changed (widget, previous_screen);

  screen = gtk_widget_get_screen (widget);
  if (previous_screen == screen)
    return;

  if (previous_screen)
    g_signal_handlers_disconnect_by_func (previous_screen,
                                          G_CALLBACK (terminal_window_window_manager_changed_cb),
                                          window);

  if (!screen)
    return;

  terminal_window_screen_update (window, screen);
}

static void
terminal_window_profile_list_changed_cb (TerminalApp *app,
                                         TerminalWindow *window)
{
  terminal_window_update_set_profile_menu (window);
  terminal_window_update_new_terminal_menus (window);
}

static void
terminal_window_encoding_list_changed_cb (TerminalApp *app,
                                          TerminalWindow *window)
{
  terminal_window_update_encoding_menu (window);
}

static void
terminal_window_init (TerminalWindow *window)
{
  const GtkActionEntry menu_entries[] =
    {
      /* Toplevel */
      { "File", NULL, N_("_File") },
      { "FileNewWindowProfiles", NULL, N_("Open _Terminal")},
      { "FileNewTabProfiles", NULL, N_("Open Ta_b") },
      { "Edit", NULL, N_("_Edit") },
      { "View", NULL, N_("_View") },
      { "Terminal", NULL, N_("_Terminal") },
      { "Tabs", NULL, N_("_Tabs") },
      { "Help", NULL, N_("_Help") },
      { "Popup", NULL, NULL },
      { "NotebookPopup", NULL, "" },

      /* File menu */
      { "FileNewWindow", STOCK_NEW_WINDOW, N_("Open _Terminal"), "<shift><control>N",
        NULL,
        G_CALLBACK (file_new_window_callback) },
      { "FileNewTab", STOCK_NEW_TAB, N_("Open Ta_b"), "<shift><control>T",
        NULL,
        G_CALLBACK (file_new_tab_callback) },
      { "FileNewProfile", GTK_STOCK_OPEN, N_("New _Profile…"), "",
        NULL,
        G_CALLBACK (file_new_profile_callback) },
      { "FileCloseTab", GTK_STOCK_CLOSE, N_("C_lose Tab"), "<shift><control>W",
        NULL,
        G_CALLBACK (file_close_tab_callback) },
      { "FileCloseWindow", GTK_STOCK_CLOSE, N_("_Close Window"), "<shift><control>Q",
        NULL,
        G_CALLBACK (file_close_window_callback) },

      /* Edit menu */
      { "EditCopy", GTK_STOCK_COPY, NULL, "<shift><control>C",
        NULL,
        G_CALLBACK (edit_copy_callback) },
      { "EditPaste", GTK_STOCK_PASTE, NULL, "<shift><control>V",
        NULL,
        G_CALLBACK (edit_paste_callback) },
      { "EditPasteURIPaths", GTK_STOCK_PASTE, N_("Paste _Filenames"), "",
        NULL,
        G_CALLBACK (edit_paste_callback) },
      { "EditProfiles", NULL, N_("P_rofiles…"), NULL,
        NULL,
        G_CALLBACK (edit_profiles_callback) },
      { "EditKeybindings", NULL, N_("_Keyboard Shortcuts…"), NULL,
        NULL,
        G_CALLBACK (edit_keybindings_callback) },
      { "EditCurrentProfile", NULL, N_("Profile _Preferences"), NULL,
        NULL,
        G_CALLBACK (edit_current_profile_callback) },

      /* View menu */
      { "ViewZoomIn", GTK_STOCK_ZOOM_IN, NULL, "<control>plus",
        NULL,
        G_CALLBACK (view_zoom_in_callback) },
      { "ViewZoomOut", GTK_STOCK_ZOOM_OUT, NULL, "<control>minus",
        NULL,
        G_CALLBACK (view_zoom_out_callback) },
      { "ViewZoom100", GTK_STOCK_ZOOM_100, NULL, "<control>0",
        NULL,
        G_CALLBACK (view_zoom_normal_callback) },

      /* Terminal menu */
      { "TerminalProfiles", NULL, N_("Change _Profile") },
      { "TerminalSetTitle", NULL, N_("_Set Title…"), NULL,
        NULL,
        G_CALLBACK (terminal_set_title_callback) },
      { "TerminalSetEncoding", NULL, N_("Set _Character Encoding") },
      { "TerminalReset", NULL, N_("_Reset"), NULL,
        NULL,
        G_CALLBACK (terminal_reset_callback) },
      { "TerminalResetClear", NULL, N_("Reset and C_lear"), NULL,
        NULL,
        G_CALLBACK (terminal_reset_clear_callback) },

      /* Terminal/Encodings menu */
      { "TerminalAddEncoding", NULL, N_("_Add or Remove…"), NULL,
        NULL,
        G_CALLBACK (terminal_add_encoding_callback) },

      /* Tabs menu */
      { "TabsPrevious", NULL, N_("_Previous Tab"), "<control>Page_Up",
        NULL,
        G_CALLBACK (tabs_previous_tab_callback) },
      { "TabsNext", NULL, N_("_Next Tab"), "<control>Page_Down",
        NULL,
        G_CALLBACK (tabs_next_tab_callback) },
      { "TabsMoveLeft", NULL, N_("Move Tab _Left"), "<shift><control>Page_Up",
        NULL,
        G_CALLBACK (tabs_move_left_callback) },
      { "TabsMoveRight", NULL, N_("Move Tab _Right"), "<shift><control>Page_Down",
        NULL,
        G_CALLBACK (tabs_move_right_callback) },
      { "TabsDetach", NULL, N_("_Detach tab"), NULL,
        NULL,
        G_CALLBACK (tabs_detach_tab_callback) },

      /* Help menu */
      { "HelpContents", GTK_STOCK_HELP, N_("_Contents"), "F1",
        NULL,
        G_CALLBACK (help_contents_callback) },
      { "HelpAbout", GTK_STOCK_ABOUT, N_("_About"), NULL,
        NULL,
        G_CALLBACK (help_about_callback) },

      /* Popup menu */
      { "PopupSendEmail", NULL, N_("_Send Mail To…"), NULL,
        NULL,
        G_CALLBACK (popup_open_url_callback) },
      { "PopupCopyEmailAddress", NULL, N_("_Copy E-mail Address"), NULL,
        NULL,
        G_CALLBACK (popup_copy_url_callback) },
      { "PopupCall", NULL, N_("C_all To…"), NULL,
        NULL,
        G_CALLBACK (popup_open_url_callback) },
      { "PopupCopyCallAddress", NULL, N_("_Copy Call Address"), NULL,
        NULL,
        G_CALLBACK (popup_copy_url_callback) },
      { "PopupOpenLink", NULL, N_("_Open Link"), NULL,
        NULL,
        G_CALLBACK (popup_open_url_callback) },
      { "PopupCopyLinkAddress", NULL, N_("_Copy Link Address"), NULL,
        NULL,
        G_CALLBACK (popup_copy_url_callback) },
      { "PopupTerminalProfiles", NULL, N_("P_rofiles") },
      { "PopupCopy", GTK_STOCK_COPY, NULL, "<shift><control>C",
        NULL,
        G_CALLBACK (edit_copy_callback) },
      { "PopupPaste", GTK_STOCK_PASTE, NULL, "<shift><control>V",
        NULL,
        G_CALLBACK (edit_paste_callback) },
      { "PopupPasteURIPaths", GTK_STOCK_PASTE, N_("Paste _Filenames"), "",
        NULL,
        G_CALLBACK (edit_paste_callback) },
      { "PopupNewTerminal", NULL, N_("Open _Terminal"), NULL,
        NULL,
        G_CALLBACK (file_new_window_callback) },
      { "PopupNewTab", NULL, N_("Open Ta_b"), NULL,
        NULL,
        G_CALLBACK (file_new_tab_callback) },
      { "PopupCloseWindow", NULL, N_("C_lose Window"), NULL,
        NULL,
        G_CALLBACK (file_close_window_callback) },
      { "PopupCloseTab", NULL, N_("C_lose Tab"), NULL,
        NULL,
        G_CALLBACK (file_close_tab_callback) },
      { "PopupInputMethods", NULL, N_("_Input Methods") }
    };
  
  const GtkToggleActionEntry toggle_menu_entries[] =
    {
      /* View Menu */
      { "ViewMenubar", NULL, N_("Show _Menubar"), NULL,
        NULL,
        G_CALLBACK (view_menubar_toggled_callback),
        FALSE },
      { "ViewFullscreen", NULL, N_("_Full Screen"), NULL,
        NULL,
        G_CALLBACK (view_fullscreen_toggled_callback),
        FALSE }
    };
  TerminalWindowPrivate *priv;
  TerminalApp *app;
  GtkActionGroup *action_group;
  GtkAction *action;
  GtkUIManager *manager;
  GtkWidget *main_vbox;
  GError *error;
  GtkWindowGroup *window_group;

  priv = window->priv = G_TYPE_INSTANCE_GET_PRIVATE (window, TERMINAL_TYPE_WINDOW, TerminalWindowPrivate);

  initialize_alpha_mode (window);

  g_signal_connect (G_OBJECT (window), "delete_event",
                    G_CALLBACK(terminal_window_delete_event),
                    NULL);

  gtk_window_set_title (GTK_WINDOW (window), _("Terminal"));

  priv->terms = 0;
  priv->active_screen = NULL;
  priv->menubar_visible = FALSE;
  
  main_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), main_vbox);
  gtk_widget_show (main_vbox);

  priv->notebook = gtk_notebook_new ();
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (priv->notebook), TRUE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (priv->notebook), FALSE);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (priv->notebook), FALSE);
  gtk_notebook_set_group (GTK_NOTEBOOK (priv->notebook), GUINT_TO_POINTER (1));
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (priv->notebook),
                               TRUE);
  g_signal_connect (priv->notebook, "button-press-event",
                    G_CALLBACK (notebook_button_press_cb), window);	
  g_signal_connect (priv->notebook, "popup-menu",
                    G_CALLBACK (notebook_popup_menu_cb), window);	
  g_signal_connect_after (priv->notebook, "switch-page",
                          G_CALLBACK (notebook_page_selected_callback), window);
  g_signal_connect_after (priv->notebook, "page-added",
                          G_CALLBACK (notebook_page_added_callback), window);
  g_signal_connect_after (priv->notebook, "page-removed",
                          G_CALLBACK (notebook_page_removed_callback), window);
  g_signal_connect_data (priv->notebook, "page-reordered",
                         G_CALLBACK (terminal_window_update_tabs_menu_sensitivity),
                         window, NULL, G_CONNECT_SWAPPED | G_CONNECT_AFTER);
  
  gtk_box_pack_end (GTK_BOX (main_vbox), priv->notebook, TRUE, TRUE, 0);
  gtk_widget_show (priv->notebook);

  priv->old_char_width = -1;
  priv->old_char_height = -1;
  priv->old_geometry_widget = NULL;
  
  /* force gtk to construct its GtkClipboard; otherwise our UI is very slow the first time we need it */
  /* FIXME is that really true still ?
   * Simple way to find out: comment the code out (if 0'd below), and see
   * if anyone complains after the next release :)
   */
#if 0
  gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
#endif

  /* Create the UI manager */
  manager = priv->ui_manager = gtk_ui_manager_new ();
  gtk_window_add_accel_group (GTK_WINDOW (window),
                              gtk_ui_manager_get_accel_group (manager));

  /* Create the actions */
  /* Note that this action group name is used in terminal-accels.c; do not change it */
  priv->action_group = action_group = gtk_action_group_new ("Main");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group, menu_entries,
                                G_N_ELEMENTS (menu_entries), window);
  gtk_action_group_add_toggle_actions (action_group,
                                       toggle_menu_entries,
                                       G_N_ELEMENTS (toggle_menu_entries),
                                       window);
  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  g_object_unref (action_group);

  action = gtk_action_group_get_action (action_group, "Edit");
  g_signal_connect (action, "activate",
                    G_CALLBACK (edit_menu_activate_callback), window);

  /* Set this action invisible so the Edit menu doesn't flash the first
   * time it's shown and there's no text/uri-list on the clipboard.
   */
  action = gtk_action_group_get_action (priv->action_group, "EditPasteURIPaths");
  gtk_action_set_visible (action, FALSE);

  /* Load the UI */
  error = NULL;
  priv->ui_id = gtk_ui_manager_add_ui_from_file (manager,
                                                 TERM_PKGDATADIR "/terminal.xml",
                                                 &error);
  if (error)
    {
      g_printerr ("Failed to load UI: %s\n", error->message);
      g_error_free (error);
    }

  priv->menubar = gtk_ui_manager_get_widget (manager, "/menubar");
  gtk_box_pack_start (GTK_BOX (main_vbox),
		      priv->menubar,
		      FALSE, FALSE, 0);

  /* Add tabs menu */
  priv->tabs_menu = terminal_tabs_menu_new (window);

  app = terminal_app_get ();
  terminal_window_profile_list_changed_cb (app, window);
  g_signal_connect (app, "profile-list-changed",
                    G_CALLBACK (terminal_window_profile_list_changed_cb), window);
  
  terminal_window_encoding_list_changed_cb (app, window);
  g_signal_connect (app, "encoding-list-changed",
                    G_CALLBACK (terminal_window_encoding_list_changed_cb), window);

  terminal_window_set_menubar_visible (window, TRUE);
  priv->use_default_menubar_visibility = TRUE;

  /* We have to explicitly call this, since screen-changed is NOT
   * emitted for the toplevel the first time!
   */
  terminal_window_screen_update (window, gtk_widget_get_screen (GTK_WIDGET (window)));

  window_group = gtk_window_group_new ();
  gtk_window_group_add_window (window_group, GTK_WINDOW (window));
  g_object_unref (window_group);

  terminal_util_set_unique_role (GTK_WINDOW (window), "gnome-terminal-window");
}

static void
terminal_window_class_init (TerminalWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  
  object_class->dispose = terminal_window_dispose;
  object_class->finalize = terminal_window_finalize;

  widget_class->show = terminal_window_show;
  widget_class->window_state_event = terminal_window_state_event;
  widget_class->screen_changed = terminal_window_screen_changed;

  g_type_class_add_private (object_class, sizeof (TerminalWindowPrivate));

  gtk_rc_parse_string ("style \"gnome-terminal-tab-close-button-style\"\n"
                       "{\n"
                          "GtkWidget::focus-padding = 0\n"
                          "GtkWidget::focus-line-width = 0\n"
                          "xthickness = 0\n"
                          "ythickness = 0\n"
                       "}\n"
                       "widget \"*.gnome-terminal-tab-close-button\" style \"gnome-terminal-tab-close-button-style\"");

  gtk_notebook_set_window_creation_hook (handle_tab_droped_on_desktop, NULL, NULL);
}

static void
terminal_window_dispose (GObject *object)
{
  TerminalWindow *window = TERMINAL_WINDOW (object);
  TerminalWindowPrivate *priv = window->priv;
  GdkScreen *screen;

  remove_popup_info (window);

  priv->disposed = TRUE;

  if (priv->tabs_menu)
    {
      g_object_unref (priv->tabs_menu);
      priv->tabs_menu = NULL;
    }

  if (priv->profiles_action_group != NULL)
    disconnect_profiles_from_actions_in_group (priv->profiles_action_group);
  if (priv->new_terminal_action_group != NULL)
    disconnect_profiles_from_actions_in_group (priv->new_terminal_action_group);

  g_signal_handlers_disconnect_by_func (terminal_app_get (),
                                        G_CALLBACK (terminal_window_profile_list_changed_cb),
                                        window);

  screen = gtk_widget_get_screen (GTK_WIDGET (object));
  if (screen)
    g_signal_handlers_disconnect_by_func (screen,
                                          G_CALLBACK (terminal_window_window_manager_changed_cb),
                                          window);

  G_OBJECT_CLASS (terminal_window_parent_class)->dispose (object);
}
   
static void
terminal_window_finalize (GObject *object)
{
  TerminalWindow *window = TERMINAL_WINDOW (object);
  TerminalWindowPrivate *priv = window->priv;

  g_free (priv->startup_id);

  g_object_unref (priv->ui_manager);

  G_OBJECT_CLASS (terminal_window_parent_class)->finalize (object);
}

static gboolean
terminal_window_delete_event (GtkWidget *widget,
                              GdkEvent *event,
                              gpointer data)
{
   return confirm_close_window (TERMINAL_WINDOW (widget));
}

static void
sn_error_trap_push (SnDisplay *display,
                    Display   *xdisplay)
{
  gdk_error_trap_push ();
}

static void
sn_error_trap_pop (SnDisplay *display,
                   Display   *xdisplay)
{
  gdk_error_trap_pop ();
}

static void
terminal_window_show (GtkWidget *widget)
{
  TerminalWindow *window = TERMINAL_WINDOW (widget);
  TerminalWindowPrivate *priv = window->priv;
  SnDisplay *sn_display;
  SnLauncheeContext *context;
  GdkScreen *screen;
  GdkDisplay *display;

  if (!GTK_WIDGET_REALIZED (widget))
    gtk_widget_realize (widget);
  
  context = NULL;
  sn_display = NULL;
  if (priv->startup_id != NULL)
    {
      /* Set up window for launch notification */

      screen = gtk_window_get_screen (GTK_WINDOW (window));
      display = gdk_screen_get_display (screen);
      
      sn_display = sn_display_new (gdk_x11_display_get_xdisplay (display),
                                   sn_error_trap_push,
                                   sn_error_trap_pop);
      
      context = sn_launchee_context_new (sn_display,
                                         gdk_screen_get_number (screen),
                                         priv->startup_id);

      /* Handle the setup for the window if the startup_id is valid; I
       * don't think it can hurt to do this even if it was invalid,
       * but why do the extra work...
       */
      if (strncmp (sn_launchee_context_get_startup_id (context), "_TIME", 5) != 0)
        sn_launchee_context_setup_window (context,
                                          GDK_WINDOW_XWINDOW (widget->window));

      /* Now, set the _NET_WM_USER_TIME for the new window to the timestamp
       * that caused the window to be launched.
       */
      if (sn_launchee_context_get_id_has_timestamp (context))
        {
          gulong timestamp;

          timestamp = sn_launchee_context_get_timestamp (context);
          gdk_x11_window_set_user_time (widget->window, timestamp);
        }

      g_free (priv->startup_id);
      priv->startup_id = NULL;
    }
  
  GTK_WIDGET_CLASS (terminal_window_parent_class)->show (widget);

  if (context != NULL)
    {
      sn_launchee_context_complete (context);
      sn_launchee_context_unref (context);
      sn_display_unref (sn_display);
    }
}

TerminalWindow*
terminal_window_new (void)
{
  return g_object_new (TERMINAL_TYPE_WINDOW, NULL);
}

static void
update_notebook (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  gboolean single;

  single = priv->terms == 1;
    
  gtk_notebook_set_show_border (GTK_NOTEBOOK (priv->notebook), !single);
}

static void
profile_set_callback (TerminalScreen *screen,
                      TerminalProfile *old_profile,
                      TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (screen != priv->active_screen)
    return;

  terminal_window_update_set_profile_menu_active_profile (window);
}

static void
sync_screen_title (TerminalScreen *screen,
                   GParamSpec *psepc,
                   TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  if (screen != priv->active_screen)
    return;

  gtk_window_set_title (GTK_WINDOW (window), terminal_screen_get_title (screen));
}

static void
sync_screen_icon_title (TerminalScreen *screen,
                        GParamSpec *psepc,
                        TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (screen != priv->active_screen)
    return;

  if (!terminal_screen_get_icon_title_set (screen))
    return;

  gdk_window_set_icon_name (GTK_WIDGET (window)->window, terminal_screen_get_icon_title (screen));
}

static void
sync_screen_icon_title_set (TerminalScreen *screen,
                            GParamSpec *psepc,
                            TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (screen != priv->active_screen)
    return;

  if (terminal_screen_get_icon_title_set (screen))
    return;

  /* Need to reset the icon name */
  /* FIXME: Once gtk+ bug 535557 is fixed, use that to unset the icon title. */

  gdk_window_set_icon_name (GTK_WIDGET (window)->window, terminal_screen_get_title (screen));
}

/* Notebook callbacks */

static void
close_button_clicked_cb (GtkWidget *widget,
                         GtkWidget *screen_container)
{
  GtkWidget *notebook;
  guint page_num;

  notebook = gtk_widget_get_parent (screen_container);
  page_num = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), screen_container);
  gtk_notebook_remove_page (GTK_NOTEBOOK (notebook), page_num);
}

static void
sync_tab_label (TerminalScreen *screen,
                GParamSpec *pspec,
                GtkWidget *label)
{
  GtkWidget *hbox;
  const char *title;

  title = terminal_screen_get_title (screen);
  hbox = gtk_widget_get_parent (label);

  gtk_label_set_text (GTK_LABEL (label), title);
  
  gtk_widget_set_tooltip_text (hbox, title);
}

static void
tab_label_style_set_cb (GtkWidget *hbox,
                        GtkStyle *previous_style,
                        GtkWidget *button)
{
  int h, w;

  gtk_icon_size_lookup_for_settings (gtk_widget_get_settings (button),
                                     GTK_ICON_SIZE_MENU, &w, &h);
  gtk_widget_set_size_request (button, w + 2, h + 2);
}

static GtkWidget *
construct_tab_label (TerminalScreen *screen, GtkWidget *screen_container)
{
  GtkWidget *hbox, *label, *close_button, *image;

  hbox = gtk_hbox_new (FALSE, 4);

  label = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (label), 0, 0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode (GTK_LABEL (label), TRUE);

  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

  close_button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (close_button), GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click (GTK_BUTTON (close_button), FALSE);
  gtk_button_set_relief (GTK_BUTTON (close_button), GTK_RELIEF_NONE);
  gtk_widget_set_name (close_button, "gnome-terminal-tab-close-button");
  gtk_widget_set_tooltip_text (close_button, _("Close tab"));

  image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (close_button), image);
  gtk_box_pack_end (GTK_BOX (hbox), close_button, FALSE, FALSE, 0);

  sync_tab_label (screen, NULL, label);
  g_signal_connect (screen, "notify::title",
                    G_CALLBACK (sync_tab_label), label);

  g_signal_connect (close_button, "clicked",
		    G_CALLBACK (close_button_clicked_cb), screen_container);

  g_signal_connect (hbox, "style-set",
                    G_CALLBACK (tab_label_style_set_cb), close_button);

  gtk_widget_show_all (hbox);

  return hbox;
}

void
terminal_window_add_screen (TerminalWindow *window,
                            TerminalScreen *screen,
                            int            position)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *old_window;
  GtkWidget *screen_container, *tab_label;

  old_window = gtk_widget_get_toplevel (GTK_WIDGET (screen));
  if (GTK_WIDGET_TOPLEVEL (old_window) &&
      TERMINAL_IS_WINDOW (old_window) &&
      TERMINAL_WINDOW (old_window)== window)
    return;  

  if (TERMINAL_IS_WINDOW (old_window))
    terminal_window_remove_screen (TERMINAL_WINDOW (old_window), screen);

  screen_container = terminal_screen_container_new (screen);
  gtk_widget_show (screen_container);

  update_tab_visibility (window, +1);

  tab_label = construct_tab_label (screen, screen_container);

  gtk_notebook_insert_page (GTK_NOTEBOOK (priv->notebook),
                            screen_container,
                            tab_label,
                            position);
  gtk_notebook_set_tab_label_packing (GTK_NOTEBOOK (priv->notebook),
                                      screen_container,
                                      TRUE, TRUE, GTK_PACK_START);
  gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (priv->notebook),
                                    screen_container,
                                    TRUE);
  gtk_notebook_set_tab_detachable (GTK_NOTEBOOK (priv->notebook),
                                   screen_container,
                                   TRUE);
}

void
terminal_window_remove_screen (TerminalWindow *window,
                               TerminalScreen *screen)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *scrolled_window;
  guint num_page;

  g_return_if_fail (gtk_widget_get_toplevel (GTK_WIDGET (screen)) == GTK_WIDGET (window));

  update_tab_visibility (window, -1);

  scrolled_window = GTK_WIDGET (screen)->parent;
  g_return_if_fail (scrolled_window != NULL);

  num_page = gtk_notebook_page_num (GTK_NOTEBOOK (priv->notebook), scrolled_window);
  gtk_notebook_remove_page (GTK_NOTEBOOK (priv->notebook), num_page);
}


void
terminal_window_move_screen (TerminalWindow *source_window,
                             TerminalWindow *dest_window,
                             TerminalScreen *screen,
                             int dest_position)
{
  GtkWidget *screen_container;

  g_return_if_fail (TERMINAL_IS_WINDOW (source_window));
  g_return_if_fail (TERMINAL_IS_WINDOW (dest_window));
  g_return_if_fail (TERMINAL_IS_SCREEN (screen));
  g_return_if_fail (gtk_widget_get_toplevel (GTK_WIDGET (screen)) == GTK_WIDGET (source_window));
  g_return_if_fail (dest_position >= -1);

  screen_container = GTK_WIDGET (screen)->parent;
  g_return_if_fail (GTK_IS_WIDGET (screen_container));

  /* We have to ref the screen container as well as the screen,
   * because otherwise removing the screen container from the source
   * window's notebook will cause the container and its containing
   * screen to be gtk_widget_destroy()ed!
   */
  g_object_ref_sink (screen_container);
  g_object_ref_sink (screen);
  terminal_window_remove_screen (source_window, screen);
    
  /* Now we can safely remove the screen from the container and let the container die */
  gtk_container_remove (GTK_CONTAINER (screen_container), GTK_WIDGET (screen));
  g_object_unref (screen_container);

  terminal_window_add_screen (dest_window, screen, dest_position);
  g_object_unref (screen);
}

GList*
terminal_window_list_screen_containers (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  /* We are trusting that GtkNotebook will return pages in order */
  return gtk_container_get_children (GTK_CONTAINER (priv->notebook));
}

void
terminal_window_set_menubar_visible (TerminalWindow *window,
                                     gboolean        setting)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkAction *action;

  /* it's been set now, so don't override when adding a screen.
   * this side effect must happen before we short-circuit below.
   */
  priv->use_default_menubar_visibility = FALSE;
  
  if (setting == priv->menubar_visible)
    return;

  priv->menubar_visible = setting;

  action = gtk_action_group_get_action (priv->action_group, "ViewMenubar");
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), setting);
  
  g_object_set (priv->menubar, "visible", setting, NULL);

  if (priv->active_screen)
    {
#ifdef DEBUG_GEOMETRY
      g_printerr ("setting size after toggling menubar visibility\n");
#endif
      terminal_window_set_size (window, priv->active_screen, TRUE);
    }
}

gboolean
terminal_window_get_menubar_visible (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  return priv->menubar_visible;
}

GtkWidget *
terminal_window_get_notebook (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
	
  g_return_val_if_fail (TERMINAL_IS_WINDOW (window), NULL);

  return GTK_WIDGET (priv->notebook);
}

void
terminal_window_set_size (TerminalWindow *window,
                          TerminalScreen *screen,
                          gboolean        even_if_mapped)
{
  terminal_window_set_size_force_grid (window, screen, even_if_mapped, -1, -1);
}

void
terminal_window_set_size_force_grid (TerminalWindow *window,
                                     TerminalScreen *screen,
                                     gboolean        even_if_mapped,
                                     int             force_grid_width,
                                     int             force_grid_height)
{
  /* Owen's hack from gnome-terminal */
  GtkWidget *widget;
  GtkWidget *app;
  GtkRequisition toplevel_request;
  GtkRequisition widget_request;
  int w, h;
  int char_width;
  int char_height;
  int grid_width;
  int grid_height;
  int xpad;
  int ypad;

  /* be sure our geometry is up-to-date */
  terminal_window_update_geometry (window);
  widget = GTK_WIDGET (screen);
  
  app = gtk_widget_get_toplevel (widget);
  g_assert (app != NULL);

  gtk_widget_size_request (app, &toplevel_request);
  gtk_widget_size_request (widget, &widget_request);

#ifdef DEBUG_GEOMETRY
  g_printerr ("set size: toplevel %dx%d widget %dx%d\n",
           toplevel_request.width, toplevel_request.height,
           widget_request.width, widget_request.height);
#endif
  
  w = toplevel_request.width - widget_request.width;
  h = toplevel_request.height - widget_request.height;

  terminal_screen_get_cell_size (screen, &char_width, &char_height);
  terminal_screen_get_size (screen, &grid_width, &grid_height);

  if (force_grid_width >= 0)
    grid_width = force_grid_width;
  if (force_grid_height >= 0)
    grid_height = force_grid_height;
  
  vte_terminal_get_padding (VTE_TERMINAL (screen), &xpad, &ypad);
  
  w += xpad * 2 + char_width * grid_width;
  h += ypad * 2 + char_height * grid_height;

#ifdef DEBUG_GEOMETRY
  g_printerr ("set size: grid %dx%d force %dx%d setting %dx%d pixels\n",
           grid_width, grid_height, force_grid_width, force_grid_height, w, h);
#endif

  if (even_if_mapped && GTK_WIDGET_MAPPED (app)) {
    gtk_window_resize (GTK_WINDOW (app), w, h);
  }
  else {
    gtk_window_set_default_size (GTK_WINDOW (app), w, h);
  }
}

static void
terminal_window_set_active (TerminalWindow *window,
                            TerminalScreen *screen)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *widget;
  TerminalProfile *profile;
  
  if (priv->active_screen == screen)
    return;
  
  /* Workaround to remove gtknotebook's feature of computing its size based on
   * all pages. When the widget is hidden, its size will not be taken into
   * account.
   */
  if (priv->active_screen)
    gtk_widget_hide (GTK_WIDGET (priv->active_screen)); /* FIXME */
  
  widget = GTK_WIDGET (screen);
  
  /* Make sure that the widget is no longer hidden due to the workaround */
  gtk_widget_show (widget);

  profile = terminal_screen_get_profile (screen);

  if (!GTK_WIDGET_REALIZED (widget))
    gtk_widget_realize (widget); /* we need this for the char width */

  priv->active_screen = screen;

  terminal_window_update_geometry (window);
  
  /* Override menubar setting if it wasn't restored from session */
  if (priv->use_default_menubar_visibility)
    {
      gboolean setting =
        terminal_profile_get_property_boolean (terminal_screen_get_profile (screen), TERMINAL_PROFILE_DEFAULT_SHOW_MENUBAR);

      terminal_window_set_menubar_visible (window, setting);
    }

  sync_screen_title (screen, NULL, window);
  sync_screen_icon_title_set (screen, NULL, window);
  sync_screen_icon_title (screen, NULL, window);

  /* set size of window to current grid size */
#ifdef DEBUG_GEOMETRY
  g_printerr ("setting size after flipping notebook pages\n");
#endif
  terminal_window_set_size (window, screen, TRUE);

  terminal_window_update_encoding_menu_active_encoding (window);
  terminal_window_update_set_profile_menu_active_profile (window);
  terminal_window_update_copy_sensitivity (window);
  terminal_window_update_zoom_sensitivity (window);
}

void
terminal_window_switch_screen (TerminalWindow *window,
                              TerminalScreen *screen)
{
  TerminalWindowPrivate *priv = window->priv;
  int page_num;

  page_num = gtk_notebook_page_num (GTK_NOTEBOOK (priv->notebook),
                                    GTK_WIDGET (screen)->parent);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->notebook), page_num);
}

TerminalScreen*
terminal_window_get_active (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  return priv->active_screen;
}

static gboolean
notebook_button_press_cb (GtkWidget *widget,
                          GdkEventButton *event,
                          TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkNotebook *notebook = GTK_NOTEBOOK (widget);
  GtkWidget *menu;
  GtkAction *action;
  int tab_clicked;

  if (event->type != GDK_BUTTON_PRESS ||
      event->button != 3 ||
      (event->state & gtk_accelerator_get_default_mod_mask ()) != 0)
    return FALSE;

  tab_clicked = find_tab_num_at_pos (notebook, event->x_root, event->y_root);
  if (tab_clicked < 0)
    return FALSE;

  /* switch to the page the mouse is over */
  gtk_notebook_set_current_page (notebook, tab_clicked);

  action = gtk_action_group_get_action (priv->action_group, "NotebookPopup");
  gtk_action_activate (action);

  menu = gtk_ui_manager_get_widget (priv->ui_manager, "/NotebookPopup");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, 
                  NULL, NULL, 
                  event->button, event->time);

  return TRUE;
}

static gboolean
notebook_popup_menu_cb (GtkWidget *widget,
                        TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkNotebook *notebook = GTK_NOTEBOOK (priv->notebook);
  GtkWidget *focus_widget, *tab, *tab_label, *menu;
  GtkAction *action;
  int page_num;

  focus_widget = gtk_window_get_focus (GTK_WINDOW (window));
  /* Only respond if the notebook is the actual focus */
  if (focus_widget != priv->notebook)
    return FALSE;

  page_num = gtk_notebook_get_current_page (notebook);
  tab = gtk_notebook_get_nth_page (notebook, page_num);
  tab_label = gtk_notebook_get_tab_label (notebook, tab);

  action = gtk_action_group_get_action (priv->action_group, "NotebookPopup");
  gtk_action_activate (action);

  menu = gtk_ui_manager_get_widget (priv->ui_manager, "/NotebookPopup");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, 
                  position_menu_under_widget, tab_label,
                  0, gtk_get_current_event_time ());
  gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);

  return TRUE;
}

static void
notebook_page_selected_callback (GtkWidget       *notebook,
                                 GtkNotebookPage *useless_crap,
                                 guint            page_num,
                                 TerminalWindow  *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget* page_widget;
  TerminalScreen *screen;
  int old_grid_width, old_grid_height;

  if (priv->active_screen == NULL || priv->disposed)
    return;

  terminal_screen_get_size (priv->active_screen, &old_grid_width, &old_grid_height);
  
  page_widget = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook),
                                           page_num);
  screen = terminal_screen_container_get_screen (page_widget);

  g_assert (screen);
  
  /* This is so that we maintain the same grid */
  vte_terminal_set_size (VTE_TERMINAL (screen), old_grid_width, old_grid_height);

  terminal_window_set_active (window, screen);
  terminal_window_update_tabs_menu_sensitivity (window);
}

static void
notebook_page_added_callback (GtkWidget       *notebook,
                              GtkWidget       *container,
                              guint            page_num,
                              TerminalWindow  *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalScreen *screen;

  screen = terminal_screen_container_get_screen (container);

  priv->terms++;

  g_signal_connect (G_OBJECT (screen),
                    "profile-set",
                    G_CALLBACK (profile_set_callback),
                    window);

  /* FIXME: only connect on the active screen, not all screens! */
  g_signal_connect (screen, "notify::title",
                    G_CALLBACK (sync_screen_title), window);
  g_signal_connect (screen, "notify::icon-title",
                    G_CALLBACK (sync_screen_icon_title), window);
  g_signal_connect (screen, "notify::icon-title-set",
                    G_CALLBACK (sync_screen_icon_title_set), window);

  g_signal_connect_swapped (G_OBJECT (screen),
                            "selection-changed",
                            G_CALLBACK (terminal_window_update_copy_sensitivity),
                            window);

  g_signal_connect (screen, "show-popup-menu",
                    G_CALLBACK (screen_show_popup_menu_callback), window);
  g_signal_connect (screen, "match-clicked",
                    G_CALLBACK (screen_match_clicked_cb), window);

  g_signal_connect (screen, "close-screen",
                    G_CALLBACK (screen_close_cb), window);

  update_notebook (window);

  update_tab_visibility (window, 0);

  /* ZvtTerm is a broken POS and requires this realize to get
   * the size request right.
   */
  /* FIXME: does this apply to VTE? */
  gtk_widget_realize (GTK_WIDGET (screen));

  /* If we have an active screen, match its size and zoom */
  if (priv->active_screen)
    {
      int current_width, current_height;
      double scale;

      terminal_screen_get_size (priv->active_screen, &current_width, &current_height);
      vte_terminal_set_size (VTE_TERMINAL (screen), current_width, current_height);

      scale = terminal_screen_get_font_scale (priv->active_screen);
      terminal_screen_set_font_scale (screen, scale);
    }
  
  /* Make the first-added screen the active one */
  /* FIXME: this shouldn't be necessary since we'll immediately get
   * page-selected callback.
   */
  if (priv->active_screen == NULL)
    terminal_window_set_active (window, screen);

  if (priv->present_on_insert)
    {
      gtk_window_present_with_time (GTK_WINDOW (window), gtk_get_current_event_time ());
      priv->present_on_insert = FALSE;
    }

  terminal_window_update_tabs_menu_sensitivity (window);
}

static void
notebook_page_removed_callback (GtkWidget       *notebook,
                                GtkWidget       *container,
                                guint            page_num,
                                TerminalWindow  *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalScreen *screen;
  int pages;

  if (priv->disposed)
    return;

  screen = terminal_screen_container_get_screen (container);

  g_signal_handlers_disconnect_by_func (G_OBJECT (screen),
                                        G_CALLBACK (profile_set_callback),
                                        window);

  g_signal_handlers_disconnect_by_func (G_OBJECT (screen),
                                        G_CALLBACK (sync_screen_title),
                                        window);

  g_signal_handlers_disconnect_by_func (G_OBJECT (screen),
                                        G_CALLBACK (sync_screen_icon_title),
                                        window);

  g_signal_handlers_disconnect_by_func (G_OBJECT (screen),
                                        G_CALLBACK (sync_screen_icon_title_set),
                                        window);

  g_signal_handlers_disconnect_by_func (G_OBJECT (screen),
                                        G_CALLBACK (terminal_window_update_copy_sensitivity),
                                        window);

  g_signal_handlers_disconnect_by_func (screen,
                                        G_CALLBACK (screen_show_popup_menu_callback),
                                        window);

  g_signal_handlers_disconnect_by_func (screen,
                                        G_CALLBACK (screen_match_clicked_cb),
                                        window);

  g_signal_handlers_disconnect_by_func (screen,
                                        G_CALLBACK (screen_close_cb),
                                        window);

  priv->terms--;

  update_notebook (window);

  terminal_window_update_tabs_menu_sensitivity (window);
  update_tab_visibility (window, 0);

  pages = priv->terms;
  if (pages == 1)
    {
      terminal_window_set_size (window, priv->active_screen, TRUE);
    }
  else if (pages == 0)
    {
      gtk_widget_destroy (GTK_WIDGET (window));
    }
}

void
terminal_window_update_geometry (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *widget;
  GdkGeometry hints;
  int char_width;
  int char_height;
  
  if (priv->active_screen == NULL)
    return;

  widget = GTK_WIDGET (priv->active_screen);

  /* We set geometry hints from the active term; best thing
   * I can think of to do. Other option would be to try to
   * get some kind of union of all hints from all terms in the
   * window, but that doesn't make too much sense.
   */
  terminal_screen_get_cell_size (priv->active_screen, &char_width, &char_height);
  
  if (char_width != priv->old_char_width ||
      char_height != priv->old_char_height ||
      widget != (GtkWidget*) priv->old_geometry_widget)
    {
      int xpad, ypad;
      
      /* FIXME Since we're using xthickness/ythickness to compute
       * padding we need to change the hints when the theme changes.
       */
      vte_terminal_get_padding (VTE_TERMINAL (priv->active_screen), &xpad, &ypad);
      
      hints.base_width = xpad;
      hints.base_height = ypad;

#define MIN_WIDTH_CHARS 4
#define MIN_HEIGHT_CHARS 2
      
      hints.width_inc = char_width;
      hints.height_inc = char_height;

      /* min size is min size of just the geometry widget, remember. */
      hints.min_width = hints.base_width + hints.width_inc * MIN_WIDTH_CHARS;
      hints.min_height = hints.base_height + hints.height_inc * MIN_HEIGHT_CHARS;
      
      gtk_window_set_geometry_hints (GTK_WINDOW (window),
                                     widget,
                                     &hints,
                                     GDK_HINT_RESIZE_INC |
                                     GDK_HINT_MIN_SIZE |
                                     GDK_HINT_BASE_SIZE);

#ifdef DEBUG_GEOMETRY
      g_printerr ("hints: base %dx%d min %dx%d inc %d %d\n",
               hints.base_width,
               hints.base_height,
               hints.min_width,
               hints.min_height,
               hints.width_inc,
               hints.height_inc);
#endif
      
      priv->old_char_width = hints.width_inc;
      priv->old_char_height = hints.height_inc;
      priv->old_geometry_widget = widget;
    }
#ifdef DEBUG_GEOMETRY
  else
    {
      g_printerr ("hints: increment unchanged, not setting\n");
    }
#endif
}

static void
file_new_window_callback (GtkAction *action,
                          TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalApp *app;
  TerminalWindow *new_window;
  TerminalProfile *profile;

  app = terminal_app_get ();

  profile = g_object_get_data (G_OBJECT (action), PROFILE_DATA_KEY);
  if (!profile)
    profile = terminal_screen_get_profile (priv->active_screen);
  if (!profile)
    profile = terminal_app_get_profile_for_new_term (app);
  if (!profile)
    return;

  if (_terminal_profile_get_forgotten (profile))
    return;

  new_window = terminal_app_new_window (app, gtk_widget_get_screen (GTK_WIDGET (window)));

  terminal_app_new_terminal (app, new_window, profile,
                             NULL, NULL,
                             terminal_screen_get_working_dir (priv->active_screen),
                             1.0);

  gtk_window_present (GTK_WINDOW (new_window));
}

static void
file_new_tab_callback (GtkAction *action,
                       TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalApp *app;
  TerminalProfile *profile;

  app = terminal_app_get ();
  profile = g_object_get_data (G_OBJECT (action), PROFILE_DATA_KEY);
  if (!profile)
    profile = terminal_screen_get_profile (priv->active_screen);
  if (!profile)
    profile = terminal_app_get_profile_for_new_term (app);
  if (!profile)
    return;

  if (_terminal_profile_get_forgotten (profile))
    return;

  terminal_app_new_terminal (app, window, profile,
                             NULL, NULL,
                             terminal_screen_get_working_dir (priv->active_screen),
                             1.0);
}

static void
confirm_close_window_response_cb (GtkWidget *dialog,
                                  int response,
                                  GtkWidget *window)
{
  gtk_widget_destroy (dialog);

  if (response == GTK_RESPONSE_ACCEPT)
    gtk_widget_destroy (window);
}

/* Returns TRUE if closing needs to wait until user confirmation;
 * FALSE if the window can close immediately
 */
static gboolean
confirm_close_window (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *dialog;
  GConfClient *client;
  gboolean do_confirm;
  int n;

  n = gtk_notebook_get_n_pages (GTK_NOTEBOOK (priv->notebook));
  if (n <= 1)
    return FALSE;

  client = gconf_client_get_default ();
  do_confirm = gconf_client_get_bool (client, CONF_GLOBAL_PREFIX "/confirm_window_close", NULL);
  g_object_unref (client);
  if (!do_confirm)
    return FALSE;

  dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_WARNING,
                                   GTK_BUTTONS_CANCEL,
                                   "%s", _("Close all tabs?"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
			  		    ngettext ("This window has one tab open. Closing "
						      "the window will close it.",
						      "This window has %d tabs open. Closing "
						      "the window will also close all tabs.",
						      n),
					    n);

  gtk_window_set_title (GTK_WINDOW(dialog), ""); 

  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Close All _Tabs"), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  g_signal_connect (dialog, "response",
                    G_CALLBACK (confirm_close_window_response_cb), window);
  gtk_window_present (GTK_WINDOW (dialog));

  return TRUE;
}

static void
file_close_window_callback (GtkAction *action,
                            TerminalWindow *window)
{
  if (!confirm_close_window (window))
    gtk_widget_destroy (GTK_WIDGET (window));
}

static void
file_close_tab_callback (GtkAction *action,
                         TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  if (!priv->active_screen)
    return;

  terminal_window_remove_screen (window, priv->active_screen);
}

static void
edit_copy_callback (GtkAction *action,
                    TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (!priv->active_screen)
    return;
      
  vte_terminal_copy_clipboard (VTE_TERMINAL (priv->active_screen));
}

typedef struct {
  TerminalScreen *screen;
  gboolean uris_as_paths;
} PasteData;

static void
clipboard_uris_received_cb (GtkClipboard *clipboard,
                            /* const */ char **uris,
                            PasteData *data)
{
  char *text;
  gsize len;

  if (!uris) {
    g_object_unref (data->screen);
    g_slice_free (PasteData, data);
    return;
  }

  /* This potentially modifies the strings in |uris| but that's ok */
  if (data->uris_as_paths)
    terminal_util_transform_uris_to_quoted_fuse_paths (uris);

  text = terminal_util_concat_uris (uris, &len);
  vte_terminal_feed_child (VTE_TERMINAL (data->screen), text, len);
  g_free (text);

  g_object_unref (data->screen);
  g_slice_free (PasteData, data);
}

static void
clipboard_targets_received_cb (GtkClipboard *clipboard,
                               GdkAtom *targets,
                               int n_targets,
                               PasteData *data)
{
  if (!targets) {
    g_object_unref (data->screen);
    g_slice_free (PasteData, data);
    return;
  }

  if (gtk_targets_include_uri (targets, n_targets)) {
    gtk_clipboard_request_uris (clipboard,
                                (GtkClipboardURIReceivedFunc) clipboard_uris_received_cb,
                                data);
    return;
  } else /* if (gtk_targets_include_text (targets, n_targets)) */ {
    vte_terminal_paste_clipboard (VTE_TERMINAL (data->screen));
  }

  g_object_unref (data->screen);
  g_slice_free (PasteData, data);
}

static void
edit_paste_callback (GtkAction *action,
                     TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkClipboard *clipboard;
  PasteData *data;
  const char *name;

  if (!priv->active_screen)
    return;
      
  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
  name = gtk_action_get_name (action);

  data = g_slice_new (PasteData);
  data->screen = g_object_ref (priv->active_screen);
  data->uris_as_paths = (name == I_("EditPasteURIPaths") || name == I_("PopupPasteURIPaths"));

  gtk_clipboard_request_targets (clipboard,
                                 (GtkClipboardTargetsReceivedFunc) clipboard_targets_received_cb,
                                 data);
}

static void
edit_keybindings_callback (GtkAction *action,
                           TerminalWindow *window)
{
  terminal_app_edit_keybindings (terminal_app_get (),
                                 GTK_WINDOW (window));
}

static void
edit_current_profile_callback (GtkAction *action,
                               TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  terminal_app_edit_profile (terminal_app_get (),
                             terminal_screen_get_profile (priv->active_screen),
                             GTK_WINDOW (window));
}

static void
file_new_profile_callback (GtkAction *action,
                           TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  terminal_app_new_profile (terminal_app_get (),
                            terminal_screen_get_profile (priv->active_screen),
                            GTK_WINDOW (window));
}

static void
edit_profiles_callback (GtkAction *action,
                        TerminalWindow *window)
{
  terminal_app_manage_profiles (terminal_app_get (),
                                GTK_WINDOW (window));
}

static void
view_menubar_toggled_callback (GtkToggleAction *action,
                               TerminalWindow *window)
{
  terminal_window_set_menubar_visible (window, gtk_toggle_action_get_active (action));
}

static void
view_fullscreen_toggled_callback (GtkToggleAction *action,
                                  TerminalWindow *window)
{
  g_return_if_fail (GTK_WIDGET_REALIZED (window));

  if (gtk_toggle_action_get_active (action))
    gtk_window_fullscreen (GTK_WINDOW (window));
  else
    gtk_window_unfullscreen (GTK_WINDOW (window));
}

static const double zoom_factors[] = {
  TERMINAL_SCALE_MINIMUM,
  TERMINAL_SCALE_XXXXX_SMALL,
  TERMINAL_SCALE_XXXX_SMALL,
  TERMINAL_SCALE_XXX_SMALL,
  PANGO_SCALE_XX_SMALL,
  PANGO_SCALE_X_SMALL,
  PANGO_SCALE_SMALL,
  PANGO_SCALE_MEDIUM,
  PANGO_SCALE_LARGE,
  PANGO_SCALE_X_LARGE,
  PANGO_SCALE_XX_LARGE,
  TERMINAL_SCALE_XXX_LARGE,
  TERMINAL_SCALE_XXXX_LARGE,
  TERMINAL_SCALE_XXXXX_LARGE,
  TERMINAL_SCALE_MAXIMUM
};

static gboolean
find_larger_zoom_factor (double  current,
                         double *found)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (zoom_factors); ++i)
    {
      /* Find a font that's larger than this one */
      if ((zoom_factors[i] - current) > 1e-6)
        {
          *found = zoom_factors[i];
          return TRUE;
        }
    }
  
  return FALSE;
}

static gboolean
find_smaller_zoom_factor (double  current,
                          double *found)
{
  int i;
  
  i = (int) G_N_ELEMENTS (zoom_factors) - 1;
  while (i >= 0)
    {
      /* Find a font that's smaller than this one */
      if ((current - zoom_factors[i]) > 1e-6)
        {
          *found = zoom_factors[i];
          return TRUE;
        }
      
      --i;
    }

  return FALSE;
}

static void
view_zoom_in_callback (GtkAction *action,
                       TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  double current;
  
  if (priv->active_screen == NULL)
    return;
  
  current = terminal_screen_get_font_scale (priv->active_screen);
  if (!find_larger_zoom_factor (current, &current))
    return;
      
  terminal_screen_set_font_scale (priv->active_screen, current);
  terminal_window_update_zoom_sensitivity (window);
}

static void
view_zoom_out_callback (GtkAction *action,
                        TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  double current;

  if (priv->active_screen == NULL)
    return;
  
  current = terminal_screen_get_font_scale (priv->active_screen);
  if (!find_smaller_zoom_factor (current, &current))
    return;
      
  terminal_screen_set_font_scale (priv->active_screen, current);
  terminal_window_update_zoom_sensitivity (window);
}

static void
view_zoom_normal_callback (GtkAction *action,
                           TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  if (priv->active_screen == NULL)
    return;

  terminal_screen_set_font_scale (priv->active_screen, PANGO_SCALE_MEDIUM);
  terminal_window_update_zoom_sensitivity (window);
}

static void
terminal_set_title_dialog_response_cb (GtkWidget *dialog,
                                       int response,
                                       TerminalScreen *screen)
{
  if (response == GTK_RESPONSE_OK)
    {
      GtkEntry *entry;
      const char *text;

      entry = GTK_ENTRY (g_object_get_data (G_OBJECT (dialog), "title-entry"));
      text = gtk_entry_get_text (entry);
      terminal_screen_set_user_title (screen, text);
    }

  gtk_widget_destroy (dialog);
}

static void
terminal_set_title_callback (GtkAction *action,
                             TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkWidget *dialog, *hbox, *label, *entry;

  if (priv->active_screen == NULL)
    return;

  /* FIXME: hook the screen up so this dialogue closes if the terminal screen closes */

  dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_OTHER,
                                   GTK_BUTTONS_OK_CANCEL,
                                   "%s", "");

  gtk_window_set_title (GTK_WINDOW (dialog), _("Set Title"));
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_role (GTK_WINDOW (dialog), "gnome-terminal-change-title");
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  g_signal_connect (dialog, "response",
                    G_CALLBACK (terminal_set_title_dialog_response_cb), priv->active_screen);
  g_signal_connect (dialog, "delete-event",
                    G_CALLBACK (terminal_util_dialog_response_on_delete), NULL);

  label = GTK_MESSAGE_DIALOG (dialog)->label;
  gtk_widget_hide (label);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (label->parent), hbox, FALSE, FALSE, 0);

  label = gtk_label_new_with_mnemonic (_("_Title:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

  entry = gtk_entry_new ();
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 32);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
  gtk_widget_show_all (hbox);

  gtk_widget_grab_focus (entry);
  gtk_entry_set_text (GTK_ENTRY (entry), terminal_screen_get_raw_title (priv->active_screen));
  gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
  g_object_set_data (G_OBJECT (dialog), "title-entry", entry);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
terminal_add_encoding_callback (GtkAction *action,
                                TerminalWindow *window)
{
  terminal_app_edit_encodings (terminal_app_get (),
                               GTK_WINDOW (window));
}

static void
terminal_reset_callback (GtkAction *action,
                         TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (priv->active_screen == NULL)
    return;
      
  vte_terminal_reset (VTE_TERMINAL (priv->active_screen), TRUE, FALSE);
}

static void
terminal_reset_clear_callback (GtkAction *action,
                               TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  if (priv->active_screen == NULL)
    return;
      
  vte_terminal_reset (VTE_TERMINAL (priv->active_screen), TRUE, TRUE);
}

static void
tabs_next_tab_callback (GtkAction *action,
                        TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  gtk_notebook_next_page (GTK_NOTEBOOK (priv->notebook));
}

static void
tabs_previous_tab_callback (GtkAction *action,
                            TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  
  gtk_notebook_prev_page (GTK_NOTEBOOK (priv->notebook));
}

static void
tabs_move_left_callback (GtkAction *action,
                         TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkNotebook *notebook = GTK_NOTEBOOK (priv->notebook);
  gint page_num,last_page;
  GtkWidget *page; 

  page_num = gtk_notebook_get_current_page (notebook);
  last_page = gtk_notebook_get_n_pages (notebook) - 1;
  page = gtk_notebook_get_nth_page (notebook, page_num);

  gtk_notebook_reorder_child (notebook, page, page_num == 0 ? last_page : page_num - 1);
}

static void
tabs_move_right_callback (GtkAction *action,
                          TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  GtkNotebook *notebook = GTK_NOTEBOOK (priv->notebook);
  gint page_num,last_page;
  GtkWidget *page; 

  page_num = gtk_notebook_get_current_page (notebook);
  last_page = gtk_notebook_get_n_pages (notebook) - 1;
  page = gtk_notebook_get_nth_page (notebook, page_num);
  
  gtk_notebook_reorder_child (notebook, page, page_num == last_page ? 0 : page_num + 1);
}

static void
tabs_detach_tab_callback (GtkAction *action,
                          TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;
  TerminalApp *app;
  TerminalWindow *new_window;
  TerminalScreen *screen;
  char *geometry;
  int width, height;

  app = terminal_app_get ();

  screen = priv->active_screen;

  /* FIXME: this seems wrong if tabs are shown in the window */
  terminal_screen_get_size (screen, &width, &height);
  geometry = g_strdup_printf ("%dx%d", width, height);

  new_window = terminal_app_new_window (app, gtk_widget_get_screen (GTK_WIDGET (window)));

  terminal_window_move_screen (window, new_window, screen, -1);

  gtk_window_parse_geometry (GTK_WINDOW (new_window), geometry);
  g_free (geometry);

  gtk_window_present_with_time (GTK_WINDOW (new_window), gtk_get_current_event_time ());
}

static void
help_contents_callback (GtkAction *action,
                        TerminalWindow *window)
{
  terminal_util_show_help (NULL, GTK_WINDOW (window));
}

static void
help_about_callback (GtkAction *action,
                     TerminalWindow *window)
{
  static const char copyright[] =
    "Copyright © 2002–2004 Havoc Pennington\n"
    "Copyright © 2003–2004, 2007 Mariano Suárez-Alvarez\n"
    "Copyright © 2006 Guilherme de S. Pastore\n"
    "Copyright © 2007–2008 Christian Persch";
  const char *authors[] = {
    "Behdad Esfahbod <behdad@gnome.org>",
    "Guilherme de S. Pastore <gpastore@gnome.org>",
    "Havoc Pennington <hp@redhat.com>",
    "Christian Persch <chpe" "\100" "gnome" "." "org" ">",
    "Mariano Suárez-Alvarez <mariano@gnome.org>",
    NULL
  };
  const gchar *license[] = {
    N_("GNOME Terminal is free software; you can redistribute it and/or modify "
       "it under the terms of the GNU General Public License as published by "
       "the Free Software Foundation; either version 2 of the License, or "
       "(at your option) any later version."),
    N_("GNOME Terminal is distributed in the hope that it will be useful, "
       "but WITHOUT ANY WARRANTY; without even the implied warranty of "
       "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
       "GNU General Public License for more details."),
    N_("You should have received a copy of the GNU General Public License "
       "along with GNOME Terminal; if not, write to the Free Software Foundation, "
       "Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA")
  };
  gchar *license_text;

  license_text = g_strjoin ("\n\n", _(license[0]), _(license[1]), _(license[2]), NULL);

  gtk_show_about_dialog (GTK_WINDOW (window),
			 "program-name", _("GNOME Terminal"),
			 "copyright", copyright,
			 "comments", _("A terminal emulator for the GNOME desktop"),
			 "version", VERSION,
			 "authors", authors,
			 "license", license_text,
			 "wrap-license", TRUE,
			 "translator-credits", _("translator-credits"),
			 "logo-icon-name", GNOME_TERMINAL_ICON_NAME,
			 NULL);
  g_free (license_text);
}

void
terminal_window_set_startup_id (TerminalWindow *window,
                                const char     *startup_id)
{
  TerminalWindowPrivate *priv = window->priv;

  g_free (priv->startup_id);
  priv->startup_id = g_strdup (startup_id);
}

GtkUIManager *
terminal_window_get_ui_manager (TerminalWindow *window)
{
  TerminalWindowPrivate *priv = window->priv;

  return priv->ui_manager;
}
