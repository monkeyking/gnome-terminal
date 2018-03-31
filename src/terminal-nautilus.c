/*
 *  Copyright (C) 2004, 2005 Free Software Foundation, Inc.
 *  Copyright © 2011 Christian Persch
 *  Author: Christian Neumair <chris@gnome-de.org>
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
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libnautilus-extension/nautilus-menu-provider.h>
#include <libnautilus-extension/nautilus-extension-types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "terminal-i18n.h"
#include "terminal-client-utils.h"
#include "terminal-defines.h"
#include "terminal-gdbus-generated.h"

/* Nautilus extension class */

#define TERMINAL_TYPE_NAUTILUS         (terminal_nautilus_get_type ())
#define TERMINAL_NAUTILUS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TERMINAL_TYPE_NAUTILUS, TerminalNautilus))
#define TERMINAL_NAUTILUS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TERMINAL_TYPE_NAUTILUS, TerminalNautilusClass))
#define TERMINAL_IS_NAUTILUS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TERMINAL_TYPE_NAUTILUS))
#define TERMINAL_IS_NAUTILUS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TERMINAL_TYPE_NAUTILUS))
#define TERMINAL_NAUTILUS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TERMINAL_TYPE_NAUTILUS, TerminalNautilusClass))

typedef struct _TerminalNautilus      TerminalNautilus;
typedef struct _TerminalNautilusClass TerminalNautilusClass;

struct _TerminalNautilus {
        GObject parent_instance;

        GSettings *lockdown_prefs;
        gboolean have_mc;
};

struct _TerminalNautilusClass {
        GObjectClass parent_class;
};

static GType terminal_nautilus_get_type (void);

/* Nautilus menu item class */

#define TERMINAL_TYPE_NAUTILUS_MENU_ITEM        (terminal_nautilus_menu_item_get_type ())
#define TERMINAL_NAUTILUS_MENU_ITEM(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), TERMINAL_TYPE_NAUTILUS_MENU_ITEM, TerminalNautilusMenuItem))
#define TERMINAL_NAUTILUS_MENU_ITEM_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), TERMINAL_TYPE_NAUTILUS_MENU_ITEM, TerminalNautilusMenuItemClass))
#define TERMINAL_IS_NAUTILUS_MENU_ITEM(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), TERMINAL_TYPE_NAUTILUS_MENU_ITEM))
#define TERMINAL_IS_NAUTILUS_MENU_ITEM_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), TERMINAL_TYPE_NAUTILUS_MENU_ITEM))
#define TERMINAL_NAUTILUS_MENU_ITEM_GET_CLASS(o)(G_TYPE_INSTANCE_GET_CLASS ((o), TERMINAL_TYPE_NAUTILUS_MENU_ITEM, TerminalNautilusMenuItemClass))

typedef struct _TerminalNautilusMenuItem      TerminalNautilusMenuItem;
typedef struct _TerminalNautilusMenuItemClass TerminalNautilusMenuItemClass;

struct _TerminalNautilusMenuItem {
  NautilusMenuItem parent_instance;

  TerminalNautilus *nautilus;
  GdkScreen *screen;
  NautilusFileInfo *file_info;
  gboolean run_in_mc;
  gboolean remote_terminal;
};

struct _TerminalNautilusMenuItemClass {
  NautilusMenuItemClass parent_class;
};

static GType terminal_nautilus_menu_item_get_type (void);

/* --- */

#define TERMINAL_ICON_NAME "utilities-terminal"

typedef enum {
  /* local files. Always open "conventionally", i.e. cd and spawn. */
  FILE_INFO_LOCAL,
  FILE_INFO_DESKTOP,
  /* SFTP: Shell terminals are opened "remote" (i.e. with ssh client),
   * commands are executed like OTHER.
   */
  FILE_INFO_SFTP,
  /* OTHER: Terminals and commands are opened by mapping the URI back
   * to ~/.gvfs, i.e. to the GVFS FUSE bridge.
   */
  FILE_INFO_OTHER
} TerminalFileInfo;

static TerminalFileInfo
get_terminal_file_info_from_uri (const char *uri)
{
  TerminalFileInfo ret;
  char *uri_scheme;

  uri_scheme = g_uri_parse_scheme (uri);

  if (uri_scheme == NULL) {
    ret = FILE_INFO_OTHER;
  } else if (strcmp (uri_scheme, "file") == 0) {
    ret = FILE_INFO_LOCAL;
  } else if (strcmp (uri_scheme, "x-nautilus-desktop") == 0) {
    ret = FILE_INFO_DESKTOP;
  } else if (strcmp (uri_scheme, "sftp") == 0 ||
             strcmp (uri_scheme, "ssh") == 0) {
    ret = FILE_INFO_SFTP;
  } else {
    ret = FILE_INFO_OTHER;
  }

  g_free (uri_scheme);

  return ret;
}

/* Helpers */

#define NAUTILUS_SETTINGS_SCHEMA                "org.gnome.Nautilus"
#define GNOME_DESKTOP_LOCKDOWN_SETTINGS_SCHEMA  "org.gnome.desktop.lockdown"

static inline gboolean
desktop_opens_home_dir (TerminalNautilus *nautilus)
{
#if 0
  return  _client_get_bool (gconf_client,
                                "/apps/nautilus-open-terminal/desktop_opens_home_dir",
                                NULL);
#endif
  return TRUE;
}

static inline gboolean
display_mc_item (TerminalNautilus *nautilus)
{
#if 0
  return gconf_client_get_bool (gconf_client,
                                "/apps/nautilus-open-terminal/display_mc_item",
                                NULL);
#endif
  return FALSE;
}

static inline gboolean
desktop_is_home_dir (TerminalNautilus *nautilus)
{
  return FALSE;
}

/* a very simple URI parsing routine from Launchpad #333462, until GLib supports URI parsing (GNOME #489862) */
#define SFTP_PREFIX "sftp://"
static void
parse_sftp_uri (GFile *file,
                char **user,
                char **host,
                unsigned int *port,
                char **path)
{
  char *tmp, *save;
  char *uri;

  uri = g_file_get_uri (file);
  g_assert (uri != NULL);
  save = uri;

  *path = NULL;
  *user = NULL;
  *host = NULL;
  *port = 0;

  /* skip intial 'sftp:// prefix */
  g_assert (!strncmp (uri, SFTP_PREFIX, strlen (SFTP_PREFIX)));
  uri += strlen (SFTP_PREFIX);

  /* cut out the path */
  tmp = strchr (uri, '/');
  if (tmp != NULL) {
    *path = g_uri_unescape_string (tmp, "/");
    *tmp = '\0';
  }

  /* read the username - it ends with @ */
  tmp = strchr (uri, '@');
  if (tmp != NULL) {
    *tmp++ = '\0';

    *user = strdup (uri);
    if (strchr (*user, ':') != NULL) {
      /* chop the password */
      *(strchr (*user, ':')) = '\0'; 
    }

    uri = tmp;
  }

  /* now read the port, starts with : */
  tmp = strchr (uri, ':');
  if (tmp != NULL) {
    *tmp++ = '\0';
    *port = atoi (tmp);  /*FIXME: getservbyname*/
  }

  /* what is left is the host */
  *host = strdup (uri);
  g_free (save);
}

static char **
ssh_argv (const char *uri,
          gboolean run_in_mc,
          int *argcp)
{
  GFile *file;
  char **argv;
  int argc;
  char *host_name, *path, *user_name, *unescaped_path, *quoted_path;
  guint host_port;

  g_assert (uri != NULL);

  argv = g_new0 (char *, 9);
  argc = 0;
  argv[argc++] = g_strdup ("ssh");
  argv[argc++] = g_strdup ("-t");

  file = g_file_new_for_uri (uri);
  parse_sftp_uri (file, &user_name, &host_name, &host_port, &path);
  g_object_unref (file);

  if (user_name != NULL) {
    argv[argc++ ]= g_strdup_printf ("%s@%s", user_name, host_name);
    g_free (host_name);
    g_free (user_name);
  } else {
    argv[argc++] = host_name;
  }

  if (host_port != 0) {
    argv[argc++] = g_strdup ("-p");
    argv[argc++] = g_strdup_printf ("%u", host_port);
  }

  /* FIXME to we have to consider the remote file encoding? */
  unescaped_path = g_uri_unescape_string (path, NULL);
  quoted_path = g_shell_quote (unescaped_path);

  if (run_in_mc) {
    argv[argc++] = g_strdup_printf ("cd %s && exec %s", quoted_path, "mc");
  } else {
    /* login shell */
    argv[argc++] = g_strdup_printf ("cd %s && exec $SHELL -", quoted_path);
  }

  g_free (path);
  g_free (unescaped_path);
  g_free (quoted_path);

  *argcp = argc;
  return argv;
}

static char **
mc_argv (int *argcp)
{
  char **argv;

  argv = g_new (char *, 2);
  argv[0] = g_strdup ("mc");
  argv[1] = NULL;

  *argcp = 1;
  return argv;
}

static gboolean
terminal_locked_down (TerminalNautilus *nautilus)
{
  return g_settings_get_boolean (nautilus->lockdown_prefs,
                                 "disable-command-line");
}

/* used to determine for remote URIs whether GVFS is capable of mapping them to ~/.gvfs */
static gboolean
uri_has_local_path (const char *uri)
{
  GFile *file;
  char *path;
  gboolean ret;

  file = g_file_new_for_uri (uri);
  path = g_file_get_path (file);

  ret = (path != NULL);

  g_free (path);
  g_object_unref (file);

  return ret;
}

/* Nautilus menu item class */

typedef struct {
  TerminalNautilus *nautilus;
  guint32 timestamp;
  char *path;
  char *uri;
  char *display;
  TerminalFileInfo info;
  gboolean remote;
  gboolean run_in_mc;
} ExecData;

static void
exec_data_free (ExecData *data)
{
  g_object_unref (data->nautilus);
  g_free (data->path);
  g_free (data->uri);
  g_free (data->display);

  g_free (data);
}

/* FIXME: make this async */
static gboolean
create_terminal (ExecData *data /* transfer full */)
{
  TerminalFactory *factory;
  TerminalReceiver *receiver;
  GError *error = NULL;
  GVariantBuilder builder;
  char *object_path;
  char startup_id[32];
  char **argv;
  int argc;

  factory = terminal_factory_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                     TERMINAL_APPLICATION_ID,
                                                     TERMINAL_FACTORY_OBJECT_PATH,
                                                     NULL /* cancellable */,
                                                     &error);
  if (factory == NULL) {
    g_dbus_error_strip_remote_error (error);
    g_printerr ("Error constructing proxy for %s:%s: %s\n",
                TERMINAL_APPLICATION_ID, TERMINAL_FACTORY_OBJECT_PATH,
                error->message);
    g_error_free (error);
    exec_data_free (data);
    return FALSE;
  }

  g_snprintf (startup_id, sizeof (startup_id), "_TIME%u", data->timestamp);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

  terminal_client_append_create_instance_options (&builder,
                                                  data->display,
                                                  startup_id,
                                                  NULL /* geometry */,
                                                  NULL /* role */,
                                                  NULL /* use default profile */,
                                                  NULL /* title */,
                                                  TRUE, /* active */
                                                  FALSE /* maximised */,
                                                  FALSE /* fullscreen */);

  if (!terminal_factory_call_create_instance_sync
         (factory,
          g_variant_builder_end (&builder),
          &object_path,
          NULL /* cancellable */,
          &error)) {
    g_dbus_error_strip_remote_error (error);
    g_printerr ("Error creating terminal: %s\n", error->message);
    g_error_free (error);
    g_object_unref (factory);
    exec_data_free (data);
    return FALSE;
  }

  g_object_unref (factory);

  receiver = terminal_receiver_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       TERMINAL_APPLICATION_ID,
                                                       object_path,
                                                       NULL /* cancellable */,
                                                       &error);
  if (receiver == NULL) {
    g_dbus_error_strip_remote_error (error);
    g_printerr ("Failed to create proxy for terminal: %s\n", error->message);
    g_error_free (error);
    g_free (object_path);
    exec_data_free (data);
    return FALSE;
  }

  g_free (object_path);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

  terminal_client_append_exec_options (&builder,
                                       data->path,
                                       NULL, 0, /* FD array */
                                       TRUE /* shell */);

  if (data->info == FILE_INFO_SFTP &&
      data->remote) {
    argv = ssh_argv (data->uri, data->run_in_mc, &argc);
  } else if (data->run_in_mc) {
    argv = mc_argv (&argc);
  } else {
    argv = NULL; argc = 0;
  }

  if (!terminal_receiver_call_exec_sync (receiver,
                                         g_variant_builder_end (&builder),
                                         g_variant_new_bytestring_array ((const char * const *) argv, argc),
                                         NULL /* in FD list */,
                                         NULL /* out FD list */,
                                         NULL /* cancellable */,
                                         &error)) {
    g_dbus_error_strip_remote_error (error);
    g_printerr ("Error: %s\n", error->message);
    g_error_free (error);
    g_strfreev (argv);
    g_object_unref (receiver);
    exec_data_free (data);
    return FALSE;
  }

  g_strfreev (argv);

  exec_data_free (data);

  g_object_unref (receiver);

  return TRUE;
}

static void
terminal_nautilus_menu_item_activate (NautilusMenuItem *item)
{
  TerminalNautilusMenuItem *menu_item = TERMINAL_NAUTILUS_MENU_ITEM (item);
  TerminalNautilus *nautilus = menu_item->nautilus;
  char *uri, *path;
  TerminalFileInfo info;
  ExecData *data;

  uri = nautilus_file_info_get_activation_uri (menu_item->file_info);
  if (uri == NULL)
    return;

  path = NULL;
  info = get_terminal_file_info_from_uri (uri);

  switch (info) {
    case FILE_INFO_LOCAL:
      path = g_filename_from_uri (uri, NULL, NULL);
      break;

    case FILE_INFO_DESKTOP:
      if (desktop_is_home_dir (nautilus) || desktop_opens_home_dir (nautilus)) {
        path = g_strdup (g_get_home_dir ());
      } else {
        path = g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP));
      }
      break;

    case FILE_INFO_SFTP:
      if (menu_item->remote_terminal)
        break;

      /* fall through */

    case FILE_INFO_OTHER: {
      GFile *file;

      /* map back remote URI to local path */
      file = g_file_new_for_uri (uri);
      path = g_file_get_path (file);
      g_object_unref (file);
      break;
    }

    default:
      g_assert_not_reached ();
  }

  if (path == NULL && (info != FILE_INFO_SFTP || !menu_item->remote_terminal)) {
    g_free (uri);
    return;
  }

  data = g_new (ExecData, 1);
  data->nautilus = g_object_ref (nautilus);
  data->timestamp = gtk_get_current_event_time ();
  data->path = path;
  data->uri = uri;
  data->display = gdk_screen_make_display_name (menu_item->screen);
  data->info = info;
  data->remote = menu_item->remote_terminal;
  data->run_in_mc = menu_item->run_in_mc;

  create_terminal (data);
}

G_DEFINE_DYNAMIC_TYPE (TerminalNautilusMenuItem, terminal_nautilus_menu_item, NAUTILUS_TYPE_MENU_ITEM)

static void 
terminal_nautilus_menu_item_init (TerminalNautilusMenuItem *nautilus_menu_item)
{
}

static void
terminal_nautilus_menu_item_dispose (GObject *object)
{
  TerminalNautilusMenuItem *menu_item = TERMINAL_NAUTILUS_MENU_ITEM (object);

  if (menu_item->screen != NULL) {
    g_object_unref (menu_item->screen);
    menu_item->screen = NULL;
  }
  if (menu_item->file_info != NULL) {
    g_object_unref (menu_item->file_info);
    menu_item->file_info = NULL;
  }
  if (menu_item->nautilus != NULL) {
    g_object_unref (menu_item->nautilus);
    menu_item->nautilus = NULL;
  }

  G_OBJECT_CLASS (terminal_nautilus_menu_item_parent_class)->dispose (object);
}

static void
terminal_nautilus_menu_item_class_init (TerminalNautilusMenuItemClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  NautilusMenuItemClass *menu_item_class = NAUTILUS_MENU_ITEM_CLASS (klass);

  gobject_class->dispose = terminal_nautilus_menu_item_dispose;

  menu_item_class->activate = terminal_nautilus_menu_item_activate;
}

static void
terminal_nautilus_menu_item_class_finalize (TerminalNautilusMenuItemClass *class)
{
}

static NautilusMenuItem *
terminal_nautilus_menu_item_new (TerminalNautilus *nautilus,
                                 NautilusFileInfo *file_info,
                                 TerminalFileInfo  terminal_file_info,
                                 GdkScreen        *screen,
                                 gboolean          run_in_mc,
                                 gboolean          remote_terminal,
                                 gboolean          is_file_item)
{
  TerminalNautilusMenuItem *item;
  const char *action_name;
  const char *name;
  const char *tooltip;

  if (!run_in_mc) {
    if (is_file_item) {
      action_name = remote_terminal ? "TerminalNautilus:OpenRemote"
                                    : "TerminalNautilus:OpenLocal";
    } else {
      action_name = remote_terminal ? "TerminalNautilus:OpenFolderRemote"
                                    : "TerminalNautilus:OpenFolderLocal";
    }

    switch (terminal_file_info) {
      case FILE_INFO_SFTP:
        if (remote_terminal) {
          name = _("Open in _Remote Terminal");
        } else {
          name = _("Open in _Local Terminal");
        }

        if (is_file_item) {
          tooltip = _("Open the currently selected folder in a terminal");
        } else {
          tooltip = _("Open the currently open folder in a terminal");
        }
        break;

      case FILE_INFO_LOCAL:
      case FILE_INFO_OTHER:
        name = _("Open in T_erminal");

        if (is_file_item) {
          tooltip = _("Open the currently selected folder in a terminal");
        } else {
          tooltip = _("Open the currently open folder in a terminal");
        }
        break;

      case FILE_INFO_DESKTOP:
        if (desktop_opens_home_dir (nautilus)) {
          name = _("Open T_erminal");
          tooltip = _("Open a terminal");
        } else {
          name = _("Open in T_erminal");
          tooltip = _("Open the currently open folder in a terminal");
        }
        break;

      default:
        g_assert_not_reached ();
    }
  } else {
    action_name = remote_terminal ? "TerminalNautilus:OpenRemoteMC"
                                  : "TerminalNautilus:OpenLocalMC";

    switch (terminal_file_info) {
      case FILE_INFO_LOCAL:
      case FILE_INFO_SFTP:
      case FILE_INFO_OTHER:
        name = _("Open in _Midnight Commander");
        if (is_file_item) {
          tooltip = _("Open the currently selected folder in the terminal file manager Midnight Commander");
        } else {
          tooltip = _("Open the currently open folder in the terminal file manager Midnight Commander");
        }
        break;

      case FILE_INFO_DESKTOP:
        if (desktop_opens_home_dir (nautilus)) {
          name = _("Open _Midnight Commander");
          tooltip = _("Open the terminal file manager Midnight Commander");
        } else {
          name = _("Open in _Midnight Commander");
          tooltip = _("Open the currently open folder in the terminal file manager Midnight Commander");
        }
        break;

      default:
              g_assert_not_reached ();
    }
  }

  item = g_object_new (TERMINAL_TYPE_NAUTILUS_MENU_ITEM,
                       "name", action_name,
                       "label", name,
                       "tip", tooltip,
                       "icon", TERMINAL_ICON_NAME,
                       NULL);

  item->nautilus = g_object_ref (nautilus);
  item->file_info = g_object_ref (file_info);
  item->screen = g_object_ref (screen);
  item->run_in_mc = run_in_mc;
  item->remote_terminal = remote_terminal;

  return (NautilusMenuItem *) item;
}

/* Nautilus extension class implementation */

static GList *
terminal_nautilus_get_background_items (NautilusMenuProvider *provider,
                                        GtkWidget            *window,
                                        NautilusFileInfo     *file_info)
{
  TerminalNautilus *nautilus = TERMINAL_NAUTILUS (provider);
  gchar *uri;
  GList *items;
  NautilusMenuItem *item;
  TerminalFileInfo terminal_file_info;

  if (terminal_locked_down (nautilus))
    return NULL;

  uri = nautilus_file_info_get_activation_uri (file_info);
  if (uri == NULL)
    return NULL;

  items = NULL;

  terminal_file_info = get_terminal_file_info_from_uri (uri);

  if (terminal_file_info == FILE_INFO_SFTP ||
      terminal_file_info == FILE_INFO_DESKTOP ||
      uri_has_local_path (uri)) {
    /* local locations or SSH */
    item = terminal_nautilus_menu_item_new (nautilus,
                                            file_info, 
                                            terminal_file_info, 
                                            gtk_widget_get_screen (window),
                                            FALSE, 
                                            terminal_file_info == FILE_INFO_SFTP, 
                                            FALSE);
    items = g_list_append (items, item);
  }

  if ((terminal_file_info == FILE_INFO_SFTP ||
        terminal_file_info == FILE_INFO_OTHER) &&
      uri_has_local_path (uri)) {
    /* remote locations that offer local back-mapping */
    item = terminal_nautilus_menu_item_new (nautilus,
                                            file_info, 
                                            terminal_file_info, 
                                            gtk_widget_get_screen (window),
                                            FALSE, 
                                            FALSE, 
                                            FALSE);
    items = g_list_append (items, item);
  }

  if (display_mc_item (nautilus) &&
      nautilus->have_mc &&
      ((terminal_file_info == FILE_INFO_DESKTOP &&
       (desktop_is_home_dir (nautilus) || desktop_opens_home_dir (nautilus))) ||
       uri_has_local_path (uri))) {
    item = terminal_nautilus_menu_item_new (nautilus,
                                            file_info, 
                                            terminal_file_info, 
                                            gtk_widget_get_screen (window), 
                                            TRUE, 
                                            FALSE, 
                                            FALSE);
    items = g_list_append (items, item);
  }

  g_free (uri);

  return items;
}

static GList *
terminal_nautilus_get_file_items (NautilusMenuProvider *provider,
                                  GtkWidget            *window,
                                  GList                *files)
{
  TerminalNautilus *nautilus = TERMINAL_NAUTILUS (provider);
  gchar *uri;
  GList *items;
  NautilusMenuItem *item;
  NautilusFileInfo *file_info;
  GFileType file_type;
  TerminalFileInfo terminal_file_info;

  if (terminal_locked_down (nautilus))
    return NULL;

  /* Only add items when passed exactly one file */
  if (files == NULL || files->next != NULL)
    return NULL;

  file_info = (NautilusFileInfo *) files->data;
  file_type = nautilus_file_info_get_file_type (file_info);
  if (!nautilus_file_info_is_directory (file_info) &&
      file_type != G_FILE_TYPE_SHORTCUT &&
      file_type != G_FILE_TYPE_MOUNTABLE)
    return NULL;

  uri = nautilus_file_info_get_activation_uri (file_info);
  if (uri == NULL)
    return NULL;

  items = NULL;

  terminal_file_info = get_terminal_file_info_from_uri (uri);

  switch (terminal_file_info) {
    case FILE_INFO_LOCAL:
    case FILE_INFO_SFTP:
    case FILE_INFO_OTHER:
      if (terminal_file_info == FILE_INFO_SFTP || 
          uri_has_local_path (uri)) {
        item = terminal_nautilus_menu_item_new (nautilus,
                                                file_info,
                                                terminal_file_info, 
                                                gtk_widget_get_screen (window),
                                                FALSE, 
                                                terminal_file_info == FILE_INFO_SFTP, 
                                                TRUE);
        items = g_list_append (items, item);
      }

      if (terminal_file_info == FILE_INFO_SFTP &&
          uri_has_local_path (uri)) {
        item = terminal_nautilus_menu_item_new (nautilus,
                                                file_info, 
                                                terminal_file_info, 
                                                gtk_widget_get_screen (window), 
                                                FALSE, 
                                                FALSE, 
                                                TRUE);
        items = g_list_append (items, item);
      }

      if (display_mc_item (nautilus) &&
          nautilus->have_mc &&
          uri_has_local_path (uri)) {
        item = terminal_nautilus_menu_item_new (nautilus,
                                                file_info, 
                                                terminal_file_info, 
                                                gtk_widget_get_screen (window), 
                                                TRUE, 
                                                TRUE, 
                                                FALSE);
        items = g_list_append (items, item);
      }
      break;

    case FILE_INFO_DESKTOP:
      break;

    default:
      g_assert_not_reached ();
  }

  g_free (uri);

  return items;
}

static void
terminal_nautilus_menu_provider_iface_init (NautilusMenuProviderIface *iface)
{
  iface->get_background_items = terminal_nautilus_get_background_items;
  iface->get_file_items = terminal_nautilus_get_file_items;
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (TerminalNautilus, terminal_nautilus, G_TYPE_OBJECT, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (NAUTILUS_TYPE_MENU_PROVIDER,
                                                               terminal_nautilus_menu_provider_iface_init))

static void 
terminal_nautilus_init (TerminalNautilus *nautilus)
{
  char *path;

  nautilus->lockdown_prefs = g_settings_new (GNOME_DESKTOP_LOCKDOWN_SETTINGS_SCHEMA);

  path = g_find_program_in_path ("mc");
  nautilus->have_mc = (path != NULL);
  g_free (path);
}

static void
terminal_nautilus_dispose (GObject *object)
{
  TerminalNautilus *nautilus = TERMINAL_NAUTILUS (object);

  g_clear_object (&nautilus->lockdown_prefs);

  G_OBJECT_CLASS (terminal_nautilus_parent_class)->dispose (object);
}

static void
terminal_nautilus_class_init (TerminalNautilusClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = terminal_nautilus_dispose;

  terminal_i18n_init (FALSE);
}

static void
terminal_nautilus_class_finalize (TerminalNautilusClass *class)
{
}

/* Nautilus extension */

static GType type_list[1];

#define EXPORT __attribute__((__visibility__("default"))) extern

EXPORT void
nautilus_module_initialize (GTypeModule *module)
{
  terminal_nautilus_register_type (module);
  terminal_nautilus_menu_item_register_type (module);

  type_list[0] = TERMINAL_TYPE_NAUTILUS;
}

EXPORT void
nautilus_module_shutdown (void)
{
}

EXPORT void
nautilus_module_list_types (const GType **types,
                            int          *num_types)
{
  *types = type_list;
  *num_types = G_N_ELEMENTS (type_list);
}
