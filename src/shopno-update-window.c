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
#include <vte/vte.h>

#include "shopno-apt.h"
#include "shopno-apt-upgrade.h"
#include "shopno-flatpak.h"
#include "shopno-update-window.h"
#include "shopno-update-window-preview.h"

/* The window can't be resized: it takes this size, or more when a page
 * needs it */
#define PAGE_WIDTH     560
#define PAGE_HEIGHT    520

/* Big enough for a debconf dialog */
#define TERMINAL_COLUMNS 80
#define TERMINAL_ROWS    24

#define BIG_ICON_SIZE  64
#define ROW_ICON_SIZE  24

#define REBOOT_REQUIRED_FILE "/run/reboot-required"

#define RESOURCE_PATH "/com/jadupc/ShopnoUpdateHelper"

typedef enum
{
  SOURCE_APT,
  SOURCE_FLATPAK,
  N_SOURCES
} Source;

typedef enum
{
  STATE_BUSY,
  STATE_OK,
  STATE_FAILED,
  STATE_SKIPPED,
} SourceState;

typedef enum
{
  RESULT_NONE,    /* nothing to install */
  RESULT_OK,
  RESULT_FAILED,
} InstallResult;

typedef enum
{
  ACTION_CLOSE,
  ACTION_CHECK,
  ACTION_INSTALL_FLATPAK,
  ACTION_RESTART,
} Action;

/* One line per source on the checking and result pages */
typedef struct
{
  GtkStack *icon;
  GtkImage *image;
  GtkSpinner *spinner;
  GtkLabel *status;
  GtkLabel *detail;
} SourceRow;

struct _ShopnoUpdateWindow
{
  GtkApplicationWindow __parent__;

  GtkStack *stack;
  GCancellable *cancellable;
  gboolean installing;

  /* Checking page */
  SourceRow check_rows[N_SOURCES];

  /* Updates page */
  GtkLabel *updates_title;
  GtkLabel *updates_subtitle;
  GtkLabel *updates_note;
  GtkLabel *updates_hint;
  GtkListBox *updates_list;

  /* Installing page */
  GtkWidget *steps;
  GtkWidget *step[N_SOURCES];
  GtkLabel *step_number[N_SOURCES];
  GtkLabel *install_status;
  GtkLabel *install_percent;
  GtkProgressBar *install_bar;
  GtkWidget *input_bar;
  GtkLabel *input_label;
  GtkExpander *details;
  VteTerminal *terminal;
  ShopnoAptUpgrade *apt_upgrade;

  /* Result page */
  GtkImage *result_icon;
  GtkLabel *result_title;
  GtkLabel *result_message;
  SourceRow result_rows[N_SOURCES];
  GtkWidget *result_sources;
  GtkLabel *result_notice;
  GtkExpander *result_details;
  GtkTextBuffer *result_log;
  GtkWidget *result_secondary;
  GtkButton *result_primary;
  Action primary_action;

  /* What the checks found */
  guint checks_pending;
  ShopnoAptCheck *apt;
  gchar *apt_log;
  GError *apt_error;
  gboolean flatpak_available;
  GPtrArray *flatpak_updates;
  GError *flatpak_error;

  /* What installing did */
  InstallResult apt_result;
  gchar *apt_message;
  InstallResult flatpak_result;
  guint flatpak_updated;
  gchar *flatpak_message;
  gchar **running_apps;
  GString *flatpak_log;
};

G_DEFINE_TYPE (ShopnoUpdateWindow, shopno_update_window, GTK_TYPE_APPLICATION_WINDOW)

static void start_check            (ShopnoUpdateWindow *self);
static void start_install          (ShopnoUpdateWindow *self);
static void install_flatpak        (ShopnoUpdateWindow *self);
static void finish_install         (ShopnoUpdateWindow *self);



/*
 * Widget helpers
 */

static void
add_class (GtkWidget   *widget,
           const gchar *css_class)
{
  gtk_style_context_add_class (gtk_widget_get_style_context (widget), css_class);
}



static void
set_state_class (GtkWidget   *widget,
                 const gchar *css_class)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);

  gtk_style_context_remove_class (context, "shopno-success");
  gtk_style_context_remove_class (context, "shopno-warning");
  gtk_style_context_remove_class (context, "shopno-error");
  gtk_style_context_remove_class (context, "shopno-dim");

  if (css_class != NULL)
    gtk_style_context_add_class (context, css_class);
}



static GtkLabel *
label_new (const gchar *text,
           const gchar *css_class,
           gfloat       xalign)
{
  GtkWidget *label = gtk_label_new (text);

  gtk_label_set_xalign (GTK_LABEL (label), xalign);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  if (css_class != NULL)
    add_class (label, css_class);

  return GTK_LABEL (label);
}



static GtkWidget *
icon_new (const gchar *icon_name,
          gint         size)
{
  GtkWidget *image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_DIALOG);

  gtk_image_set_pixel_size (GTK_IMAGE (image), size);

  return image;
}



static GtkWidget *
page_new (void)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  add_class (page, "shopno-page");

  return page;
}



/* Bordered list, as in the mockups */
static GtkListBox *
list_new (GtkWidget **frame)
{
  GtkWidget *list = gtk_list_box_new ();

  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  add_class (list, "shopno-list");

  *frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (*frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (*frame), list);

  return GTK_LIST_BOX (list);
}



/* Rows are information, not buttons */
static void
list_append (GtkListBox *list,
             GtkWidget  *child)
{
  gtk_list_box_insert (list, child, -1);

  if (!GTK_IS_LIST_BOX_ROW (child))
    child = gtk_widget_get_parent (child);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (child), FALSE);
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (child), FALSE);
}



static void
list_clear (GtkListBox *list)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (list));

  for (GList *li = children; li != NULL; li = li->next)
    gtk_widget_destroy (li->data);

  g_list_free (children);
}



static GtkWidget *
footer_new (GtkWidget **hint)
{
  GtkWidget *footer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);

  add_class (footer, "shopno-footer");

  if (hint != NULL)
    {
      *hint = GTK_WIDGET (label_new (NULL, "shopno-dim", 0));
      add_class (*hint, "shopno-small");
      gtk_box_pack_start (GTK_BOX (footer), *hint, TRUE, TRUE, 0);
    }

  return footer;
}



static GtkWidget *
source_row_new (SourceRow   *row,
                const gchar *title)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *text = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *name;

  add_class (box, "shopno-source");

  row->icon = GTK_STACK (gtk_stack_new ());
  row->spinner = GTK_SPINNER (gtk_spinner_new ());
  row->image = GTK_IMAGE (icon_new ("shopno-success-symbolic", 18));
  gtk_widget_set_size_request (GTK_WIDGET (row->spinner), 18, 18);
  gtk_stack_add_named (row->icon, GTK_WIDGET (row->spinner), "busy");
  gtk_stack_add_named (row->icon, GTK_WIDGET (row->image), "done");
  gtk_widget_set_valign (GTK_WIDGET (row->icon), GTK_ALIGN_CENTER);

  name = GTK_WIDGET (label_new (title, "shopno-strong", 0));
  row->detail = label_new (NULL, "shopno-dim", 0);
  add_class (GTK_WIDGET (row->detail), "shopno-small");
  gtk_widget_set_no_show_all (GTK_WIDGET (row->detail), TRUE);
  gtk_box_pack_start (GTK_BOX (text), name, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (text), GTK_WIDGET (row->detail), FALSE, FALSE, 0);
  gtk_widget_set_valign (text, GTK_ALIGN_CENTER);

  row->status = label_new (NULL, "shopno-dim", 1);
  gtk_label_set_line_wrap (row->status, FALSE);
  gtk_widget_set_valign (GTK_WIDGET (row->status), GTK_ALIGN_CENTER);

  gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (row->icon), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), text, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (box), GTK_WIDGET (row->status), FALSE, FALSE, 0);

  return box;
}



static void
source_row_set (SourceRow   *row,
                SourceState  state,
                const gchar *status,
                const gchar *detail)
{
  if (state == STATE_BUSY)
    {
      gtk_spinner_start (row->spinner);
      gtk_stack_set_visible_child_name (row->icon, "busy");
    }
  else
    {
      static const gchar *const icons[] = {
        [STATE_OK] = "shopno-success-symbolic",
        [STATE_FAILED] = "shopno-error-symbolic",
        [STATE_SKIPPED] = "shopno-skipped-symbolic",
      };
      static const gchar *const classes[] = {
        [STATE_OK] = "shopno-success",
        [STATE_FAILED] = "shopno-error",
        [STATE_SKIPPED] = "shopno-dim",
      };

      gtk_spinner_stop (row->spinner);
      gtk_image_set_from_icon_name (row->image, icons[state], GTK_ICON_SIZE_BUTTON);
      gtk_image_set_pixel_size (row->image, 18);
      set_state_class (GTK_WIDGET (row->image), classes[state]);
      gtk_stack_set_visible_child_name (row->icon, "done");
    }

  gtk_label_set_text (row->status, status);
  gtk_label_set_text (row->detail, detail);
  gtk_widget_set_visible (GTK_WIDGET (row->detail), detail != NULL && *detail != '\0');
}



static GtkWidget *
sources_new (SourceRow rows[N_SOURCES])
{
  GtkWidget *frame;
  GtkListBox *list = list_new (&frame);

  list_append (list, source_row_new (&rows[SOURCE_APT], _("System packages")));
  list_append (list, source_row_new (&rows[SOURCE_FLATPAK], _("Flatpak apps")));

  return frame;
}



/*
 * Building the pages
 */

static GtkWidget *
build_checking_page (ShopnoUpdateWindow *self)
{
  GtkWidget *page = page_new ();
  GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *spinner = gtk_spinner_new ();
  GtkWidget *footer = footer_new (NULL);
  GtkWidget *cancel = gtk_button_new_with_mnemonic (_("_Cancel"));

  add_class (body, "shopno-body");
  gtk_widget_set_valign (body, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand (body, TRUE);

  gtk_widget_set_size_request (spinner, 48, 48);
  gtk_widget_set_halign (spinner, GTK_ALIGN_CENTER);
  gtk_spinner_start (GTK_SPINNER (spinner));

  gtk_box_pack_start (GTK_BOX (body), spinner, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (label_new (_("Checking for updates"), "shopno-title", 0.5)), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), sources_new (self->check_rows), FALSE, FALSE, 12);

  g_signal_connect_swapped (cancel, "clicked", G_CALLBACK (gtk_widget_destroy), self);
  gtk_box_pack_end (GTK_BOX (footer), cancel, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (page), body, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (page), footer, FALSE, FALSE, 0);

  return page;
}



static GtkWidget *
build_updates_page (ShopnoUpdateWindow *self)
{
  GtkWidget *page = page_new ();
  GtkWidget *header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
  GtkWidget *titles = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *icon = icon_new ("shopno-update-symbolic", 44);
  GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);
  GtkWidget *frame;
  GtkWidget *hint;
  GtkWidget *footer = footer_new (&hint);
  GtkWidget *skip = gtk_button_new_with_mnemonic (_("_Skip"));
  GtkWidget *upgrade = gtk_button_new_with_mnemonic (_("_Upgrade"));

  add_class (header, "shopno-header");
  add_class (icon, "shopno-accent");
  self->updates_title = label_new (NULL, "shopno-heading", 0);
  self->updates_subtitle = label_new (NULL, "shopno-dim", 0);
  gtk_box_pack_start (GTK_BOX (titles), GTK_WIDGET (self->updates_title), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (titles), GTK_WIDGET (self->updates_subtitle), FALSE, FALSE, 0);
  gtk_widget_set_valign (titles, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (header), icon, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (header), titles, TRUE, TRUE, 0);

  self->updates_note = label_new (NULL, "shopno-notice", 0);
  gtk_widget_set_no_show_all (GTK_WIDGET (self->updates_note), TRUE);

  self->updates_list = list_new (&frame);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled), frame);
  gtk_widget_set_vexpand (scrolled, TRUE);
  add_class (scrolled, "shopno-content");

  self->updates_hint = GTK_LABEL (hint);
  g_signal_connect_swapped (skip, "clicked", G_CALLBACK (gtk_widget_destroy), self);
  g_signal_connect_swapped (upgrade, "clicked", G_CALLBACK (start_install), self);
  gtk_box_pack_end (GTK_BOX (footer), upgrade, FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (footer), skip, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (page), header, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (page), GTK_WIDGET (self->updates_note), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (page), scrolled, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (page), footer, FALSE, FALSE, 0);

  return page;
}



static GtkWidget *
step_new (ShopnoUpdateWindow *self,
          Source              source,
          const gchar        *title)
{
  GtkWidget *step = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gchar number[8];

  g_snprintf (number, sizeof (number), "%d", source + 1);
  self->step_number[source] = label_new (number, "shopno-step-number", 0.5);
  gtk_label_set_line_wrap (self->step_number[source], FALSE);

  gtk_box_pack_start (GTK_BOX (step), GTK_WIDGET (self->step_number[source]), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (step), GTK_WIDGET (label_new (title, "shopno-step-title", 0)), FALSE, FALSE, 0);
  add_class (step, "shopno-step");
  self->step[source] = step;

  return step;
}



/* The terminal's size request is what makes the window grow when Details
 * opens, and shrink back when it closes */
static void
on_terminal_char_size_changed (VteTerminal       *terminal,
                               guint              char_width,
                               guint              char_height,
                               GtkScrolledWindow *scrolled)
{
  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (terminal));
  GtkBorder padding;

  gtk_style_context_get_padding (context, gtk_style_context_get_state (context), &padding);
  gtk_widget_set_size_request (GTK_WIDGET (terminal),
                               TERMINAL_COLUMNS * char_width + padding.left + padding.right, -1);
  gtk_scrolled_window_set_min_content_height (scrolled,
                                              TERMINAL_ROWS * char_height + padding.top + padding.bottom);
}



static GtkWidget *
build_installing_page (ShopnoUpdateWindow *self)
{
  GtkWidget *page = page_new ();
  GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 16);
  GtkWidget *titles = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *line = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *progress = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *status_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *input_icon = icon_new ("shopno-question-symbolic", 22);
  GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);

  add_class (body, "shopno-body");

  gtk_box_pack_start (GTK_BOX (titles), GTK_WIDGET (label_new (_("Installing updates"), "shopno-heading", 0)), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (titles), GTK_WIDGET (label_new (_("Please keep this window open until it finishes."), "shopno-dim", 0)), FALSE, FALSE, 0);

  /* 1 System packages ── 2 Flatpak apps */
  self->steps = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  add_class (line, "shopno-step-line");
  gtk_widget_set_valign (line, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (self->steps), step_new (self, SOURCE_APT, _("System packages")), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->steps), line, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (self->steps), step_new (self, SOURCE_FLATPAK, _("Flatpak apps")), FALSE, FALSE, 0);
  gtk_widget_show_all (self->steps);
  gtk_widget_set_no_show_all (self->steps, TRUE);

  self->install_status = label_new (NULL, "shopno-strong", 0);
  gtk_label_set_line_wrap (self->install_status, FALSE);
  gtk_label_set_ellipsize (self->install_status, PANGO_ELLIPSIZE_END);
  self->install_percent = label_new (NULL, "shopno-dim", 1);
  gtk_box_pack_start (GTK_BOX (status_row), GTK_WIDGET (self->install_status), TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (status_row), GTK_WIDGET (self->install_percent), FALSE, FALSE, 0);
  self->install_bar = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
  gtk_box_pack_start (GTK_BOX (progress), status_row, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (progress), GTK_WIDGET (self->install_bar), FALSE, FALSE, 0);

  /* Shown when a package asks a question */
  self->input_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  add_class (self->input_bar, "shopno-question");
  add_class (input_icon, "shopno-accent");
  self->input_label = label_new (NULL, NULL, 0);
  gtk_box_pack_start (GTK_BOX (self->input_bar), input_icon, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (self->input_bar), GTK_WIDGET (self->input_label), TRUE, TRUE, 0);
  gtk_widget_show_all (self->input_bar);
  gtk_widget_set_no_show_all (self->input_bar, TRUE);
  gtk_widget_hide (self->input_bar);

  self->terminal = VTE_TERMINAL (vte_terminal_new ());
  vte_terminal_set_size (self->terminal, TERMINAL_COLUMNS, TERMINAL_ROWS);
  vte_terminal_set_scrollback_lines (self->terminal, 10000);
  vte_terminal_set_mouse_autohide (self->terminal, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (self->terminal));
  gtk_widget_set_vexpand (scrolled, TRUE);
  add_class (scrolled, "shopno-terminal");
  g_signal_connect (self->terminal, "char-size-changed", G_CALLBACK (on_terminal_char_size_changed), scrolled);

  self->details = GTK_EXPANDER (gtk_expander_new_with_mnemonic (_("_Details")));
  gtk_container_add (GTK_CONTAINER (self->details), scrolled);

  self->apt_upgrade = shopno_apt_upgrade_new (self->terminal);

  gtk_box_pack_start (GTK_BOX (body), titles, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), self->steps, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), progress, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), self->input_bar, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (self->details), TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (page), body, TRUE, TRUE, 0);

  return page;
}



static void
on_result_primary (ShopnoUpdateWindow *self);



static GtkWidget *
build_result_page (ShopnoUpdateWindow *self)
{
  GtkWidget *page = page_new ();
  GtkWidget *body = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *footer = footer_new (NULL);
  GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);
  GtkWidget *log_view = gtk_text_view_new ();

  add_class (body, "shopno-body");
  gtk_widget_set_valign (body, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand (body, TRUE);

  self->result_icon = GTK_IMAGE (icon_new ("shopno-success-symbolic", BIG_ICON_SIZE));
  self->result_title = label_new (NULL, "shopno-title", 0.5);
  self->result_message = label_new (NULL, "shopno-dim", 0.5);
  gtk_label_set_justify (self->result_message, GTK_JUSTIFY_CENTER);
  self->result_notice = label_new (NULL, "shopno-notice", 0);
  gtk_widget_set_no_show_all (GTK_WIDGET (self->result_notice), TRUE);

  self->result_log = gtk_text_view_get_buffer (GTK_TEXT_VIEW (log_view));
  gtk_text_view_set_editable (GTK_TEXT_VIEW (log_view), FALSE);
  gtk_text_view_set_monospace (GTK_TEXT_VIEW (log_view), TRUE);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (log_view), GTK_WRAP_WORD_CHAR);
  gtk_container_add (GTK_CONTAINER (scrolled), log_view);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled), GTK_SHADOW_IN);
  gtk_widget_set_size_request (scrolled, -1, 180);
  self->result_details = GTK_EXPANDER (gtk_expander_new_with_mnemonic (_("_Details")));
  gtk_container_add (GTK_CONTAINER (self->result_details), scrolled);
  gtk_widget_show_all (GTK_WIDGET (self->result_details));
  gtk_widget_set_no_show_all (GTK_WIDGET (self->result_details), TRUE);

  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (self->result_icon), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (self->result_title), FALSE, FALSE, 4);
  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (self->result_message), FALSE, FALSE, 0);
  self->result_sources = sources_new (self->result_rows);
  gtk_box_pack_start (GTK_BOX (body), self->result_sources, FALSE, FALSE, 8);
  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (self->result_notice), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (body), GTK_WIDGET (self->result_details), FALSE, FALSE, 0);

  self->result_primary = GTK_BUTTON (gtk_button_new_with_mnemonic (NULL));
  self->result_secondary = gtk_button_new_with_mnemonic (_("_Close"));
  gtk_widget_show (self->result_secondary);
  gtk_widget_set_no_show_all (self->result_secondary, TRUE);
  g_signal_connect_swapped (self->result_primary, "clicked", G_CALLBACK (on_result_primary), self);
  g_signal_connect_swapped (self->result_secondary, "clicked", G_CALLBACK (gtk_widget_destroy), self);
  gtk_box_pack_end (GTK_BOX (footer), GTK_WIDGET (self->result_primary), FALSE, FALSE, 0);
  gtk_box_pack_end (GTK_BOX (footer), self->result_secondary, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (page), body, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (page), footer, FALSE, FALSE, 0);

  return page;
}



/*
 * Result page
 */

typedef enum
{
  LOOK_SUCCESS,
  LOOK_WARNING,
  LOOK_ERROR,
  LOOK_OFFLINE,
} Look;

static void
show_result (ShopnoUpdateWindow *self,
             Look                look,
             const gchar        *title,
             const gchar        *message,
             gboolean            show_sources,
             const gchar        *notice,
             const gchar        *log,
             const gchar        *secondary,
             const gchar        *primary,
             Action              primary_action)
{
  static const gchar *const icons[] = {
    [LOOK_SUCCESS] = "shopno-success-symbolic",
    [LOOK_WARNING] = "shopno-warning-symbolic",
    [LOOK_ERROR] = "shopno-error-symbolic",
    [LOOK_OFFLINE] = "shopno-offline-symbolic",
  };
  static const gchar *const classes[] = {
    [LOOK_SUCCESS] = "shopno-success",
    [LOOK_WARNING] = "shopno-warning",
    [LOOK_ERROR] = "shopno-error",
    [LOOK_OFFLINE] = "shopno-error",
  };
  gtk_image_set_from_icon_name (self->result_icon, icons[look], GTK_ICON_SIZE_DIALOG);
  gtk_image_set_pixel_size (self->result_icon, BIG_ICON_SIZE);
  set_state_class (GTK_WIDGET (self->result_icon), classes[look]);

  gtk_label_set_text (self->result_title, title);
  gtk_label_set_text (self->result_message, message);
  gtk_widget_set_visible (GTK_WIDGET (self->result_message), message != NULL);

  gtk_widget_set_visible (self->result_sources, show_sources);

  gtk_label_set_text (self->result_notice, notice);
  gtk_widget_set_visible (GTK_WIDGET (self->result_notice), notice != NULL);

  gtk_text_buffer_set_text (self->result_log, log != NULL ? log : "", -1);
  gtk_expander_set_expanded (self->result_details, FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->result_details), log != NULL && *log != '\0');

  if (secondary != NULL)
    gtk_button_set_label (GTK_BUTTON (self->result_secondary), secondary);
  gtk_widget_set_visible (self->result_secondary, secondary != NULL);

  gtk_button_set_label (self->result_primary, primary);
  self->primary_action = primary_action;

  gtk_stack_set_visible_child_name (self->stack, "result");
  gtk_widget_grab_focus (GTK_WIDGET (self->result_primary));
}



static void
on_reboot_done (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(ShopnoUpdateWindow) self = user_data;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  GtkWidget *dialog;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  if (reply != NULL)
    return;

  if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED))
    return;

  g_dbus_error_strip_remote_error (error);
  dialog = gtk_message_dialog_new (GTK_WINDOW (self), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE, _("Could not restart the computer"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", error->message);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}



static void
restart (ShopnoUpdateWindow *self)
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GError) error = NULL;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (bus == NULL)
    {
      g_warning ("Could not connect to the system bus: %s", error->message);
      return;
    }

  /* logind asks for permission itself, through polkit */
  g_dbus_connection_call (bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager", "Reboot",
                          g_variant_new ("(b)", TRUE), NULL,
                          G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, G_MAXINT,
                          NULL, on_reboot_done, g_object_ref (self));
}



static void
on_result_primary (ShopnoUpdateWindow *self)
{
  switch (self->primary_action)
    {
    case ACTION_CLOSE:
      gtk_widget_destroy (GTK_WIDGET (self));
      break;

    case ACTION_CHECK:
      start_check (self);
      break;

    case ACTION_INSTALL_FLATPAK:
      self->installing = TRUE;
      gtk_stack_set_visible_child_name (self->stack, "installing");
      install_flatpak (self);
      break;

    case ACTION_RESTART:
      restart (self);
      break;
    }
}



/*
 * Checking
 */

static guint
count_flatpak_apps (GPtrArray *updates,
                    gboolean   apps)
{
  guint n = 0;

  for (guint i = 0; updates != NULL && i < updates->len; i++)
    {
      ShopnoFlatpakUpdate *update = g_ptr_array_index (updates, i);

      if (update->is_app == apps)
        n++;
    }

  return n;
}



static guint
n_apt_changes (ShopnoUpdateWindow *self)
{
  return self->apt != NULL ? self->apt->changes->len : 0;
}



static guint
n_flatpak_updates (ShopnoUpdateWindow *self)
{
  return self->flatpak_updates != NULL ? self->flatpak_updates->len : 0;
}



static GtkWidget *
section_new (const gchar *title,
             const gchar *info)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  g_autofree gchar *upper = g_utf8_strup (title, -1);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  add_class (row, "shopno-section");
  gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (label_new (upper, NULL, 0)), TRUE, TRUE, 0);
  if (info != NULL)
    gtk_box_pack_end (GTK_BOX (box), GTK_WIDGET (label_new (info, "shopno-dim", 1)), FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (row), box);

  return row;
}



static GtkWidget *
item_new (const gchar *icon_name,
          const gchar *name,
          const gchar *info,
          const gchar *info_class,
          const gchar *tooltip)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkLabel *name_label = label_new (name, "shopno-strong", 0);
  GtkLabel *info_label;

  add_class (box, "shopno-item");

  if (icon_name != NULL)
    gtk_box_pack_start (GTK_BOX (box), icon_new (icon_name, ROW_ICON_SIZE), FALSE, FALSE, 0);

  gtk_label_set_line_wrap (name_label, FALSE);
  gtk_label_set_ellipsize (name_label, PANGO_ELLIPSIZE_END);
  gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (name_label), TRUE, TRUE, 0);

  if (info != NULL)
    {
      info_label = label_new (info, info_class, 1);
      gtk_label_set_line_wrap (info_label, FALSE);
      gtk_label_set_ellipsize (info_label, PANGO_ELLIPSIZE_START);
      add_class (GTK_WIDGET (info_label), "shopno-small");
      gtk_box_pack_end (GTK_BOX (box), GTK_WIDGET (info_label), FALSE, FALSE, 0);
    }

  if (tooltip != NULL)
    gtk_widget_set_tooltip_text (box, tooltip);

  return box;
}



static void
add_apt_rows (ShopnoUpdateWindow *self)
{
  GtkListBox *list = self->updates_list;
  g_autofree gchar *info = NULL;
  guint n = n_apt_changes (self);

  if (n == 0)
    {
      list_append (list, section_new (_("System packages"), NULL));
      list_append (list, item_new (NULL, _("Up to date"), NULL, NULL, NULL));
      return;
    }

  if (self->apt->download_size > 0)
    {
      g_autofree gchar *size = g_format_size (self->apt->download_size);
      info = g_strdup_printf ("%u · %s", n, size);
    }
  else
    {
      info = g_strdup_printf ("%u", n);
    }

  list_append (list, section_new (_("System packages"), info));

  for (guint i = 0; i < n; i++)
    {
      ShopnoAptChange *change = g_ptr_array_index (self->apt->changes, i);
      g_autofree gchar *versions = NULL;
      g_autofree gchar *tooltip = NULL;
      const gchar *info_class = "shopno-mono";

      switch (change->kind)
        {
        case SHOPNO_APT_CHANGE_UPGRADE:
          versions = shopno_apt_format_versions (change->old_version, change->new_version);
          tooltip = g_strdup_printf ("%s → %s", change->old_version, change->new_version);
          break;

        case SHOPNO_APT_CHANGE_INSTALL:
          /* TRANSLATORS: a package that is installed for the first time */
          versions = g_strdup_printf (_("new · %s"), change->new_version);
          break;

        case SHOPNO_APT_CHANGE_REMOVE:
          versions = g_strdup (_("will be removed"));
          info_class = "shopno-error";
          break;

        case SHOPNO_APT_CHANGE_REPAIR:
          versions = g_strdup (_("repair"));
          info_class = "shopno-warning";
          break;
        }

      list_append (list, item_new (NULL, change->name, versions, info_class, tooltip));
    }
}



static const gchar *
flatpak_icon_name (const gchar *app_id)
{
  if (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), app_id))
    return app_id;

  return "application-x-executable";
}



static void
add_flatpak_rows (ShopnoUpdateWindow *self)
{
  GtkListBox *list = self->updates_list;
  guint n_apps = count_flatpak_apps (self->flatpak_updates, TRUE);
  guint n_runtimes = count_flatpak_apps (self->flatpak_updates, FALSE);
  g_autofree gchar *info = NULL;

  if (!self->flatpak_available)
    {
      list_append (list, section_new (_("Flatpak apps"), NULL));
      list_append (list, item_new (NULL, _("Flatpak is not installed, skipped"), NULL, NULL, NULL));
      return;
    }

  if (self->flatpak_error != NULL)
    {
      list_append (list, section_new (_("Flatpak apps"), NULL));
      list_append (list, item_new (NULL, _("Could not check for updates"),
                                   self->flatpak_error->message, "shopno-error",
                                   self->flatpak_error->message));
      return;
    }

  if (n_apps + n_runtimes == 0)
    {
      list_append (list, section_new (_("Flatpak apps"), NULL));
      list_append (list, item_new (NULL, _("Up to date"), NULL, NULL, NULL));
      return;
    }

  info = g_strdup_printf ("%u", n_apps + n_runtimes);
  list_append (list, section_new (_("Flatpak apps"), info));

  for (guint i = 0; i < self->flatpak_updates->len; i++)
    {
      ShopnoFlatpakUpdate *update = g_ptr_array_index (self->flatpak_updates, i);

      if (update->is_app)
        list_append (list, item_new (flatpak_icon_name (update->id), update->name, NULL, NULL, update->id));
    }

  /* Runtimes mean little to most people, so fold them into one row */
  if (n_runtimes > 0)
    {
      GtkWidget *expander;
      GtkWidget *names = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
      g_autofree gchar *title = g_strdup_printf (ngettext ("Runtimes and extensions (%u)",
                                                           "Runtimes and extensions (%u)",
                                                           n_runtimes),
                                                 n_runtimes);

      expander = gtk_expander_new (title);
      add_class (expander, "shopno-item");
      add_class (names, "shopno-runtimes");

      for (guint i = 0; i < self->flatpak_updates->len; i++)
        {
          ShopnoFlatpakUpdate *update = g_ptr_array_index (self->flatpak_updates, i);

          if (!update->is_app)
            gtk_box_pack_start (GTK_BOX (names), GTK_WIDGET (label_new (update->name, "shopno-dim", 0)), FALSE, FALSE, 0);
        }

      gtk_container_add (GTK_CONTAINER (expander), names);
      list_append (list, expander);
    }
}



static void
show_updates (ShopnoUpdateWindow *self)
{
  guint n_apt = n_apt_changes (self);
  guint n_flatpak = n_flatpak_updates (self);
  guint total = n_apt + n_flatpak;
  g_autofree gchar *title = NULL;
  g_autoptr(GString) note = g_string_new (NULL);
  const gchar *hint;

  title = g_strdup_printf (ngettext ("%u update available", "%u updates available", total), total);
  gtk_label_set_text (self->updates_title, title);

  if (n_apt > 0 && self->apt->download_size > 0)
    {
      g_autofree gchar *size = g_format_size (self->apt->download_size);
      g_autofree gchar *subtitle = g_strdup_printf (_("System packages: %s to download"), size);
      gtk_label_set_text (self->updates_subtitle, subtitle);
    }
  else
    {
      gtk_label_set_text (self->updates_subtitle, _("Review the changes below."));
    }

  if (self->apt != NULL && self->apt->sources_failed)
    g_string_append (note, _("Some package sources could not be reached. Updates from them may be missing."));

  if (self->apt != NULL && self->apt->kept_back > 0)
    {
      if (note->len > 0)
        g_string_append_c (note, '\n');
      g_string_append_printf (note,
                              ngettext ("%u package is held back and will not be updated.",
                                        "%u packages are held back and will not be updated.",
                                        self->apt->kept_back),
                              self->apt->kept_back);
    }

  gtk_label_set_text (self->updates_note, note->str);
  gtk_widget_set_visible (GTK_WIDGET (self->updates_note), note->len > 0);

  list_clear (self->updates_list);
  add_apt_rows (self);
  add_flatpak_rows (self);
  gtk_widget_show_all (GTK_WIDGET (self->updates_list));

  if (n_apt > 0 && n_flatpak > 0)
    hint = _("System packages install first, then Flatpak apps.");
  else if (n_apt > 0)
    hint = _("You will be asked for your password.");
  else
    hint = NULL;
  gtk_label_set_text (self->updates_hint, hint);

  gtk_stack_set_visible_child_name (self->stack, "updates");
}



static void
checks_done (ShopnoUpdateWindow *self)
{
  if (self->apt_error != NULL)
    {
      Look look = LOOK_ERROR;
      const gchar *title = _("Could not check for updates");

      if (g_error_matches (self->apt_error, SHOPNO_APT_ERROR, SHOPNO_APT_ERROR_NETWORK))
        {
          look = LOOK_OFFLINE;
          title = _("Could not reach the update servers");
        }

      show_result (self, look, title, self->apt_error->message, FALSE, NULL, self->apt_log,
                   _("_Close"), _("_Try Again"), ACTION_CHECK);
      return;
    }

  if (n_apt_changes (self) + n_flatpak_updates (self) > 0)
    {
      show_updates (self);
      return;
    }

  /* Nothing to do */
  source_row_set (&self->result_rows[SOURCE_APT], STATE_OK, _("Up to date"), NULL);

  if (!self->flatpak_available)
    source_row_set (&self->result_rows[SOURCE_FLATPAK], STATE_SKIPPED, _("Not installed, skipped"), NULL);
  else if (self->flatpak_error != NULL)
    source_row_set (&self->result_rows[SOURCE_FLATPAK], STATE_FAILED, _("Could not check"),
                    self->flatpak_error->message);
  else
    source_row_set (&self->result_rows[SOURCE_FLATPAK], STATE_OK, _("Up to date"), NULL);

  show_result (self, LOOK_SUCCESS,
               self->flatpak_error != NULL ? _("System packages are up to date")
                                           : _("Your system is up to date"),
               _("Everything is already on the latest version."),
               TRUE, NULL, NULL, NULL, _("_Close"), ACTION_CLOSE);
}



static void
on_apt_checked (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  ShopnoUpdateWindow *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *log = NULL;
  ShopnoAptCheck *check;
  g_autofree gchar *status = NULL;

  check = shopno_apt_check_finish (result, &log, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_object_unref (self);
      return;
    }

  self->apt = check;
  self->apt_log = g_steal_pointer (&log);

  if (check == NULL)
    {
      self->apt_error = g_steal_pointer (&error);
      source_row_set (&self->check_rows[SOURCE_APT], STATE_FAILED, _("Failed"), NULL);
    }
  else if (check->changes->len == 0)
    {
      source_row_set (&self->check_rows[SOURCE_APT], STATE_OK, _("Up to date"), NULL);
    }
  else
    {
      status = g_strdup_printf (ngettext ("%u update", "%u updates", check->changes->len),
                                check->changes->len);
      source_row_set (&self->check_rows[SOURCE_APT], STATE_OK, status, NULL);
    }

  if (--self->checks_pending == 0)
    checks_done (self);

  g_object_unref (self);
}



static void
on_flatpak_checked (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  ShopnoUpdateWindow *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *status = NULL;
  GPtrArray *updates;

  updates = shopno_flatpak_check_finish (result, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_object_unref (self);
      return;
    }

  self->flatpak_updates = updates;

  if (updates == NULL)
    {
      self->flatpak_error = g_steal_pointer (&error);
      source_row_set (&self->check_rows[SOURCE_FLATPAK], STATE_FAILED, _("Failed"),
                      self->flatpak_error->message);
    }
  else if (updates->len == 0)
    {
      source_row_set (&self->check_rows[SOURCE_FLATPAK], STATE_OK, _("Up to date"), NULL);
    }
  else
    {
      status = g_strdup_printf (ngettext ("%u update", "%u updates", updates->len), updates->len);
      source_row_set (&self->check_rows[SOURCE_FLATPAK], STATE_OK, status, NULL);
    }

  if (--self->checks_pending == 0)
    checks_done (self);

  g_object_unref (self);
}



static void
reset_results (ShopnoUpdateWindow *self)
{
  g_clear_pointer (&self->apt, shopno_apt_check_free);
  g_clear_pointer (&self->apt_log, g_free);
  g_clear_error (&self->apt_error);
  g_clear_pointer (&self->flatpak_updates, g_ptr_array_unref);
  g_clear_error (&self->flatpak_error);

  self->apt_result = RESULT_NONE;
  self->flatpak_result = RESULT_NONE;
  self->flatpak_updated = 0;
  g_clear_pointer (&self->apt_message, g_free);
  g_clear_pointer (&self->flatpak_message, g_free);
  g_clear_pointer (&self->running_apps, g_strfreev);
  g_string_truncate (self->flatpak_log, 0);
}



static void
start_check (ShopnoUpdateWindow *self)
{
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  reset_results (self);
  gtk_stack_set_visible_child_name (self->stack, "checking");

  self->checks_pending = 1;
  source_row_set (&self->check_rows[SOURCE_APT], STATE_BUSY, _("Checking…"), NULL);
  shopno_apt_check_async (self->cancellable, on_apt_checked, g_object_ref (self));

  self->flatpak_available = shopno_flatpak_is_available ();
  if (self->flatpak_available)
    {
      self->checks_pending++;
      source_row_set (&self->check_rows[SOURCE_FLATPAK], STATE_BUSY, _("Checking…"), NULL);
      shopno_flatpak_check_async (self->cancellable, on_flatpak_checked, g_object_ref (self));
    }
  else
    {
      source_row_set (&self->check_rows[SOURCE_FLATPAK], STATE_SKIPPED, _("Not installed, skipped"), NULL);
    }
}



/*
 * Installing
 */

static void
set_step (ShopnoUpdateWindow *self,
          Source              source,
          const gchar        *state_class,
          const gchar        *mark)
{
  GtkStyleContext *context = gtk_widget_get_style_context (self->step[source]);
  gchar number[8];

  gtk_style_context_remove_class (context, "shopno-step-active");
  gtk_style_context_remove_class (context, "shopno-step-done");
  gtk_style_context_remove_class (context, "shopno-step-failed");
  if (state_class != NULL)
    gtk_style_context_add_class (context, state_class);

  if (mark == NULL)
    {
      g_snprintf (number, sizeof (number), "%d", source + 1);
      mark = number;
    }
  gtk_label_set_text (self->step_number[source], mark);
}



static void
set_progress (ShopnoUpdateWindow *self,
              gdouble             fraction,
              const gchar        *text)
{
  g_autofree gchar *percent = g_strdup_printf ("%d%%", (gint) (fraction * 100));

  gtk_progress_bar_set_fraction (self->install_bar, fraction);
  gtk_label_set_text (self->install_percent, percent);
  if (text != NULL && *text != '\0')
    gtk_label_set_text (self->install_status, text);
}



static void
on_apt_progress (ShopnoAptUpgrade   *upgrade,
                 gdouble             fraction,
                 const gchar        *text,
                 ShopnoUpdateWindow *self)
{
  set_progress (self, fraction, text);
}



static void
on_apt_input_needed (ShopnoAptUpgrade   *upgrade,
                     gboolean            needed,
                     const gchar        *package,
                     ShopnoUpdateWindow *self)
{
  if (!needed)
    {
      gtk_widget_hide (self->input_bar);
      return;
    }

  if (package != NULL)
    {
      g_autofree gchar *markup = g_markup_printf_escaped (_("<b>%s</b> needs your input. Answer the question below to continue."),
                                                          package);
      gtk_label_set_markup (self->input_label, markup);
    }
  else
    {
      gtk_label_set_text (self->input_label, _("A package needs your input. Answer the question below to continue."));
    }

  gtk_label_set_text (self->install_status, _("Waiting for your answer"));
  gtk_widget_show (self->input_bar);
  gtk_expander_set_expanded (self->details, TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self->terminal));
}



static void
on_apt_finished (ShopnoAptUpgrade   *upgrade,
                 gint                exit_status,
                 const gchar        *errors,
                 ShopnoUpdateWindow *self)
{
  gtk_widget_hide (self->input_bar);

  if (exit_status == SHOPNO_APT_UPGRADE_DISMISSED
      || exit_status == SHOPNO_APT_UPGRADE_NOT_AUTHORIZED)
    {
      /* Nothing was changed; go back so the user can try again */
      self->installing = FALSE;
      gtk_label_set_text (self->updates_note, _("Authentication was cancelled. Nothing was changed."));
      gtk_widget_show (GTK_WIDGET (self->updates_note));
      gtk_stack_set_visible_child_name (self->stack, "updates");
      return;
    }

  if (exit_status == 0)
    {
      self->apt_result = RESULT_OK;
      set_step (self, SOURCE_APT, "shopno-step-done", "✓");
    }
  else
    {
      self->apt_result = RESULT_FAILED;
      if (errors != NULL)
        self->apt_message = g_strdup (errors);
      else if (exit_status > 0)
        self->apt_message = g_strdup_printf (_("The package manager stopped with error %d."), exit_status);
      else
        self->apt_message = g_strdup (_("The package manager could not be started."));
      set_step (self, SOURCE_APT, "shopno-step-failed", "✕");
    }

  if (n_flatpak_updates (self) > 0)
    install_flatpak (self);
  else
    finish_install (self);
}



static void
install_apt (ShopnoUpdateWindow *self)
{
  set_step (self, SOURCE_APT, "shopno-step-active", NULL);
  set_progress (self, 0, _("Preparing…"));
  shopno_apt_upgrade_start (self->apt_upgrade);
}



static void
on_flatpak_progress (gdouble      fraction,
                     const gchar *text,
                     const gchar *log_line,
                     gpointer     user_data)
{
  ShopnoUpdateWindow *self = user_data;

  set_progress (self, fraction, text);

  if (log_line != NULL)
    {
      g_autofree gchar *line = g_strdup_printf ("%s\r\n", log_line);

      g_string_append_printf (self->flatpak_log, "%s\n", log_line);
      vte_terminal_feed (self->terminal, line, -1);
    }
}



static void
on_flatpak_installed (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(ShopnoUpdateWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  g_clear_pointer (&self->running_apps, g_strfreev);

  if (shopno_flatpak_update_finish (result, &self->flatpak_updated, &self->running_apps, &error))
    {
      self->flatpak_result = RESULT_OK;
      set_step (self, SOURCE_FLATPAK, "shopno-step-done", "✓");
    }
  else
    {
      self->flatpak_result = RESULT_FAILED;
      g_free (self->flatpak_message);
      self->flatpak_message = g_strdup (error->message);
      set_step (self, SOURCE_FLATPAK, "shopno-step-failed", "✕");
    }

  finish_install (self);
}



static void
install_flatpak (ShopnoUpdateWindow *self)
{
  const gchar *heading = _("Updating Flatpak apps");
  g_autofree gchar *line = g_strdup_printf ("\r\n\033[1m%s\033[0m\r\n", heading);

  set_step (self, SOURCE_FLATPAK, "shopno-step-active", NULL);
  set_progress (self, 0, _("Preparing…"));
  vte_terminal_feed (self->terminal, line, -1);

  shopno_flatpak_update_async (self->flatpak_updates, on_flatpak_progress, self,
                               NULL, on_flatpak_installed, g_object_ref (self));
}



static void
prepare_install (ShopnoUpdateWindow *self)
{
  gboolean both = n_apt_changes (self) > 0 && n_flatpak_updates (self) > 0;

  self->installing = TRUE;
  self->apt_result = RESULT_NONE;
  self->flatpak_result = RESULT_NONE;
  g_clear_pointer (&self->apt_message, g_free);
  g_clear_pointer (&self->flatpak_message, g_free);
  g_string_truncate (self->flatpak_log, 0);

  set_step (self, SOURCE_APT, NULL, NULL);
  set_step (self, SOURCE_FLATPAK, NULL, NULL);
  gtk_widget_set_visible (self->steps, both);
  gtk_widget_hide (self->input_bar);
  gtk_widget_hide (GTK_WIDGET (self->updates_note));
  gtk_stack_set_visible_child_name (self->stack, "installing");
}



static void
start_install (ShopnoUpdateWindow *self)
{
  prepare_install (self);

  if (n_apt_changes (self) > 0)
    {
      install_apt (self);
    }
  else
    {
      vte_terminal_reset (self->terminal, TRUE, TRUE);
      install_flatpak (self);
    }
}



static void
set_result_row (SourceRow     *row,
                gboolean       available,
                InstallResult  result,
                guint          n_updated,
                const gchar   *message)
{
  g_autofree gchar *status = NULL;

  if (!available)
    {
      source_row_set (row, STATE_SKIPPED, _("Not installed, skipped"), NULL);
      return;
    }

  switch (result)
    {
    case RESULT_NONE:
      source_row_set (row, STATE_OK, _("Up to date"), NULL);
      break;

    case RESULT_OK:
      status = g_strdup_printf (ngettext ("%u updated", "%u updated", n_updated), n_updated);
      source_row_set (row, STATE_OK, status, NULL);
      break;

    case RESULT_FAILED:
      source_row_set (row, STATE_FAILED, _("Failed"), message);
      break;
    }
}



static gchar *
build_log (ShopnoUpdateWindow *self)
{
  g_autofree gchar *terminal = shopno_apt_upgrade_get_log (self->apt_upgrade);
  GString *log = g_string_new (NULL);

  if (self->apt_result != RESULT_NONE && terminal != NULL)
    g_string_append (log, g_strstrip (terminal));

  if (self->flatpak_log->len > 0)
    {
      if (log->len > 0)
        g_string_append (log, "\n\n");
      g_string_append (log, self->flatpak_log->str);
    }

  return g_string_free (log, FALSE);
}



static void
finish_install (ShopnoUpdateWindow *self)
{
  gboolean apt_failed = self->apt_result == RESULT_FAILED;
  gboolean flatpak_failed = self->flatpak_result == RESULT_FAILED;
  gboolean apt_ok = self->apt_result == RESULT_OK;
  gboolean flatpak_ok = self->flatpak_result == RESULT_OK;
  g_autoptr(GString) notice = g_string_new (NULL);
  g_autofree gchar *log = NULL;
  gboolean restart_needed;

  self->installing = FALSE;
  set_progress (self, 1.0, NULL);

  set_result_row (&self->result_rows[SOURCE_APT], TRUE, self->apt_result,
                  n_apt_changes (self), self->apt_message);
  set_result_row (&self->result_rows[SOURCE_FLATPAK], self->flatpak_available, self->flatpak_result,
                  self->flatpak_updated, self->flatpak_message);

  if (!apt_failed && !flatpak_failed)
    {
      restart_needed = apt_ok
                       && (g_file_test (REBOOT_REQUIRED_FILE, G_FILE_TEST_EXISTS)
                           || shopno_apt_changes_need_restart (self->apt->changes));

      if (restart_needed)
        g_string_append (notice, _("Restart your computer to finish installing the updates."));

      if (self->running_apps != NULL && self->running_apps[0] != NULL)
        {
          g_autofree gchar *names = g_strjoinv (", ", self->running_apps);

          if (notice->len > 0)
            g_string_append_c (notice, '\n');
          g_string_append_printf (notice,
                                  ngettext ("%s is still running the old version until you reopen it.",
                                            "%s are still running the old version until you reopen them.",
                                            g_strv_length (self->running_apps)),
                                  names);
        }

      if (restart_needed)
        show_result (self, LOOK_SUCCESS, _("Updates installed"), _("Your system is up to date."),
                     TRUE, notice->str, NULL, _("Restart _Later"), _("_Restart Now"), ACTION_RESTART);
      else
        show_result (self, LOOK_SUCCESS, _("Updates installed"), _("Your system is up to date."),
                     TRUE, notice->len > 0 ? notice->str : NULL, NULL, NULL, _("_Close"), ACTION_CLOSE);
      return;
    }

  log = build_log (self);

  if ((apt_ok && flatpak_failed) || (flatpak_ok && apt_failed))
    {
      /* Retrying only Flatpak avoids asking for the password again */
      if (apt_ok)
        show_result (self, LOOK_WARNING, _("Some updates were not installed"), NULL,
                     TRUE, NULL, log, _("_Close"), _("Retry _Flatpak Apps"), ACTION_INSTALL_FLATPAK);
      else
        show_result (self, LOOK_WARNING, _("Some updates were not installed"), NULL,
                     TRUE, NULL, log, _("_Close"), _("_Try Again"), ACTION_CHECK);
      return;
    }

  show_result (self, LOOK_ERROR, _("Updates could not be installed"), NULL,
               TRUE, NULL, log, _("_Close"), _("_Try Again"), ACTION_CHECK);
}



/*
 * Window
 */

static gboolean
shopno_update_window_delete_event (GtkWidget   *widget,
                                   GdkEventAny *event)
{
  ShopnoUpdateWindow *self = SHOPNO_UPDATE_WINDOW (widget);
  GtkWidget *dialog;

  if (!self->installing)
    return FALSE;

  dialog = gtk_message_dialog_new (GTK_WINDOW (self), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                   GTK_BUTTONS_OK, _("Updates are being installed"));
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Closing now could leave your system in a broken state. "
                                              "The window can be closed when installing finishes."));
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  return TRUE;
}



static void
shopno_update_window_dispose (GObject *object)
{
  ShopnoUpdateWindow *self = SHOPNO_UPDATE_WINDOW (object);

  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  if (self->apt_upgrade != NULL)
    g_signal_handlers_disconnect_by_data (self->apt_upgrade, self);
  g_clear_object (&self->apt_upgrade);

  G_OBJECT_CLASS (shopno_update_window_parent_class)->dispose (object);
}



static void
shopno_update_window_finalize (GObject *object)
{
  ShopnoUpdateWindow *self = SHOPNO_UPDATE_WINDOW (object);

  reset_results (self);
  g_string_free (self->flatpak_log, TRUE);

  G_OBJECT_CLASS (shopno_update_window_parent_class)->finalize (object);
}



static void
shopno_update_window_class_init (ShopnoUpdateWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = shopno_update_window_dispose;
  object_class->finalize = shopno_update_window_finalize;
  widget_class->delete_event = shopno_update_window_delete_event;
}



static void
shopno_update_window_init (ShopnoUpdateWindow *self)
{
  self->flatpak_log = g_string_new (NULL);

  gtk_window_set_title (GTK_WINDOW (self), _("Update Helper"));
  gtk_window_set_icon_name (GTK_WINDOW (self), "system-software-update");
  gtk_window_set_resizable (GTK_WINDOW (self), FALSE);
  gtk_window_set_position (GTK_WINDOW (self), GTK_WIN_POS_CENTER);

  self->stack = GTK_STACK (gtk_stack_new ());

  gtk_widget_set_size_request (GTK_WIDGET (self->stack), PAGE_WIDTH, PAGE_HEIGHT);
  gtk_stack_set_transition_type (self->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_add_named (self->stack, build_checking_page (self), "checking");
  gtk_stack_add_named (self->stack, build_updates_page (self), "updates");
  gtk_stack_add_named (self->stack, build_installing_page (self), "installing");
  gtk_stack_add_named (self->stack, build_result_page (self), "result");
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (self->stack));

  g_signal_connect (self->apt_upgrade, "progress", G_CALLBACK (on_apt_progress), self);
  g_signal_connect (self->apt_upgrade, "input-needed", G_CALLBACK (on_apt_input_needed), self);
  g_signal_connect (self->apt_upgrade, "finished", G_CALLBACK (on_apt_finished), self);

  gtk_widget_show_all (GTK_WIDGET (self->stack));
}



GtkWidget *
shopno_update_window_new (GtkApplication *application)
{
  ShopnoUpdateWindow *self;

  self = g_object_new (SHOPNO_TYPE_UPDATE_WINDOW, "application", application, NULL);
  start_check (self);

  return GTK_WIDGET (self);
}



void
shopno_update_load_style (void)
{
  g_autoptr(GtkCssProvider) colors = gtk_css_provider_new ();
  g_autoptr(GtkCssProvider) provider = gtk_css_provider_new ();

  /* Below the theme, so these colors only fill in what it leaves out */
  gtk_css_provider_load_from_resource (colors, RESOURCE_PATH "/shopno-update-helper-colors.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (colors),
                                             GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);

  gtk_css_provider_load_from_resource (provider, RESOURCE_PATH "/shopno-update-helper.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (), RESOURCE_PATH "/icons");
}



/*
 * Preview, for tools/shopno-update-preview: every page with sample data,
 * without running apt or Flatpak
 */

static const gchar *preview_simulation =
  "Inst code [1.143.1-1792691907] (1.144.0-1793133881 code stable:stable [amd64])\n"
  "Inst google-chrome-stable [156.0.8078.47-1] (156.0.8078.79-1 stable:stable [amd64])\n"
  "Inst openjdk-21-jre [21.0.12+1-1~deb13u1] (21.0.13+9-1~deb13u2 Debian-Security:13/stable-security [amd64])\n"
  "Inst openjdk-21-jre-headless [21.0.12+1-1~deb13u1] (21.0.13+9-1~deb13u2 Debian-Security:13/stable-security [amd64])\n"
  "Inst turbowarp-desktop [1.17.0] (1.17.1 stable:stable [amd64])\n"
  "Inst tzdata [2026e-0+deb13u1] (2026f-0+deb13u1 Debian:13.8/stable-updates [all])\n"
  "0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n";

/* A debconf dialog, as whiptail draws it */
static const gchar *preview_dialog =
  "\033[44m\033[2J\033[1;1H\033[37mPackage configuration\033[0m"
  "\033[5;6H\033[47;30m┌────────────────────┤ \033[31mConfiguring code\033[30m ├─────────────────────┐"
  "\033[6;6H│                                                            │"
  "\033[7;6H│ The installer would like to add the Microsoft repository   │"
  "\033[8;6H│ and signing key to update VS Code through apt.             │"
  "\033[9;6H│                                                            │"
  "\033[10;6H│ Add Microsoft apt repository for Visual Studio Code?       │"
  "\033[11;6H│                                                            │"
  "\033[12;6H│          \033[41;37m<Yes>\033[47;30m                           <No>             │"
  "\033[13;6H│                                                            │"
  "\033[14;6H└────────────────────────────────────────────────────────────┘\033[0m";

static void
preview_add_flatpak (GPtrArray   *updates,
                     const gchar *id,
                     const gchar *name,
                     gboolean     is_app)
{
  ShopnoFlatpakUpdate *update = g_new0 (ShopnoFlatpakUpdate, 1);

  update->id = g_strdup (id);
  update->name = g_strdup (name);
  update->ref = g_strdup_printf ("%s/%s/x86_64/stable", is_app ? "app" : "runtime", id);
  update->is_app = is_app;
  g_ptr_array_add (updates, update);
}



static void
preview_fill (ShopnoUpdateWindow *self,
              gboolean            with_updates,
              gboolean            with_flatpak)
{
  reset_results (self);

  self->apt = g_new0 (ShopnoAptCheck, 1);
  self->apt->changes = shopno_apt_parse_simulation (with_updates ? preview_simulation : "", NULL);
  self->apt->download_size = with_updates ? 601 * 1000 * 1000 : 0;

  self->flatpak_available = with_flatpak;
  if (!with_flatpak)
    return;

  self->flatpak_updates = g_ptr_array_new_with_free_func ((GDestroyNotify) shopno_flatpak_update_free);
  if (!with_updates)
    return;

  preview_add_flatpak (self->flatpak_updates, "app.zen_browser.zen", "Zen", TRUE);
  preview_add_flatpak (self->flatpak_updates, "io.github.kolunmi.Bazaar", "Bazaar", TRUE);
  preview_add_flatpak (self->flatpak_updates, "io.typora.Typora", "Typora", TRUE);
  preview_add_flatpak (self->flatpak_updates, "org.gnome.Platform", "GNOME Application Platform version 51", FALSE);
  preview_add_flatpak (self->flatpak_updates, "org.freedesktop.Platform.GL.default", "Mesa", FALSE);
  preview_add_flatpak (self->flatpak_updates, "org.freedesktop.Platform.codecs-extra", "Codecs Extra Extension", FALSE);
}



static void
preview_installing (ShopnoUpdateWindow *self)
{
  prepare_install (self);
  self->installing = FALSE;
  set_step (self, SOURCE_APT, "shopno-step-active", NULL);
  vte_terminal_reset (self->terminal, TRUE, TRUE);
  set_progress (self, 0.62, "Installing google-chrome-stable (amd64)");
  vte_terminal_feed (self->terminal,
                     "Get:6 https://dl.google.com/linux/chrome-stable/deb stable/main amd64 google-chrome-stable 156.0.8078.79-1 [146 MB]\r\n"
                     "Fetched 601 MB in 29s (21.0 MB/s)\r\n"
                     "Preparing to unpack .../google-chrome-stable_156.0.8078.79-1_amd64.deb ...\r\n", -1);
}



gboolean
shopno_update_window_preview (ShopnoUpdateWindow *self,
                              const gchar        *page)
{
  if (g_strcmp0 (page, "checking") == 0)
    {
      gtk_stack_set_visible_child_name (self->stack, "checking");
      source_row_set (&self->check_rows[SOURCE_APT], STATE_OK, "6 updates", NULL);
      source_row_set (&self->check_rows[SOURCE_FLATPAK], STATE_BUSY, _("Checking…"), NULL);
    }
  else if (g_strcmp0 (page, "updates") == 0)
    {
      preview_fill (self, TRUE, TRUE);
      show_updates (self);
    }
  else if (g_strcmp0 (page, "updates-no-flatpak") == 0)
    {
      preview_fill (self, TRUE, FALSE);
      show_updates (self);
    }
  else if (g_strcmp0 (page, "installing") == 0)
    {
      preview_fill (self, TRUE, TRUE);
      preview_installing (self);
    }
  else if (g_strcmp0 (page, "input") == 0)
    {
      preview_fill (self, TRUE, TRUE);
      preview_installing (self);
      set_progress (self, 0.55, NULL);
      vte_terminal_feed (self->terminal, preview_dialog, -1);
      on_apt_input_needed (self->apt_upgrade, TRUE, "code", self);
    }
  else if (g_strcmp0 (page, "done") == 0 || g_strcmp0 (page, "done-restart") == 0)
    {
      preview_fill (self, TRUE, TRUE);
      if (g_strcmp0 (page, "done-restart") == 0)
        {
          g_autoptr(GPtrArray) kernel = shopno_apt_parse_simulation (
            "Inst linux-image-amd64 [6.12.113-1] (6.12.114-1 Debian:13.8/stable [amd64])\n", NULL);
          g_ptr_array_add (self->apt->changes, g_ptr_array_steal_index (kernel, 0));
        }
      self->apt_result = RESULT_OK;
      self->flatpak_result = RESULT_OK;
      self->flatpak_updated = self->flatpak_updates->len;
      self->running_apps = g_strdupv ((gchar *[]) { (gchar *) "Zen", NULL });
      finish_install (self);
    }
  else if (g_strcmp0 (page, "up-to-date") == 0)
    {
      preview_fill (self, FALSE, FALSE);
      checks_done (self);
    }
  else if (g_strcmp0 (page, "partial") == 0)
    {
      preview_fill (self, TRUE, TRUE);
      self->apt_result = RESULT_OK;
      self->flatpak_result = RESULT_FAILED;
      self->flatpak_message = g_strdup ("Could not download from flathub: Could not resolve host");
      g_string_append (self->flatpak_log, "Failed to update Zen: Could not resolve host: dl.flathub.org\n");
      finish_install (self);
    }
  else if (g_strcmp0 (page, "error") == 0)
    {
      preview_fill (self, FALSE, TRUE);
      g_clear_pointer (&self->apt, shopno_apt_check_free);
      self->apt_error = g_error_new_literal (SHOPNO_APT_ERROR, SHOPNO_APT_ERROR_NETWORK,
                                             _("Check your internet connection and try again."));
      self->apt_log = g_strdup ("Err:1 http://deb.debian.org/debian trixie InRelease\n"
                                "  Temporary failure resolving 'deb.debian.org'\n");
      checks_done (self);
    }
  else
    {
      return FALSE;
    }

  return TRUE;
}



GtkWidget *
shopno_update_window_new_preview (GtkApplication *application)
{
  return g_object_new (SHOPNO_TYPE_UPDATE_WINDOW, "application", application, NULL);
}
