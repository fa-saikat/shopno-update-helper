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

/*
 * Opens one page of Update Helper with sample data, for layout and style
 * work and screenshots. Nothing is checked or installed, and buttons
 * that would start work do nothing useful.
 *
 *   shopno-update-preview [PAGE]
 */

#include "config.h"

#include <gtk/gtk.h>

#include "shopno-update-window-preview.h"

static const gchar *const pages[] = {
  "checking",
  "updates",
  "updates-no-flatpak",
  "installing",
  "input",
  "done",
  "done-restart",
  "up-to-date",
  "partial",
  "error",
  NULL
};

static const gchar *page = "updates";



static void
on_activate (GtkApplication *application)
{
  GtkWidget *window;

  shopno_update_load_style ();

  window = shopno_update_window_new_preview (application);
  shopno_update_window_preview (SHOPNO_UPDATE_WINDOW (window), page);
  gtk_window_present (GTK_WINDOW (window));
}



int
main (int    argc,
      char **argv)
{
  g_autoptr(GtkApplication) application = NULL;

  if (argc > 2 || (argc == 2 && !g_strv_contains (pages, argv[1])))
    {
      g_autofree gchar *list = g_strjoinv (", ", (gchar **) pages);

      g_printerr ("Usage: %s [PAGE]\nPages: %s\n", argv[0], list);
      return 2;
    }

  if (argc == 2)
    page = argv[1];

  application = gtk_application_new (APPLICATION_ID ".Preview", G_APPLICATION_NON_UNIQUE);
  g_signal_connect (application, "activate", G_CALLBACK (on_activate), NULL);

  return g_application_run (G_APPLICATION (application), 1, argv);
}
