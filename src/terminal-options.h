/*
 * Copyright © 2001, 2002 Havoc Pennington
 * Copyright © 2002 Red Hat, Inc.
 * Copyright © 2002 Sun Microsystems
 * Copyright © 2003 Mariano Suarez-Alvarez
 * Copyright © 2008 Christian Persch
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

#ifndef TERMINAL_OPTIONS_H
#define TERMINAL_OPTIONS_H

#include <glib.h>

#include "terminal-profiles-list.h"

G_BEGIN_DECLS

#define TERMINAL_CONFIG_VERSION             (1) /* Bump this for any changes */
#define TERMINAL_CONFIG_COMPAT_VERSION      (1) /* Bump this for incompatible changes */

#define TERMINAL_CONFIG_GROUP               "GNOME Terminal Configuration"
#define TERMINAL_CONFIG_PROP_VERSION        "Version"
#define TERMINAL_CONFIG_PROP_COMPAT_VERSION "CompatVersion"
#define TERMINAL_CONFIG_PROP_WINDOWS        "Windows"

#define TERMINAL_CONFIG_WINDOW_PROP_ACTIVE_TAB       "ActiveTerminal"
#define TERMINAL_CONFIG_WINDOW_PROP_FULLSCREEN       "Fullscreen"
#define TERMINAL_CONFIG_WINDOW_PROP_GEOMETRY         "Geometry"
#define TERMINAL_CONFIG_WINDOW_PROP_MAXIMIZED        "Maximized"
#define TERMINAL_CONFIG_WINDOW_PROP_MENUBAR_VISIBLE  "MenubarVisible"
#define TERMINAL_CONFIG_WINDOW_PROP_ROLE             "Role"
#define TERMINAL_CONFIG_WINDOW_PROP_TABS             "Terminals"

#define TERMINAL_CONFIG_TERMINAL_PROP_HEIGHT             "Height"
#define TERMINAL_CONFIG_TERMINAL_PROP_COMMAND            "Command"
#define TERMINAL_CONFIG_TERMINAL_PROP_PROFILE_ID         "ProfileID"
#define TERMINAL_CONFIG_TERMINAL_PROP_TITLE              "Title"
#define TERMINAL_CONFIG_TERMINAL_PROP_WIDTH              "Width"
#define TERMINAL_CONFIG_TERMINAL_PROP_WORKING_DIRECTORY  "WorkingDirectory"
#define TERMINAL_CONFIG_TERMINAL_PROP_ZOOM               "Zoom"

enum
{
  SOURCE_DEFAULT = 0,
  SOURCE_SESSION = 1
};

typedef struct
{
  TerminalSettingsList *profiles_list; /* may be NULL */

  char    *server_app_id;
  gboolean remote_arguments;
  char    *startup_id;
  char    *display_name;
  int      screen_number;
  GList   *initial_windows;
  gboolean default_window_menubar_forced;
  gboolean default_window_menubar_state;
  gboolean default_fullscreen;
  gboolean default_maximize;
  char    *default_role;
  char    *default_geometry;
  char    *default_working_dir;
  char    *default_title;
  char   **exec_argv;
  char    *default_profile;
  gboolean default_profile_is_id;

  gboolean  execute;
  gboolean  use_factory;
  double    zoom;

  gboolean sm_client_disable;
  char *sm_client_id;
  char *sm_config_prefix;

  guint zoom_set : 1;
} TerminalOptions;

typedef struct
{
  char *profile;
  gboolean profile_is_id;
  char **exec_argv;
  char *title;
  char *working_dir;
  double zoom;
  guint zoom_set : 1;
  guint active : 1;
} InitialTab;

typedef struct
{
  guint source_tag;

  GList *tabs; /* list of InitialTab */

  gboolean force_menubar_state;
  gboolean menubar_state;

  gboolean start_fullscreen;
  gboolean start_maximized;

  char *geometry;
  char *role;

} InitialWindow;

#define TERMINAL_OPTION_ERROR (g_quark_from_static_string ("terminal-option-error"))

typedef enum {
  TERMINAL_OPTION_ERROR_NOT_SUPPORTED,
  TERMINAL_OPTION_ERROR_NOT_IN_FACTORY,
  TERMINAL_OPTION_ERROR_EXCLUSIVE_OPTIONS,
  TERMINAL_OPTION_ERROR_INVALID_CONFIG_FILE,
  TERMINAL_OPTION_ERROR_INCOMPATIBLE_CONFIG_FILE
} TerminalOptionError;

TerminalOptions *terminal_options_parse (const char *working_directory,
                                         const char *startup_id,
                                         int *argcp,
                                         char ***argvp,
                                         GError **error);

gboolean terminal_options_merge_config (TerminalOptions *options,
                                        GKeyFile *key_file,
                                        guint source_tag,
                                        GError **error);

void terminal_options_ensure_window (TerminalOptions *options);

void terminal_options_free (TerminalOptions *options);

G_END_DECLS

#endif /* !TERMINAL_OPTIONS_H */
