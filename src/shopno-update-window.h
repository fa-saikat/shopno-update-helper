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

#ifndef __SHOPNO_UPDATE_WINDOW_H__
#define __SHOPNO_UPDATE_WINDOW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SHOPNO_TYPE_UPDATE_WINDOW (shopno_update_window_get_type ())
G_DECLARE_FINAL_TYPE (ShopnoUpdateWindow, shopno_update_window, SHOPNO, UPDATE_WINDOW, GtkApplicationWindow)

/* Starts checking for updates as soon as it is created */
GtkWidget *shopno_update_window_new (GtkApplication *application);

/* Loads the stylesheet and icons; call once at startup */
void       shopno_update_load_style (void);

G_END_DECLS

#endif /* !__SHOPNO_UPDATE_WINDOW_H__ */
