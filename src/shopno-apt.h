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

#ifndef __SHOPNO_APT_H__
#define __SHOPNO_APT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define SHOPNO_APT_ERROR (shopno_apt_error_quark ())

typedef enum
{
  SHOPNO_APT_ERROR_FAILED,
  SHOPNO_APT_ERROR_AUTH,
  SHOPNO_APT_ERROR_NETWORK,
} ShopnoAptError;

/* In display order */
typedef enum
{
  SHOPNO_APT_CHANGE_REPAIR,
  SHOPNO_APT_CHANGE_UPGRADE,
  SHOPNO_APT_CHANGE_INSTALL,
  SHOPNO_APT_CHANGE_REMOVE,
} ShopnoAptChangeKind;

typedef struct
{
  ShopnoAptChangeKind kind;
  gchar *name;
  gchar *old_version;
  gchar *new_version;
} ShopnoAptChange;

typedef enum
{
  SHOPNO_APT_STATUS_DOWNLOAD,
  SHOPNO_APT_STATUS_INSTALL,
  SHOPNO_APT_STATUS_CONFFILE,
  SHOPNO_APT_STATUS_ERROR,
  SHOPNO_APT_STATUS_MEDIA_CHANGE,
} ShopnoAptStatusKind;

typedef struct
{
  ShopnoAptStatusKind kind;
  gdouble percent;
  gchar *package;
  gchar *message;
} ShopnoAptStatus;

/* Result of shopno_apt_check_async() */
typedef struct
{
  GPtrArray *changes;     /* ShopnoAptChange, in display order */
  guint64 download_size;  /* bytes still to download */
  guint kept_back;        /* upgradable, but held back by apt */
  gboolean sources_failed; /* some package sources could not be refreshed */
} ShopnoAptCheck;

GQuark          shopno_apt_error_quark             (void);

void            shopno_apt_change_free             (ShopnoAptChange *change);
void            shopno_apt_status_clear            (ShopnoAptStatus *status);
void            shopno_apt_check_free              (ShopnoAptCheck  *check);

/* Parsers, kept free of I/O so they can be unit tested. Each expects
 * output produced in the C locale. */
GPtrArray      *shopno_apt_parse_simulation        (const gchar     *output,
                                                    guint           *kept_back);
guint64         shopno_apt_parse_download_size     (const gchar     *output);
gboolean        shopno_apt_parse_status            (const gchar     *line,
                                                    ShopnoAptStatus *status);
gboolean        shopno_apt_refresh_network_failed  (const gchar     *output);
gboolean        shopno_apt_refresh_sources_failed  (const gchar     *output);
gboolean        shopno_apt_screen_needs_input      (const gchar     *screen);

/* "1:2.4.1-3+deb13u1" → "2.4.1": no epoch, no Debian revision */
gchar          *shopno_apt_upstream_version        (const gchar     *version);

/* "old → new" for display, as short as still shows what changed */
gchar          *shopno_apt_format_versions         (const gchar     *old_version,
                                                    const gchar     *new_version);

/* Returns TRUE if one of the changes usually needs a restart */
gboolean        shopno_apt_changes_need_restart    (GPtrArray       *changes);

/* Refreshes the package lists through pkexec, then works out what
 * `apt-get full-upgrade` would change. */
void            shopno_apt_check_async             (GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);
ShopnoAptCheck *shopno_apt_check_finish            (GAsyncResult        *result,
                                                    gchar              **log,
                                                    GError             **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ShopnoAptCheck, shopno_apt_check_free)

G_END_DECLS

#endif /* !__SHOPNO_APT_H__ */
