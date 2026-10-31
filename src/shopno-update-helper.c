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

#include <locale.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "shopno-update-window.h"

static void
on_startup (GApplication *application)
{
  shopno_update_load_style ();
}



static void
on_activate (GtkApplication *application)
{
  GtkWindow *window = gtk_application_get_active_window (application);

  if (window == NULL)
    window = GTK_WINDOW (shopno_update_window_new (application));

  gtk_window_present (window);
}



int
main (int    argc,
      char **argv)
{
  g_autoptr(GtkApplication) application = NULL;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_set_application_name (_("Update Helper"));

  /* G_APPLICATION_DEFAULT_FLAGS needs GLib 2.74; Ubuntu 22.04 has 2.72 */
  application = gtk_application_new (APPLICATION_ID, (GApplicationFlags) 0);
  g_signal_connect (application, "startup", G_CALLBACK (on_startup), NULL);
  g_signal_connect (application, "activate", G_CALLBACK (on_activate), NULL);

  return g_application_run (G_APPLICATION (application), argc, argv);
}
