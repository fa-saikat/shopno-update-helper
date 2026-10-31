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

#ifndef __SHOPNO_FLATPAK_H__
#define __SHOPNO_FLATPAK_H__

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct
{
  GObject *installation; /* FlatpakInstallation */
  gchar *ref;            /* app/org.example.App/x86_64/stable */
  gchar *id;             /* org.example.App */
  gchar *name;           /* name to show */
  gboolean is_app;       /* FALSE for runtimes and extensions */
} ShopnoFlatpakUpdate;

/* Called in the main thread. @log_line, if not NULL, is one line for
 * the details log. */
typedef void (*ShopnoFlatpakProgressFunc) (gdouble      fraction,
                                           const gchar *text,
                                           const gchar *log_line,
                                           gpointer     user_data);

void       shopno_flatpak_update_free   (ShopnoFlatpakUpdate *update);

/* FALSE when built without Flatpak support or Flatpak is not installed */
gboolean   shopno_flatpak_is_available  (void);

/* Lists installed apps and runtimes with an update. Apps come first. */
void       shopno_flatpak_check_async   (GCancellable               *cancellable,
                                         GAsyncReadyCallback         callback,
                                         gpointer                    user_data);
GPtrArray *shopno_flatpak_check_finish  (GAsyncResult               *result,
                                         GError                    **error);

/* Installs @updates. On return, @n_updated is the number of operations
 * that completed, and @running_apps the names of updated apps that are
 * still running the old version. Both are set even on failure. */
void       shopno_flatpak_update_async  (GPtrArray                  *updates,
                                         ShopnoFlatpakProgressFunc   progress,
                                         gpointer                    progress_data,
                                         GCancellable               *cancellable,
                                         GAsyncReadyCallback         callback,
                                         gpointer                    user_data);
gboolean   shopno_flatpak_update_finish (GAsyncResult               *result,
                                         guint                      *n_updated,
                                         gchar                    ***running_apps,
                                         GError                    **error);

G_END_DECLS

#endif /* !__SHOPNO_FLATPAK_H__ */
