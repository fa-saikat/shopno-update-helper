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

#ifndef __SHOPNO_UPDATE_WINDOW_PREVIEW_H__
#define __SHOPNO_UPDATE_WINDOW_PREVIEW_H__

#include "shopno-update-window.h"

G_BEGIN_DECLS

/* For tools/shopno-update-preview only */

/* A window that does not start checking */
GtkWidget *shopno_update_window_new_preview (GtkApplication     *application);

/* Shows @page with sample data. Returns FALSE for an unknown page. */
gboolean   shopno_update_window_preview     (ShopnoUpdateWindow *window,
                                             const gchar        *page);

G_END_DECLS

#endif /* !__SHOPNO_UPDATE_WINDOW_PREVIEW_H__ */
