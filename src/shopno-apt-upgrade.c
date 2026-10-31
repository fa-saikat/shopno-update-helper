/*
 * Copyright (C) 2026 JaduPc
 * Copyright (C) 2026 FA Saikat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "shopno-apt.h"
#include "shopno-apt-upgrade.h"

/* How long the terminal must stay quiet before checking whether it is
 * waiting for an answer */
#define QUIET_INTERVAL_MS 1500

/* Downloading fills the first half of the progress, installing the rest */
#define DOWNLOAD_SHARE 0.5

enum
{
  PROGRESS,
  INPUT_NEEDED,
  FINISHED,
  N_SIGNALS
};

struct _ShopnoAptUpgrade
{
  GObject __parent__;

  VteTerminal *terminal;
  gulong contents_changed_id;
  gulong commit_id;
  gulong child_exited_id;

  gboolean running;

  /* Status FIFO. keep_fd holds a writer open so reads never see EOF
   * while the helper is still starting. */
  gchar *tmp_dir;
  gchar *fifo_path;
  gint read_fd;
  gint keep_fd;
  GIOChannel *channel;
  guint watch_id;

  guint quiet_id;
  gint64 last_status_time;
  gboolean waiting_for_input;

  gboolean installing;
  gdouble download_percent;
  gdouble install_percent;
  gchar *package;
  GString *errors;
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (ShopnoAptUpgrade, shopno_apt_upgrade, G_TYPE_OBJECT)



static void
stop_status (ShopnoAptUpgrade *self)
{
  g_clear_handle_id (&self->watch_id, g_source_remove);
  g_clear_handle_id (&self->quiet_id, g_source_remove);
  g_clear_pointer (&self->channel, g_io_channel_unref);

  if (self->read_fd >= 0)
    close (self->read_fd);
  if (self->keep_fd >= 0)
    close (self->keep_fd);
  self->read_fd = -1;
  self->keep_fd = -1;

  if (self->fifo_path != NULL)
    g_unlink (self->fifo_path);
  if (self->tmp_dir != NULL)
    g_rmdir (self->tmp_dir);
  g_clear_pointer (&self->fifo_path, g_free);
  g_clear_pointer (&self->tmp_dir, g_free);
}



static void
set_waiting (ShopnoAptUpgrade *self,
             gboolean          waiting)
{
  if (self->waiting_for_input == waiting)
    return;

  self->waiting_for_input = waiting;
  g_signal_emit (self, signals[INPUT_NEEDED], 0, waiting, self->package);
}



static void
emit_progress (ShopnoAptUpgrade *self,
               const gchar      *text)
{
  gdouble fraction;

  if (self->installing)
    fraction = DOWNLOAD_SHARE + (1.0 - DOWNLOAD_SHARE) * self->install_percent / 100.0;
  else
    fraction = DOWNLOAD_SHARE * self->download_percent / 100.0;

  g_signal_emit (self, signals[PROGRESS], 0, CLAMP (fraction, 0.0, 1.0), text);
}



static void
handle_status_line (ShopnoAptUpgrade *self,
                    const gchar      *line)
{
  ShopnoAptStatus status;

  if (!shopno_apt_parse_status (line, &status))
    return;

  self->last_status_time = g_get_monotonic_time ();

  switch (status.kind)
    {
    case SHOPNO_APT_STATUS_DOWNLOAD:
      if (!self->installing)
        {
          self->download_percent = status.percent;
          emit_progress (self, status.message);
        }
      set_waiting (self, FALSE);
      break;

    case SHOPNO_APT_STATUS_INSTALL:
      self->installing = TRUE;
      self->install_percent = status.percent;
      if (status.package != NULL)
        {
          g_free (self->package);
          self->package = g_strdup (status.package);
        }
      emit_progress (self, status.message);
      set_waiting (self, FALSE);
      break;

    case SHOPNO_APT_STATUS_CONFFILE:
    case SHOPNO_APT_STATUS_MEDIA_CHANGE:
      set_waiting (self, TRUE);
      break;

    case SHOPNO_APT_STATUS_ERROR:
      if (status.package != NULL)
        g_string_append_printf (self->errors, "%s: %s\n", status.package, status.message);
      else
        g_string_append_printf (self->errors, "%s\n", status.message);
      break;
    }

  shopno_apt_status_clear (&status);
}



static void
read_status (ShopnoAptUpgrade *self)
{
  if (self->channel == NULL)
    return;

  for (;;)
    {
      g_autofree gchar *line = NULL;
      gsize terminator;
      GIOStatus status;

      status = g_io_channel_read_line (self->channel, &line, NULL, &terminator, NULL);
      if (status != G_IO_STATUS_NORMAL || line == NULL)
        break;

      line[terminator] = '\0';
      handle_status_line (self, line);
    }
}



static gboolean
on_status_readable (GIOChannel   *channel,
                    GIOCondition  condition,
                    gpointer      user_data)
{
  read_status (SHOPNO_APT_UPGRADE (user_data));
  return G_SOURCE_CONTINUE;
}



static gchar *
get_screen_text (VteTerminal *terminal)
{
#if VTE_CHECK_VERSION (0, 76, 0)
  return vte_terminal_get_text_format (terminal, VTE_FORMAT_TEXT);
#else
  return vte_terminal_get_text (terminal, NULL, NULL, NULL);
#endif
}



static gboolean
on_quiet (gpointer user_data)
{
  ShopnoAptUpgrade *self = user_data;
  g_autofree gchar *screen = NULL;

  /* apt is still reporting progress, so it is not waiting */
  if (g_get_monotonic_time () - self->last_status_time < QUIET_INTERVAL_MS * 1000)
    return G_SOURCE_CONTINUE;

  self->quiet_id = 0;

  screen = get_screen_text (self->terminal);
  if (shopno_apt_screen_needs_input (screen))
    set_waiting (self, TRUE);

  return G_SOURCE_REMOVE;
}



static void
on_contents_changed (VteTerminal      *terminal,
                     ShopnoAptUpgrade *self)
{
  if (!self->running)
    return;

  g_clear_handle_id (&self->quiet_id, g_source_remove);
  self->quiet_id = g_timeout_add (QUIET_INTERVAL_MS, on_quiet, self);
}



/* The user typed an answer */
static void
on_commit (VteTerminal      *terminal,
           const gchar      *text,
           guint             size,
           ShopnoAptUpgrade *self)
{
  if (self->running)
    set_waiting (self, FALSE);
}



static void
finish (ShopnoAptUpgrade *self,
        gint              exit_status,
        const gchar      *message)
{
  if (!self->running)
    return;

  read_status (self);
  stop_status (self);
  self->running = FALSE;
  set_waiting (self, FALSE);

  if (message != NULL)
    g_string_append_printf (self->errors, "%s\n", message);

  g_signal_emit (self, signals[FINISHED], 0, exit_status,
                 self->errors->len > 0 ? self->errors->str : NULL);
}



static void
on_child_exited (VteTerminal      *terminal,
                 gint              wait_status,
                 ShopnoAptUpgrade *self)
{
  if (WIFEXITED (wait_status))
    finish (self, WEXITSTATUS (wait_status), NULL);
  else
    finish (self, SHOPNO_APT_UPGRADE_SPAWN_FAILED, _("The update process was stopped."));
}



static void
on_spawned (VteTerminal *terminal,
            GPid         pid,
            GError      *error,
            gpointer     user_data)
{
  ShopnoAptUpgrade *self = user_data;

  if (error != NULL)
    finish (self, SHOPNO_APT_UPGRADE_SPAWN_FAILED, error->message);

  g_object_unref (self);
}



static gboolean
open_status (ShopnoAptUpgrade  *self,
             GError           **error)
{
  self->tmp_dir = g_build_filename (g_get_user_runtime_dir (), "shopno-update-helper-XXXXXX", NULL);
  if (g_mkdtemp_full (self->tmp_dir, 0700) == NULL)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "%s: %s", self->tmp_dir, g_strerror (errno));
      g_clear_pointer (&self->tmp_dir, g_free);
      return FALSE;
    }

  self->fifo_path = g_build_filename (self->tmp_dir, "status", NULL);
  if (mkfifo (self->fifo_path, 0600) < 0
      || (self->read_fd = open (self->fifo_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0
      || (self->keep_fd = open (self->fifo_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "%s: %s", self->fifo_path, g_strerror (errno));
      return FALSE;
    }

  self->channel = g_io_channel_unix_new (self->read_fd);
  g_io_channel_set_encoding (self->channel, NULL, NULL);
  g_io_channel_set_flags (self->channel, G_IO_FLAG_NONBLOCK, NULL);
  self->watch_id = g_io_add_watch (self->channel, G_IO_IN | G_IO_HUP, on_status_readable, self);

  return TRUE;
}



void
shopno_apt_upgrade_start (ShopnoAptUpgrade *self)
{
  g_autoptr(GError) error = NULL;
  gchar *argv[] = { (gchar *) "pkexec", (gchar *) UPGRADE_HELPER, NULL, NULL };

  g_return_if_fail (SHOPNO_IS_APT_UPGRADE (self));
  g_return_if_fail (!self->running);

  self->running = TRUE;
  self->installing = FALSE;
  self->download_percent = 0;
  self->install_percent = 0;
  self->last_status_time = 0;
  g_clear_pointer (&self->package, g_free);
  g_string_truncate (self->errors, 0);

  if (!open_status (self, &error))
    {
      finish (self, SHOPNO_APT_UPGRADE_SPAWN_FAILED, error->message);
      return;
    }

  argv[2] = self->fifo_path;

  vte_terminal_reset (self->terminal, TRUE, TRUE);
  vte_terminal_spawn_async (self->terminal, VTE_PTY_DEFAULT, NULL, argv, NULL,
                            G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                            on_spawned, g_object_ref (self));
}



gchar *
shopno_apt_upgrade_get_log (ShopnoAptUpgrade *self)
{
  g_autoptr(GOutputStream) stream = NULL;

  g_return_val_if_fail (SHOPNO_IS_APT_UPGRADE (self), NULL);

  stream = g_memory_output_stream_new_resizable ();
  if (!vte_terminal_write_contents_sync (self->terminal, stream, VTE_WRITE_DEFAULT, NULL, NULL)
      || !g_output_stream_write_all (stream, "", 1, NULL, NULL, NULL)
      || !g_output_stream_close (stream, NULL, NULL))
    return NULL;

  return g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (stream));
}



static void
shopno_apt_upgrade_dispose (GObject *object)
{
  ShopnoAptUpgrade *self = SHOPNO_APT_UPGRADE (object);

  stop_status (self);

  if (self->terminal != NULL)
    {
      g_clear_signal_handler (&self->contents_changed_id, self->terminal);
      g_clear_signal_handler (&self->commit_id, self->terminal);
      g_clear_signal_handler (&self->child_exited_id, self->terminal);
      g_clear_object (&self->terminal);
    }

  G_OBJECT_CLASS (shopno_apt_upgrade_parent_class)->dispose (object);
}



static void
shopno_apt_upgrade_finalize (GObject *object)
{
  ShopnoAptUpgrade *self = SHOPNO_APT_UPGRADE (object);

  g_free (self->package);
  g_string_free (self->errors, TRUE);

  G_OBJECT_CLASS (shopno_apt_upgrade_parent_class)->finalize (object);
}



static void
shopno_apt_upgrade_class_init (ShopnoAptUpgradeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = shopno_apt_upgrade_dispose;
  object_class->finalize = shopno_apt_upgrade_finalize;

  signals[PROGRESS] =
    g_signal_new ("progress", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_STRING);

  signals[INPUT_NEEDED] =
    g_signal_new ("input-needed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_STRING);

  signals[FINISHED] =
    g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING);
}



static void
shopno_apt_upgrade_init (ShopnoAptUpgrade *self)
{
  self->read_fd = -1;
  self->keep_fd = -1;
  self->errors = g_string_new (NULL);
}



ShopnoAptUpgrade *
shopno_apt_upgrade_new (VteTerminal *terminal)
{
  ShopnoAptUpgrade *self;

  g_return_val_if_fail (VTE_IS_TERMINAL (terminal), NULL);

  self = g_object_new (SHOPNO_TYPE_APT_UPGRADE, NULL);
  self->terminal = g_object_ref (terminal);

  self->contents_changed_id =
    g_signal_connect (terminal, "contents-changed", G_CALLBACK (on_contents_changed), self);
  self->commit_id =
    g_signal_connect (terminal, "commit", G_CALLBACK (on_commit), self);
  self->child_exited_id =
    g_signal_connect (terminal, "child-exited", G_CALLBACK (on_child_exited), self);

  return self;
}
