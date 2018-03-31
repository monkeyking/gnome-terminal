/*
 * Copyright © 2001, 2002 Havoc Pennington
 * Copyright © 2002 Red Hat, Inc.
 * Copyright © 2002 Sun Microsystems
 * Copyright © 2003 Mariano Suarez-Alvarez
 * Copyright © 2008, 2010, 2011 Christian Persch
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

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include "terminal-debug.h"
#include "terminal-defines.h"
#include "terminal-i18n.h"
#include "terminal-options.h"
#include "terminal-gdbus-generated.h"
#include "terminal-defines.h"
#include "terminal-client-utils.h"
#include "terminal-libgsystem.h"

GS_DEFINE_CLEANUP_FUNCTION0(TerminalOptions*, gs_local_options_free, terminal_options_free)
#define gs_free_options __attribute__ ((cleanup(gs_local_options_free)))

/* Wait-for-exit helper */

typedef struct {
  GMainLoop *loop;
  int status;
} RunData;

static void
receiver_child_exited_cb (TerminalReceiver *receiver,
                          int status,
                          RunData *data)
{
  data->status = status;

  if (g_main_loop_is_running (data->loop))
    g_main_loop_quit (data->loop);
}

static int
run_receiver (TerminalReceiver *receiver)
{
  RunData data = { g_main_loop_new (NULL, FALSE), 0 };
  gulong id = g_signal_connect (receiver, "child-exited",
                                G_CALLBACK (receiver_child_exited_cb), &data);
  g_main_loop_run (data.loop);
  g_signal_handler_disconnect (receiver, id);
  g_main_loop_unref (data.loop);

  /* Mangle the exit status */
  int exit_code;
  if (WIFEXITED (data.status))
    exit_code = WEXITSTATUS (data.status);
  else if (WIFSIGNALED (data.status))
    exit_code = 128 + (int) WTERMSIG (data.status);
  else if (WCOREDUMP (data.status))
    exit_code = 127;
  else
    exit_code = 127;

  return exit_code;
}

/* Factory helpers */

static gboolean
get_factory_exit_status (const char *service_name,
                         const char *message,
                         int *exit_status)
{
  gs_free char *pattern = NULL, *number = NULL;
  gs_unref_regex GRegex *regex = NULL;
  gs_free_match_info GMatchInfo *match_info = NULL;
  gint64 v;
  char *end;
  GError *err = NULL;

  pattern = g_strdup_printf ("org.freedesktop.DBus.Error.Spawn.ChildExited: Process %s exited with status (\\d+)$",
                             service_name);
  regex = g_regex_new (pattern, 0, 0, &err);
  g_assert_no_error (err);

  if (!g_regex_match (regex, message, 0, &match_info))
    return FALSE;

  number = g_match_info_fetch (match_info, 1);
  g_assert_nonnull (number);

  errno = 0;
  v = g_ascii_strtoll (number, &end, 10);
  if (errno || end == number || *end != '\0' || v < 0 || v > G_MAXINT)
    return FALSE;

  *exit_status = (int)v;
  return TRUE;
}

static gboolean
handle_factory_error (const char *service_name,
                      GError *error)
{
  int exit_status;

  if (!g_dbus_error_is_remote_error (error) ||
      !g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SPAWN_CHILD_EXITED) ||
      !get_factory_exit_status (service_name, error->message, &exit_status))
    return FALSE;

  g_dbus_error_strip_remote_error (error);
  terminal_printerr ("%s\n\n", error->message);

  switch (exit_status) {
  case _EXIT_FAILURE_WRONG_ID:
    terminal_printerr ("You tried to run gnome-terminal-server with elevated privileged. This is not supported.\n");
    break;
  case _EXIT_FAILURE_NO_UTF8:
    terminal_printerr ("The environment that gnome-terminal-server was launched with specified a non-UTF-8 locale. This is not supported.\n");
    break;
  case _EXIT_FAILURE_UNSUPPORTED_LOCALE:
    terminal_printerr ("The environment that gnome-terminal-server was launched with specified an unsupported locale.\n");
    break;
  case _EXIT_FAILURE_GTK_INIT:
    terminal_printerr ("The environment that gnome-terminal-server was launched with most likely contained an incorrect or unset \"DISPLAY\" variable.\n");
    break;
  default:
    break;
  }
  terminal_printerr ("See https://wiki.gnome.org/Apps/Terminal/FAQ#Exit_status_%d for more information.\n", exit_status);

  return TRUE;
}

static gboolean
handle_create_instance_error (const char *service_name,
                              GError *error)
{
  if (handle_factory_error (service_name, error))
    return TRUE;

  g_dbus_error_strip_remote_error (error);
  terminal_printerr ("Error creating terminal: %s\n", error->message);
  return FALSE; /* don't abort */
}

static gboolean
handle_create_receiver_proxy_error (const char *service_name,
                                    GError *error)
{
  if (handle_factory_error (service_name, error))
    return TRUE;

  g_dbus_error_strip_remote_error (error);
  terminal_printerr ("Failed to create proxy for terminal: %s\n", error->message);
  return FALSE; /* don't abort */
}

static gboolean
handle_exec_error (const char *service_name,
                   GError *error)
{
  if (handle_factory_error (service_name, error))
    return TRUE;

  g_dbus_error_strip_remote_error (error);
  terminal_printerr ("Error: %s\n", error->message);
  return FALSE; /* don't abort */
}

static gboolean
factory_proxy_new_for_service_name (const char *service_name,
                                    gboolean ping_server,
                                    TerminalFactory **factory_ptr,
                                    char **service_name_ptr,
                                    GError **error)
{
  if (service_name == NULL)
    service_name = TERMINAL_APPLICATION_ID;

  gs_free_error GError *err = NULL;
  gs_unref_object TerminalFactory *factory =
    terminal_factory_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                             G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                             G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                             service_name,
                                             TERMINAL_FACTORY_OBJECT_PATH,
                                             NULL /* cancellable */,
                                             &err);
  if (factory == NULL) {
    if (!handle_factory_error (service_name, err))
      terminal_printerr ("Error constructing proxy for %s:%s: %s\n",
                         service_name, TERMINAL_FACTORY_OBJECT_PATH, err->message);
    g_propagate_error (error, err);
    err = NULL;
    return FALSE;
  }

  if (ping_server) {
    /* If we try to use the environment specified server, we need to make
     * sure it actually exists so we can later fall back to the default name.
     * There doesn't appear to a way to fail proxy creation above if the
     * unique name doesn't exist; so we do it this way.
     */
    gs_unref_variant GVariant *v = g_dbus_proxy_call_sync (G_DBUS_PROXY (factory),
                                                           "org.freedesktop.DBus.Peer.Ping",
                                                           g_variant_new ("()"),
                                                           G_DBUS_CALL_FLAGS_NONE,
                                                           1000 /* 1s */,
                                                           NULL /* cancelleable */,
                                                           &err);
    if (v == NULL) {
      g_propagate_error (error, err);
      err = NULL;
      return FALSE;
    }
  }

  gs_transfer_out_value (factory_ptr, &factory);
  *service_name_ptr = g_strdup (service_name);
  return TRUE;
}

static gboolean
factory_proxy_new (TerminalOptions *options,
                   TerminalFactory **factory_ptr,
                   char **service_name_ptr,
                   char **parent_screen_object_path_ptr,
                   GError **error)
{
  const char *service_name = options->server_app_id;

  /* If --app-id was specified, or the environment does not specify
   * the server to use, create the factory proxy from the given (or default)
   * name, with no fallback.
   *
   * If the server specified by the environment doesn't exist, fall back to the
   * default server, and ignore the environment-specified parent screen.
   */
  if (options->server_app_id == NULL &&
      options->server_unique_name != NULL) {
    gs_free_error GError *err = NULL;
    if (factory_proxy_new_for_service_name (options->server_unique_name,
                                            TRUE,
                                            factory_ptr,
                                            service_name_ptr,
                                            &err)) {
      *parent_screen_object_path_ptr = g_strdup (options->parent_screen_object_path);
      return TRUE;
    }

    terminal_printerr ("Failed to use specified server: %s\n",
                       err->message);
    terminal_printerr ("Falling back to default server.\n");

    /* Fall back to the default */
    service_name = NULL;
  }

  *parent_screen_object_path_ptr = NULL;

  return factory_proxy_new_for_service_name (service_name,
                                             FALSE,
                                             factory_ptr,
                                             service_name_ptr,
                                             error);
}

static void
handle_show_preferences (const char *service_name)
{
  gs_free_error GError *error = NULL;
  gs_unref_object GDBusConnection *bus = NULL;
  gs_free char *object_path = NULL;
  GVariantBuilder builder;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL) {
    terminal_printerr ("Failed to get session bus: %s\n", error->message);
    return;
  }

  /* For reasons (!?), the org.gtk.Actions interface's object path
   * is derived from the service name, i.e. for service name
   * "foo.bar.baz" the object path is "/foo/bar/baz".
   * This means that without the name (like when given only the unique name),
   * we cannot activate the action.
   */
  if (g_dbus_is_unique_name(service_name)) {
    terminal_printerr ("Cannot call this function from within gnome-terminal.\n");
    return;
  }

  object_path = g_strdelimit (g_strdup_printf (".%s", service_name), ".", '/');

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sava{sv})"));
  g_variant_builder_add (&builder, "s", "preferences");
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("av"));
  g_variant_builder_close (&builder);
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_close (&builder);

  if (!g_dbus_connection_call_sync (bus,
                                    service_name,
                                    object_path,
                                    "org.gtk.Actions",
                                    "Activate",
                                    g_variant_builder_end (&builder),
                                    G_VARIANT_TYPE ("()"),
                                    G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                    30 * 1000 /* ms timeout */,
                                    NULL /* cancelleable */,
                                    &error)) {
    terminal_printerr ("Activate call failed: %s\n", error->message);
    return;
  }
}

/**
 * handle_options:
 * @app:
 * @options: a #TerminalOptions
 * @allow_resume: whether to merge the terminal configuration from the
 *   saved session on resume
 * @wait_for_receiver: location to store the #TerminalReceiver to wait for
 *
 * Processes @options. It loads or saves the terminal configuration, or
 * opens the specified windows and tabs.
 *
 * Returns: %TRUE if @options could be successfully handled, or %FALSE on
 *   error
 */
static gboolean
handle_options (TerminalOptions *options,
                TerminalFactory *factory,
                const char *service_name,
                const char *parent_screen_object_path,
                TerminalReceiver **wait_for_receiver)
{

  /* We need to forward the locale encoding to the server, see bug #732128 */
  const char *encoding;
  g_get_charset (&encoding);

  if (options->show_preferences) {
    handle_show_preferences (service_name);
  } else {
    /* Make sure we open at least one window */
    terminal_options_ensure_window (options);
  }

  const char *factory_unique_name = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (factory));

  for (GList *lw = options->initial_windows;  lw != NULL; lw = lw->next)
    {
      InitialWindow *iw = lw->data;

      g_assert_nonnull (iw);

      guint window_id = 0;

      gs_free char *previous_screen_object_path = NULL;
      if (iw->implicit_first_window)
        previous_screen_object_path = g_strdup (parent_screen_object_path);

      /* Now add the tabs */
      for (GList *lt = iw->tabs; lt != NULL; lt = lt->next)
        {
          InitialTab *it = lt->data;
          g_assert_nonnull (it);

          GVariantBuilder builder;
          g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

          terminal_client_append_create_instance_options (&builder,
                                                          options->display_name,
                                                          options->startup_id,
                                                          iw->geometry,
                                                          iw->role,
                                                          it->profile ? it->profile : options->default_profile,
                                                          encoding,
                                                          it->title ? it->title : options->default_title,
                                                          it->active,
                                                          iw->start_maximized,
                                                          iw->start_fullscreen);

          /* This will be used to apply missing defaults */
          if (parent_screen_object_path != NULL)
            g_variant_builder_add (&builder, "{sv}",
                                   "parent-screen", g_variant_new_object_path (parent_screen_object_path));

          /* This will be used to get the parent window */
          if (previous_screen_object_path)
            g_variant_builder_add (&builder, "{sv}",
                                   "window-from-screen", g_variant_new_object_path (previous_screen_object_path));
          if (window_id)
            g_variant_builder_add (&builder, "{sv}",
                                   "window-id", g_variant_new_uint32 (window_id));
          /* Restored windows shouldn't demand attention; see bug #586308. */
          if (iw->source_tag == SOURCE_SESSION)
            g_variant_builder_add (&builder, "{sv}",
                                   "present-window", g_variant_new_boolean (FALSE));
          if (options->zoom_set || it->zoom_set)
            g_variant_builder_add (&builder, "{sv}",
                                   "zoom", g_variant_new_double (it->zoom_set ? it->zoom : options->zoom));
          if (iw->force_menubar_state)
            g_variant_builder_add (&builder, "{sv}",
                                   "show-menubar", g_variant_new_boolean (iw->menubar_state));

          gs_free_error GError *err = NULL;
          gs_free char *object_path = NULL;
          if (!terminal_factory_call_create_instance_sync
                 (factory,
                  g_variant_builder_end (&builder),
                  &object_path,
                  NULL /* cancellable */,
                  &err)) {
            if (handle_create_instance_error (service_name, err))
              return FALSE;
            else
              continue; /* Continue processing the remaining options! */
          }

          /* Deprecated and not working on new server anymore */
          char *p = strstr (object_path, "/window/");
          if (p) {
            char *end = NULL;
            guint64 value;

            errno = 0;
            p += strlen ("/window/");
            value = g_ascii_strtoull (p, &end, 10);
            if (errno == 0 && end != p && *end == '/')
              window_id = (guint) value;
          }

          g_free (previous_screen_object_path);
          previous_screen_object_path = g_strdup (object_path);

          gs_unref_object TerminalReceiver *receiver =
            terminal_receiver_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                      (it->wait ? 0 : G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS),
                                                      factory_unique_name,
                                                      object_path,
                                                      NULL /* cancellable */,
                                                      &err);
          if (receiver == NULL) {
            if (handle_create_receiver_proxy_error (service_name, err))
              return FALSE;
            else
              continue; /* Continue processing the remaining options! */
          }

          g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

          char **argv = it->exec_argv ? it->exec_argv : options->exec_argv;
          int argc = argv ? g_strv_length (argv) : 0;

          PassFdElement *fd_array = it->fd_array ? (PassFdElement*)it->fd_array->data : NULL;
          gsize fd_array_len = it->fd_array ? it->fd_array->len : 0;

          terminal_client_append_exec_options (&builder,
                                               it->working_dir ? it->working_dir
                                                               : options->default_working_dir,
                                               fd_array, fd_array_len,
                                               argc == 0);

          if (!terminal_receiver_call_exec_sync (receiver,
                                                 g_variant_builder_end (&builder),
                                                 g_variant_new_bytestring_array ((const char * const *) argv, argc),
                                                 it->fd_list, NULL /* outfdlist */,
                                                 NULL /* cancellable */,
                                                 &err)) {
            if (handle_exec_error (service_name, err))
              return FALSE;
            else
              continue; /* Continue processing the remaining options! */
          }

          if (it->wait)
            gs_transfer_out_value (wait_for_receiver, &receiver);

          if (options->print_environment)
            g_print ("%s=%s\n", TERMINAL_ENV_SCREEN, object_path);
        }
    }

  return TRUE;
}

int
main (int argc, char **argv)
{
  int exit_code = EXIT_FAILURE;

#if GLIB_CHECK_VERSION (2, 50, 0)
  g_log_set_writer_func (terminal_log_writer, NULL, NULL);
#endif

  g_set_prgname ("gterminalclient");

  setlocale (LC_ALL, "");

  terminal_i18n_init (TRUE);

  _terminal_debug_init ();

  /* Make a NULL-terminated copy since we may need it later */
  gs_free char **argv_copy = g_new (char *, argc + 1);
  int i;
  for (i = 0; i < argc; ++i)
    argv_copy [i] = argv [i];
  argv_copy [i] = NULL;

  gs_free_error GError *error = NULL;
  gs_free_options TerminalOptions *options = terminal_options_parse (&argc, &argv, &error);
  if (options == NULL) {
    terminal_printerr (_("Failed to parse arguments: %s\n"), error->message);
    return exit_code;
  }

  g_set_application_name (_("Terminal"));

  gs_unref_object TerminalFactory *factory = NULL;
  gs_free char *service_name = NULL;
  gs_free char *parent_screen_object_path = NULL;
  if (!factory_proxy_new (options,
                          &factory,
                          &service_name,
                          &parent_screen_object_path,
                          &error))
    return exit_code;

  if (options->print_environment) {
    const char *name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (factory));
    if (name_owner != NULL)
      g_print ("%s=%s\n", TERMINAL_ENV_SERVICE_NAME, name_owner);
    else
      return exit_code;
  }

  TerminalReceiver *receiver = NULL;
  if (!handle_options (options, factory, service_name, parent_screen_object_path, &receiver))
    return exit_code;

  if (receiver != NULL) {
    exit_code = run_receiver (receiver);
    g_object_unref (receiver);
  } else
    exit_code = EXIT_SUCCESS;

  return exit_code;
}
