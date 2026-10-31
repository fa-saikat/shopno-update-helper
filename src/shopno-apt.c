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

#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>

#include "shopno-apt.h"

/* pkexec exit codes */
#define PKEXEC_DISMISSED      126
#define PKEXEC_NOT_AUTHORIZED 127

/* Packages whose update usually means the running system is out of date
 * until a restart. Debian does not create /run/reboot-required on its
 * own, so this list is checked as well. */
static const gchar *const restart_prefixes[] = {
  "linux-image-",
  "linux-firmware",
  "firmware-",
  "intel-microcode",
  "amd64-microcode",
  "nvidia-kernel-",
  "libc6",
  "systemd",
  "dbus",
  NULL
};

G_DEFINE_QUARK (shopno-apt-error-quark, shopno_apt_error)



void
shopno_apt_change_free (ShopnoAptChange *change)
{
  if (change == NULL)
    return;

  g_free (change->name);
  g_free (change->old_version);
  g_free (change->new_version);
  g_free (change);
}



void
shopno_apt_status_clear (ShopnoAptStatus *status)
{
  g_clear_pointer (&status->package, g_free);
  g_clear_pointer (&status->message, g_free);
}



void
shopno_apt_check_free (ShopnoAptCheck *check)
{
  if (check == NULL)
    return;

  g_clear_pointer (&check->changes, g_ptr_array_unref);
  g_free (check);
}



static ShopnoAptChange *
change_new (ShopnoAptChangeKind  kind,
            gchar               *name,
            gchar               *old_version,
            gchar               *new_version)
{
  ShopnoAptChange *change = g_new0 (ShopnoAptChange, 1);

  change->kind = kind;
  change->name = name;
  change->old_version = old_version;
  change->new_version = new_version;

  return change;
}



static gint
compare_changes (gconstpointer a,
                 gconstpointer b)
{
  const ShopnoAptChange *x = *(ShopnoAptChange *const *) a;
  const ShopnoAptChange *y = *(ShopnoAptChange *const *) b;

  if (x->kind != y->kind)
    return (gint) x->kind - (gint) y->kind;

  return g_strcmp0 (x->name, y->name);
}



/* Parses one action line of a simulated run:
 *   Inst faac [1.30-1] (1.31.1+dfsg-1 Debian:13.7/stable [amd64]) []
 *   Inst libfaac0t64 (1.31.1+dfsg-1 Debian:13.7/stable [amd64])
 *   Remv libfaac0 [1.30-1] []
 */
static ShopnoAptChange *
parse_action_line (const gchar *line)
{
  ShopnoAptChangeKind kind;
  const gchar *p;
  const gchar *end;
  gchar *name;
  gchar *old_version = NULL;
  gchar *new_version = NULL;

  if (g_str_has_prefix (line, "Inst "))
    kind = SHOPNO_APT_CHANGE_UPGRADE;
  else if (g_str_has_prefix (line, "Remv "))
    kind = SHOPNO_APT_CHANGE_REMOVE;
  else
    return NULL;

  p = line + 5;
  end = strchr (p, ' ');
  name = end != NULL ? g_strndup (p, end - p) : g_strdup (p);

  if (end != NULL)
    {
      p = end + 1;

      if (*p == '[' && (end = strchr (p, ']')) != NULL)
        {
          old_version = g_strndup (p + 1, end - p - 1);
          for (p = end + 1; *p == ' '; p++);
        }

      if (*p == '(' && (end = strpbrk (p + 1, " )")) != NULL)
        new_version = g_strndup (p + 1, end - p - 1);
    }

  if (kind == SHOPNO_APT_CHANGE_UPGRADE && old_version == NULL)
    kind = SHOPNO_APT_CHANGE_INSTALL;

  return change_new (kind, name, old_version, new_version);
}



GPtrArray *
shopno_apt_parse_simulation (const gchar *output,
                             guint       *kept_back)
{
  GPtrArray *changes = g_ptr_array_new_with_free_func ((GDestroyNotify) shopno_apt_change_free);
  g_auto(GStrv) lines = g_strsplit (output != NULL ? output : "", "\n", -1);
  guint not_upgraded = 0;

  for (guint i = 0; lines[i] != NULL; i++)
    {
      ShopnoAptChange *change = parse_action_line (lines[i]);
      const gchar *p;

      if (change != NULL)
        g_ptr_array_add (changes, change);
      else if (sscanf (lines[i], "%*u upgraded, %*u newly installed, %*u to remove and %u not upgraded",
                       &not_upgraded) == 1)
        ;
      else if ((p = strstr (lines[i], "Not Upgrading: ")) != NULL)
        not_upgraded = (guint) g_ascii_strtoull (p + strlen ("Not Upgrading: "), NULL, 10);
    }

  g_ptr_array_sort (changes, compare_changes);

  if (kept_back != NULL)
    *kept_back = not_upgraded;

  return changes;
}



/* Each line of `apt-get -qq --print-uris` is: 'URI' file size hash */
guint64
shopno_apt_parse_download_size (const gchar *output)
{
  g_auto(GStrv) lines = g_strsplit (output != NULL ? output : "", "\n", -1);
  guint64 total = 0;

  for (guint i = 0; lines[i] != NULL; i++)
    {
      g_auto(GStrv) fields = NULL;

      if (lines[i][0] != '\'')
        continue;

      fields = g_strsplit (lines[i], " ", 4);
      if (g_strv_length (fields) >= 3)
        total += g_ascii_strtoull (fields[2], NULL, 10);
    }

  return total;
}



/* Lines on APT::Status-Fd look like
 *   dlstatus:3:42.5:Retrieving file 3 of 6
 *   pmstatus:code:amd64:62.5:Installing code (amd64)
 *   pmerror:/var/cache/apt/archives/foo.deb:80:subprocess failed
 *   pmconffile:/etc/foo.conf '/etc/foo.conf' '/etc/foo.conf.dpkg-new' 1 1
 * The package field may contain ':' (an architecture), and so may the
 * message, so the percentage is found as the first numeric field. */
gboolean
shopno_apt_parse_status (const gchar     *line,
                         ShopnoAptStatus *status)
{
  g_auto(GStrv) fields = NULL;
  guint n_fields;
  guint i;

  memset (status, 0, sizeof (*status));

  if (g_str_has_prefix (line, "dlstatus:"))
    status->kind = SHOPNO_APT_STATUS_DOWNLOAD;
  else if (g_str_has_prefix (line, "pmstatus:"))
    status->kind = SHOPNO_APT_STATUS_INSTALL;
  else if (g_str_has_prefix (line, "pmerror:"))
    status->kind = SHOPNO_APT_STATUS_ERROR;
  else if (g_str_has_prefix (line, "pmconffile:"))
    status->kind = SHOPNO_APT_STATUS_CONFFILE;
  else if (g_str_has_prefix (line, "media-change:"))
    status->kind = SHOPNO_APT_STATUS_MEDIA_CHANGE;
  else
    return FALSE;

  if (status->kind == SHOPNO_APT_STATUS_CONFFILE)
    {
      status->message = g_strchomp (g_strdup (line + strlen ("pmconffile:")));
      return TRUE;
    }

  fields = g_strsplit (line, ":", -1);
  n_fields = g_strv_length (fields);

  for (i = 2; i < n_fields; i++)
    {
      gchar *end;

      status->percent = g_ascii_strtod (fields[i], &end);
      if (end != fields[i] && *end == '\0')
        break;
    }

  if (i >= n_fields)
    {
      status->percent = 0;
      if (status->kind == SHOPNO_APT_STATUS_DOWNLOAD
          || status->kind == SHOPNO_APT_STATUS_INSTALL)
        return FALSE;

      status->message = g_strchomp (g_strdup (strchr (line, ':') + 1));
      return TRUE;
    }

  if (status->kind != SHOPNO_APT_STATUS_DOWNLOAD)
    {
      gchar *saved = fields[i];

      fields[i] = NULL;
      status->package = g_strjoinv (":", fields + 1);
      fields[i] = saved;

      /* apt's own steps, not a package */
      if (g_strcmp0 (status->package, "dpkg-exec") == 0)
        g_clear_pointer (&status->package, g_free);
    }

  status->percent = CLAMP (status->percent, 0.0, 100.0);
  status->message = g_strchomp (g_strjoinv (":", fields + MIN (i + 1, n_fields)));

  return TRUE;
}



static void
count_refresh_lines (const gchar *output,
                     guint       *n_ok,
                     guint       *n_failed)
{
  g_auto(GStrv) lines = g_strsplit (output != NULL ? output : "", "\n", -1);

  *n_ok = 0;
  *n_failed = 0;

  for (guint i = 0; lines[i] != NULL; i++)
    {
      if (g_str_has_prefix (lines[i], "Hit:") || g_str_has_prefix (lines[i], "Get:"))
        (*n_ok)++;
      else if (g_str_has_prefix (lines[i], "Err:"))
        (*n_failed)++;
    }
}



/* No source could be reached at all */
gboolean
shopno_apt_refresh_network_failed (const gchar *output)
{
  guint n_ok, n_failed;

  count_refresh_lines (output, &n_ok, &n_failed);

  return n_ok == 0 && n_failed > 0;
}



/* Some sources could not be refreshed, but others could */
gboolean
shopno_apt_refresh_sources_failed (const gchar *output)
{
  guint n_ok, n_failed;

  count_refresh_lines (output, &n_ok, &n_failed);

  return n_failed > 0
         || (output != NULL && strstr (output, "Some index files failed to download") != NULL);
}



/* Run when the terminal has been quiet for a moment. Looks for a
 * debconf dialog (whiptail or dialog draw a box) or a question on the
 * last line, such as dpkg's conffile prompt. */
gboolean
shopno_apt_screen_needs_input (const gchar *screen)
{
  g_auto(GStrv) lines = NULL;
  const gchar *last = NULL;
  g_autofree gchar *trimmed = NULL;

  if (screen == NULL)
    return FALSE;

  if (strstr (screen, "┌") != NULL && strstr (screen, "┘") != NULL)
    return TRUE;

  lines = g_strsplit (screen, "\n", -1);
  for (guint i = 0; lines[i] != NULL; i++)
    {
      if (*g_strstrip (lines[i]) != '\0')
        last = lines[i];
    }

  if (last == NULL)
    return FALSE;

  trimmed = g_strdup (last);

  return g_str_has_suffix (trimmed, "?")
         || g_str_has_suffix (trimmed, ":")
         || g_str_has_suffix (trimmed, "[Y/n]")
         || g_str_has_suffix (trimmed, "[y/N]")
         || g_str_has_suffix (trimmed, "--More--")
         || g_str_has_suffix (trimmed, "(END)")
         || strstr (trimmed, "[default=") != NULL;
}



gchar *
shopno_apt_upstream_version (const gchar *version)
{
  const gchar *colon;
  const gchar *dash;

  if (version == NULL)
    return NULL;

  colon = strchr (version, ':');
  if (colon != NULL)
    version = colon + 1;

  dash = strrchr (version, '-');
  return dash != NULL ? g_strndup (version, dash - version) : g_strdup (version);
}



gchar *
shopno_apt_format_versions (const gchar *old_version,
                            const gchar *new_version)
{
  g_autofree gchar *old_short = shopno_apt_upstream_version (old_version);
  g_autofree gchar *new_short = shopno_apt_upstream_version (new_version);
  const gchar *colon;

  /* Only the Debian revision changed, as with most security updates */
  if (g_strcmp0 (old_short, new_short) == 0)
    {
      if (old_version != NULL && (colon = strchr (old_version, ':')) != NULL)
        old_version = colon + 1;
      if (new_version != NULL && (colon = strchr (new_version, ':')) != NULL)
        new_version = colon + 1;

      return g_strdup_printf ("%s → %s", old_version, new_version);
    }

  return g_strdup_printf ("%s → %s", old_short, new_short);
}



gboolean
shopno_apt_changes_need_restart (GPtrArray *changes)
{
  if (changes == NULL)
    return FALSE;

  for (guint i = 0; i < changes->len; i++)
    {
      ShopnoAptChange *change = g_ptr_array_index (changes, i);

      if (change->kind != SHOPNO_APT_CHANGE_UPGRADE
          && change->kind != SHOPNO_APT_CHANGE_INSTALL)
        continue;

      for (guint j = 0; restart_prefixes[j] != NULL; j++)
        {
          if (g_str_has_prefix (change->name, restart_prefixes[j]))
            return TRUE;
        }
    }

  return FALSE;
}



/*
 * Checking for updates
 */

typedef struct
{
  ShopnoAptCheck *check;
  GString *log;
} CheckData;

static void on_refresh_done  (GObject *source, GAsyncResult *result, gpointer user_data);
static void on_simulate_done (GObject *source, GAsyncResult *result, gpointer user_data);
static void on_uris_done     (GObject *source, GAsyncResult *result, gpointer user_data);



static void
check_data_free (CheckData *data)
{
  shopno_apt_check_free (data->check);
  g_string_free (data->log, TRUE);
  g_free (data);
}



static GSubprocess *
spawn (const gchar *const  *argv,
       GError             **error)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                        | G_SUBPROCESS_FLAGS_STDERR_MERGE);

  /* The output is parsed, so it must not be translated */
  g_subprocess_launcher_setenv (launcher, "LC_ALL", "C", TRUE);

  return g_subprocess_launcher_spawnv (launcher, argv, error);
}



static void
spawn_step (GTask               *task,
            const gchar *const  *argv,
            GAsyncReadyCallback  callback)
{
  g_autoptr(GSubprocess) process = NULL;
  GError *error = NULL;

  process = spawn (argv, &error);
  if (process == NULL)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  g_subprocess_communicate_utf8_async (process, NULL, g_task_get_cancellable (task),
                                       callback, task);
}



/* Returns the exit status, or -1 if the process did not exit normally */
static gint
finish_step (GSubprocess   *process,
             GAsyncResult  *result,
             CheckData     *data,
             GError       **error)
{
  g_autofree gchar *output = NULL;

  if (!g_subprocess_communicate_utf8_finish (process, result, &output, NULL, error))
    return -2;

  if (output != NULL)
    g_string_append (data->log, output);

  if (!g_subprocess_get_if_exited (process))
    return -1;

  return g_subprocess_get_exit_status (process);
}



void
shopno_apt_check_async (GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  const gchar *argv[] = { "pkexec", REFRESH_HELPER, NULL };
  GTask *task;
  CheckData *data;

  data = g_new0 (CheckData, 1);
  data->check = g_new0 (ShopnoAptCheck, 1);
  data->log = g_string_new (NULL);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, shopno_apt_check_async);
  g_task_set_task_data (task, data, (GDestroyNotify) check_data_free);

  spawn_step (task, argv, on_refresh_done);
}



static void
on_refresh_done (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GTask *task = user_data;
  CheckData *data = g_task_get_task_data (task);
  const gchar *argv[] = { "apt-get", "-s", "-o", "Debug::NoLocking=1", "full-upgrade", NULL };
  GError *error = NULL;
  gint status;

  status = finish_step (G_SUBPROCESS (source), result, data, &error);

  if (status == -2)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  if (status == PKEXEC_DISMISSED || status == PKEXEC_NOT_AUTHORIZED)
    {
      g_task_return_new_error (task, SHOPNO_APT_ERROR, SHOPNO_APT_ERROR_AUTH,
                               _("Permission to check for updates was not granted."));
      g_object_unref (task);
      return;
    }

  if (shopno_apt_refresh_network_failed (data->log->str))
    {
      g_task_return_new_error (task, SHOPNO_APT_ERROR, SHOPNO_APT_ERROR_NETWORK,
                               _("Check your internet connection and try again."));
      g_object_unref (task);
      return;
    }

  if (status != 0)
    {
      g_task_return_new_error (task, SHOPNO_APT_ERROR, SHOPNO_APT_ERROR_FAILED,
                               _("The package lists could not be refreshed."));
      g_object_unref (task);
      return;
    }

  data->check->sources_failed = shopno_apt_refresh_sources_failed (data->log->str);

  g_string_append (data->log, "\n");
  spawn_step (task, argv, on_simulate_done);
}



static void
on_simulate_done (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GTask *task = user_data;
  CheckData *data = g_task_get_task_data (task);
  const gchar *argv[] = { "apt-get", "-qq", "--print-uris", "-o", "Debug::NoLocking=1", "full-upgrade", NULL };
  GError *error = NULL;
  gsize offset = data->log->len;
  const gchar *output;
  gint status;

  status = finish_step (G_SUBPROCESS (source), result, data, &error);

  if (status == -2)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  output = data->log->str + offset;

  if (status == 0)
    {
      data->check->changes = shopno_apt_parse_simulation (output, &data->check->kept_back);
    }
  else
    {
      /* The upgrade helper repairs these before upgrading, so offer that
       * instead of failing */
      const gchar *repair = NULL;

      if (strstr (output, "dpkg was interrupted") != NULL)
        repair = _("Finish an interrupted installation");
      else if (strstr (output, "Unmet dependencies") != NULL || strstr (output, "--fix-broken") != NULL)
        repair = _("Repair broken packages");

      if (repair == NULL)
        {
          g_task_return_new_error (task, SHOPNO_APT_ERROR, SHOPNO_APT_ERROR_FAILED,
                                   _("The list of updates could not be worked out."));
          g_object_unref (task);
          return;
        }

      data->check->changes = g_ptr_array_new_with_free_func ((GDestroyNotify) shopno_apt_change_free);
      g_ptr_array_add (data->check->changes,
                       change_new (SHOPNO_APT_CHANGE_REPAIR, g_strdup (repair), NULL, NULL));
    }

  if (data->check->changes->len == 0)
    {
      g_task_return_pointer (task, g_steal_pointer (&data->check),
                             (GDestroyNotify) shopno_apt_check_free);
      g_object_unref (task);
      return;
    }

  spawn_step (task, argv, on_uris_done);
}



static void
on_uris_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  GTask *task = user_data;
  CheckData *data = g_task_get_task_data (task);
  g_autofree gchar *output = NULL;
  GError *error = NULL;

  /* Not logged: it is one line per file and only used for the size */
  if (!g_subprocess_communicate_utf8_finish (G_SUBPROCESS (source), result, &output, NULL, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, error);
          g_object_unref (task);
          return;
        }

      g_clear_error (&error);
    }

  data->check->download_size = shopno_apt_parse_download_size (output);

  g_task_return_pointer (task, g_steal_pointer (&data->check),
                         (GDestroyNotify) shopno_apt_check_free);
  g_object_unref (task);
}



ShopnoAptCheck *
shopno_apt_check_finish (GAsyncResult  *result,
                         gchar        **log,
                         GError       **error)
{
  CheckData *data;

  g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);

  data = g_task_get_task_data (G_TASK (result));
  if (log != NULL)
    *log = g_strdup (data->log->str);

  return g_task_propagate_pointer (G_TASK (result), error);
}
