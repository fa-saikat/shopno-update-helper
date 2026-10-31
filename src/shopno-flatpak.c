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

#include <glib/gi18n.h>

#if HAVE_FLATPAK
#include <flatpak.h>
#endif

#include "shopno-flatpak.h"



void
shopno_flatpak_update_free (ShopnoFlatpakUpdate *update)
{
  if (update == NULL)
    return;

  g_clear_object (&update->installation);
  g_free (update->ref);
  g_free (update->id);
  g_free (update->name);
  g_free (update);
}



#if HAVE_FLATPAK

gboolean
shopno_flatpak_is_available (void)
{
  g_autofree gchar *flatpak = g_find_program_in_path ("flatpak");

  return flatpak != NULL;
}



static gint
compare_updates (gconstpointer a,
                 gconstpointer b)
{
  const ShopnoFlatpakUpdate *x = *(ShopnoFlatpakUpdate *const *) a;
  const ShopnoFlatpakUpdate *y = *(ShopnoFlatpakUpdate *const *) b;

  if (x->is_app != y->is_app)
    return x->is_app ? -1 : 1;

  return g_utf8_collate (x->name, y->name);
}



/* System installations first, then the user's */
static GPtrArray *
get_installations (GCancellable  *cancellable,
                   GError       **error)
{
  g_autoptr(GPtrArray) installations = NULL;
  FlatpakInstallation *user;

  installations = flatpak_get_system_installations (cancellable, error);
  if (installations == NULL)
    return NULL;

  user = flatpak_installation_new_user (cancellable, NULL);
  if (user != NULL)
    g_ptr_array_add (installations, user);

  return g_steal_pointer (&installations);
}



static void
check_thread (GTask        *task,
              gpointer      source_object,
              gpointer      task_data,
              GCancellable *cancellable)
{
  g_autoptr(GPtrArray) installations = NULL;
  g_autoptr(GPtrArray) updates = NULL;
  GError *error = NULL;

  installations = get_installations (cancellable, &error);
  if (installations == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  updates = g_ptr_array_new_with_free_func ((GDestroyNotify) shopno_flatpak_update_free);

  for (guint i = 0; i < installations->len; i++)
    {
      FlatpakInstallation *installation = g_ptr_array_index (installations, i);
      g_autoptr(GPtrArray) refs = NULL;

      refs = flatpak_installation_list_installed_refs_for_update (installation, cancellable, &error);
      if (refs == NULL)
        {
          g_task_return_error (task, error);
          return;
        }

      for (guint j = 0; j < refs->len; j++)
        {
          FlatpakInstalledRef *installed = g_ptr_array_index (refs, j);
          FlatpakRef *ref = FLATPAK_REF (installed);
          ShopnoFlatpakUpdate *update = g_new0 (ShopnoFlatpakUpdate, 1);
          const gchar *name = flatpak_installed_ref_get_appdata_name (installed);

          update->installation = G_OBJECT (g_object_ref (installation));
          update->ref = flatpak_ref_format_ref (ref);
          update->id = g_strdup (flatpak_ref_get_name (ref));
          update->name = g_strdup (name != NULL && *name != '\0' ? name : update->id);
          update->is_app = flatpak_ref_get_kind (ref) == FLATPAK_REF_KIND_APP;

          g_ptr_array_add (updates, update);
        }
    }

  g_ptr_array_sort (updates, compare_updates);
  g_task_return_pointer (task, g_steal_pointer (&updates), (GDestroyNotify) g_ptr_array_unref);
}



void
shopno_flatpak_check_async (GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);

  g_task_set_source_tag (task, shopno_flatpak_check_async);
  g_task_run_in_thread (task, check_thread);
}



GPtrArray *
shopno_flatpak_check_finish (GAsyncResult  *result,
                             GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}



/*
 * Installing updates
 */

typedef struct
{
  GPtrArray *updates;
  GHashTable *names;     /* ref → name to show */
  GMainContext *context; /* where progress is reported */
  ShopnoFlatpakProgressFunc progress;
  gpointer progress_data;

  guint n_total;
  guint n_requested; /* by the running transaction */
  guint n_done;
  guint n_failed;
  gchar *current;        /* name of the running operation */
  gchar **running_apps;
} UpdateData;

typedef struct
{
  UpdateData *data;
  gdouble fraction;
  gchar *text;
  gchar *log_line;
} Progress;



static void
update_data_free (UpdateData *data)
{
  g_ptr_array_unref (data->updates);
  g_hash_table_unref (data->names);
  g_main_context_unref (data->context);
  g_free (data->current);
  g_strfreev (data->running_apps);
  g_free (data);
}



static gboolean
report_progress_cb (gpointer user_data)
{
  Progress *progress = user_data;

  progress->data->progress (progress->fraction, progress->text, progress->log_line,
                            progress->data->progress_data);

  g_free (progress->text);
  g_free (progress->log_line);
  g_free (progress);

  return G_SOURCE_REMOVE;
}



/* Called in the worker thread */
static void
report_progress (UpdateData  *data,
                 gdouble      op_percent,
                 const gchar *log_line)
{
  Progress *progress;

  if (data->progress == NULL)
    return;

  progress = g_new0 (Progress, 1);
  progress->data = data;
  progress->fraction = data->n_total > 0
                       ? CLAMP ((data->n_done + op_percent / 100.0) / data->n_total, 0.0, 1.0)
                       : 0.0;
  progress->text = data->current != NULL
                   ? g_strdup_printf (_("Updating %s"), data->current)
                   : g_strdup (_("Preparing…"));
  progress->log_line = g_strdup (log_line);

  g_main_context_invoke (data->context, report_progress_cb, progress);
}



static gchar *
operation_name (UpdateData                  *data,
                FlatpakTransactionOperation *operation)
{
  const gchar *ref = flatpak_transaction_operation_get_ref (operation);
  const gchar *name = g_hash_table_lookup (data->names, ref);
  g_autoptr(FlatpakRef) parsed = NULL;

  if (name != NULL)
    return g_strdup (name);

  /* A related ref the transaction added, such as a locale extension */
  parsed = flatpak_ref_parse (ref, NULL);
  return g_strdup (parsed != NULL ? flatpak_ref_get_name (parsed) : ref);
}



static void
on_progress_changed (FlatpakTransactionProgress *progress,
                     UpdateData                 *data)
{
  report_progress (data, flatpak_transaction_progress_get_progress (progress), NULL);
}



static void
on_new_operation (FlatpakTransaction          *transaction,
                  FlatpakTransactionOperation *operation,
                  FlatpakTransactionProgress  *progress,
                  UpdateData                  *data)
{
  g_free (data->current);
  data->current = operation_name (data, operation);

  g_signal_connect (progress, "changed", G_CALLBACK (on_progress_changed), data);
  report_progress (data, 0, NULL);
}



static void
on_operation_done (FlatpakTransaction          *transaction,
                   FlatpakTransactionOperation *operation,
                   const gchar                 *commit,
                   FlatpakTransactionResult     result,
                   UpdateData                  *data)
{
  g_autofree gchar *name = operation_name (data, operation);
  g_autofree gchar *line = g_strdup_printf (_("Updated %s"), name);

  data->n_done++;
  report_progress (data, 0, line);
}



/* Keep going, so one broken app does not hold back the others */
static gboolean
on_operation_error (FlatpakTransaction          *transaction,
                    FlatpakTransactionOperation *operation,
                    const GError                *error,
                    gint                         details,
                    UpdateData                  *data)
{
  g_autofree gchar *name = operation_name (data, operation);
  g_autofree gchar *line = NULL;

  if (g_error_matches (error, FLATPAK_ERROR, FLATPAK_ERROR_SKIPPED))
    return TRUE;

  line = g_strdup_printf (_("Failed to update %s: %s"), name, error->message);
  data->n_failed++;
  data->n_done++;
  report_progress (data, 0, line);

  return TRUE;
}



/* Correct the total now that related refs, such as locale extensions,
 * are known */
static gboolean
on_ready (FlatpakTransaction *transaction,
          UpdateData         *data)
{
  GList *operations = flatpak_transaction_get_operations (transaction);
  guint n_operations = g_list_length (operations);

  if (n_operations > data->n_requested)
    data->n_total += n_operations - data->n_requested;

  g_list_free_full (operations, g_object_unref);

  return TRUE;
}



static gchar **
find_running_apps (UpdateData *data)
{
  g_autoptr(GPtrArray) instances = flatpak_instance_get_all ();
  g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; i < data->updates->len; i++)
    {
      ShopnoFlatpakUpdate *update = g_ptr_array_index (data->updates, i);

      if (!update->is_app)
        continue;

      for (guint j = 0; j < instances->len; j++)
        {
          FlatpakInstance *instance = g_ptr_array_index (instances, j);

          if (g_strcmp0 (flatpak_instance_get_app (instance), update->id) == 0)
            {
              g_ptr_array_add (names, g_strdup (update->name));
              break;
            }
        }
    }

  g_ptr_array_add (names, NULL);
  return (gchar **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}



static void
update_thread (GTask        *task,
               gpointer      source_object,
               gpointer      task_data,
               GCancellable *cancellable)
{
  UpdateData *data = task_data;
  g_autoptr(GPtrArray) installations = g_ptr_array_new ();
  GError *first_error = NULL;

  /* One transaction per installation, in the order they were found */
  for (guint i = 0; i < data->updates->len; i++)
    {
      ShopnoFlatpakUpdate *update = g_ptr_array_index (data->updates, i);

      if (!g_ptr_array_find (installations, update->installation, NULL))
        g_ptr_array_add (installations, update->installation);
    }

  for (guint i = 0; i < installations->len; i++)
    {
      FlatpakInstallation *installation = g_ptr_array_index (installations, i);
      g_autoptr(FlatpakTransaction) transaction = NULL;
      GError *error = NULL;
      guint n_requested = 0;
      guint n_done_before = data->n_done;

      transaction = flatpak_transaction_new_for_installation (installation, cancellable, &error);
      if (transaction == NULL)
        goto failed;

      for (guint j = 0; j < data->updates->len; j++)
        {
          ShopnoFlatpakUpdate *update = g_ptr_array_index (data->updates, j);

          if (update->installation != G_OBJECT (installation))
            continue;

          if (!flatpak_transaction_add_update (transaction, update->ref, NULL, NULL, &error))
            goto failed;

          n_requested++;
        }

      data->n_requested = n_requested;
      g_signal_connect (transaction, "ready", G_CALLBACK (on_ready), data);
      g_signal_connect (transaction, "new-operation", G_CALLBACK (on_new_operation), data);
      g_signal_connect (transaction, "operation-done", G_CALLBACK (on_operation_done), data);
      g_signal_connect (transaction, "operation-error", G_CALLBACK (on_operation_error), data);

      if (!flatpak_transaction_run (transaction, cancellable, &error))
        goto failed;

      continue;

    failed:
      if (data->n_done == n_done_before)
        data->n_failed += MAX (n_requested, 1);

      if (first_error == NULL)
        first_error = error;
      else
        g_clear_error (&error);
    }

  data->running_apps = find_running_apps (data);

  if (first_error != NULL)
    g_task_return_error (task, first_error);
  else if (data->n_failed > 0)
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                             ngettext ("%u update could not be installed.",
                                       "%u updates could not be installed.",
                                       data->n_failed),
                             data->n_failed);
  else
    g_task_return_boolean (task, TRUE);
}



void
shopno_flatpak_update_async (GPtrArray                 *updates,
                             ShopnoFlatpakProgressFunc  progress,
                             gpointer                   progress_data,
                             GCancellable              *cancellable,
                             GAsyncReadyCallback        callback,
                             gpointer                   user_data)
{
  g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
  UpdateData *data = g_new0 (UpdateData, 1);

  data->updates = g_ptr_array_ref (updates);
  data->names = g_hash_table_new (g_str_hash, g_str_equal);
  data->context = g_main_context_ref_thread_default ();
  data->progress = progress;
  data->progress_data = progress_data;
  data->n_total = updates->len;

  for (guint i = 0; i < updates->len; i++)
    {
      ShopnoFlatpakUpdate *update = g_ptr_array_index (updates, i);
      g_hash_table_insert (data->names, update->ref, update->name);
    }

  g_task_set_source_tag (task, shopno_flatpak_update_async);
  g_task_set_task_data (task, data, (GDestroyNotify) update_data_free);
  g_task_run_in_thread (task, update_thread);
}



gboolean
shopno_flatpak_update_finish (GAsyncResult   *result,
                              guint          *n_updated,
                              gchar        ***running_apps,
                              GError        **error)
{
  UpdateData *data;

  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);

  data = g_task_get_task_data (G_TASK (result));

  if (n_updated != NULL)
    *n_updated = data->n_done - data->n_failed;
  if (running_apps != NULL)
    *running_apps = g_strdupv (data->running_apps);

  return g_task_propagate_boolean (G_TASK (result), error);
}

#else /* !HAVE_FLATPAK */

gboolean
shopno_flatpak_is_available (void)
{
  return FALSE;
}



void
shopno_flatpak_check_async (GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  g_task_report_new_error (NULL, callback, user_data, shopno_flatpak_check_async,
                           G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "Built without Flatpak support");
}



GPtrArray *
shopno_flatpak_check_finish (GAsyncResult  *result,
                             GError       **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}



void
shopno_flatpak_update_async (GPtrArray                 *updates,
                             ShopnoFlatpakProgressFunc  progress,
                             gpointer                   progress_data,
                             GCancellable              *cancellable,
                             GAsyncReadyCallback        callback,
                             gpointer                   user_data)
{
  g_task_report_new_error (NULL, callback, user_data, shopno_flatpak_update_async,
                           G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "Built without Flatpak support");
}



gboolean
shopno_flatpak_update_finish (GAsyncResult   *result,
                              guint          *n_updated,
                              gchar        ***running_apps,
                              GError        **error)
{
  if (n_updated != NULL)
    *n_updated = 0;
  if (running_apps != NULL)
    *running_apps = NULL;

  return g_task_propagate_boolean (G_TASK (result), error);
}

#endif /* HAVE_FLATPAK */
