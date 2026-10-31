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

#ifndef __SHOPNO_APT_UPGRADE_H__
#define __SHOPNO_APT_UPGRADE_H__

#include <vte/vte.h>

G_BEGIN_DECLS

/* Exit statuses passed to ::finished besides the helper's own */
#define SHOPNO_APT_UPGRADE_DISMISSED      126 /* authentication dialog dismissed */
#define SHOPNO_APT_UPGRADE_NOT_AUTHORIZED 127 /* not authorized, or no agent */
#define SHOPNO_APT_UPGRADE_SPAWN_FAILED   (-1)

#define SHOPNO_TYPE_APT_UPGRADE (shopno_apt_upgrade_get_type ())
G_DECLARE_FINAL_TYPE (ShopnoAptUpgrade, shopno_apt_upgrade, SHOPNO, APT_UPGRADE, GObject)

/*
 * Runs the upgrade helper inside @terminal, so anything a package asks
 * (debconf dialogs, conffile prompts) can be answered there.
 *
 * Signals:
 *   progress     (gdouble fraction, const gchar *text)
 *   input-needed (gboolean needed, const gchar *package or NULL)
 *   finished     (gint exit_status, const gchar *errors or NULL)
 */
ShopnoAptUpgrade *shopno_apt_upgrade_new     (VteTerminal      *terminal);
void              shopno_apt_upgrade_start   (ShopnoAptUpgrade *upgrade);

/* Everything the terminal showed, for error reports */
gchar            *shopno_apt_upgrade_get_log (ShopnoAptUpgrade *upgrade);

G_END_DECLS

#endif /* !__SHOPNO_APT_UPGRADE_H__ */
