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

#include "shopno-apt.h"

/* Real output of `LC_ALL=C apt-get -s full-upgrade`, apt 3.0 */
static const gchar *simulation_apt3 =
  "NOTE: This is only a simulation!\n"
  "      apt-get needs root privileges for real execution.\n"
  "Reading package lists...\n"
  "Building dependency tree...\n"
  "Reading state information...\n"
  "Calculating upgrade...\n"
  "The following packages will be REMOVED:\n"
  "  libfaac0\n"
  "The following NEW packages will be installed:\n"
  "  libfaac0t64\n"
  "The following packages will be upgraded:\n"
  "  faac\n"
  "1 upgraded, 1 newly installed, 1 to remove and 2 not upgraded.\n"
  "Inst faac [1.30-1] (1.31.1+dfsg-1 Debian:13.7/stable [amd64]) []\n"
  "Remv libfaac0 [1.30-1] []\n"
  "Inst libfaac0t64 (1.31.1+dfsg-1 Debian:13.7/stable [amd64])\n"
  "Conf faac (1.31.1+dfsg-1 Debian:13.7/stable [amd64])\n"
  "Conf libfaac0t64 (1.31.1+dfsg-1 Debian:13.7/stable [amd64])\n";



static void
test_simulation (void)
{
  g_autoptr(GPtrArray) changes = NULL;
  ShopnoAptChange *change;
  guint kept_back = 0;

  changes = shopno_apt_parse_simulation (simulation_apt3, &kept_back);

  g_assert_cmpuint (changes->len, ==, 3);
  g_assert_cmpuint (kept_back, ==, 2);

  /* Sorted: upgrades, new installs, removals */
  change = g_ptr_array_index (changes, 0);
  g_assert_cmpint (change->kind, ==, SHOPNO_APT_CHANGE_UPGRADE);
  g_assert_cmpstr (change->name, ==, "faac");
  g_assert_cmpstr (change->old_version, ==, "1.30-1");
  g_assert_cmpstr (change->new_version, ==, "1.31.1+dfsg-1");

  change = g_ptr_array_index (changes, 1);
  g_assert_cmpint (change->kind, ==, SHOPNO_APT_CHANGE_INSTALL);
  g_assert_cmpstr (change->name, ==, "libfaac0t64");
  g_assert_null (change->old_version);
  g_assert_cmpstr (change->new_version, ==, "1.31.1+dfsg-1");

  change = g_ptr_array_index (changes, 2);
  g_assert_cmpint (change->kind, ==, SHOPNO_APT_CHANGE_REMOVE);
  g_assert_cmpstr (change->name, ==, "libfaac0");
  g_assert_cmpstr (change->old_version, ==, "1.30-1");
  g_assert_null (change->new_version);
}



static void
test_simulation_empty (void)
{
  g_autoptr(GPtrArray) changes = NULL;
  guint kept_back = 99;

  changes = shopno_apt_parse_simulation ("Reading package lists...\n"
                                         "0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n",
                                         &kept_back);
  g_assert_cmpuint (changes->len, ==, 0);
  g_assert_cmpuint (kept_back, ==, 0);

  g_clear_pointer (&changes, g_ptr_array_unref);
  changes = shopno_apt_parse_simulation (NULL, NULL);
  g_assert_cmpuint (changes->len, ==, 0);
}



static void
test_simulation_multiarch (void)
{
  g_autoptr(GPtrArray) changes = NULL;
  ShopnoAptChange *change;

  changes = shopno_apt_parse_simulation ("Inst libc6:i386 [2.41-11] (2.41-12 Debian:13.8/stable [i386])\n", NULL);

  g_assert_cmpuint (changes->len, ==, 1);
  change = g_ptr_array_index (changes, 0);
  g_assert_cmpstr (change->name, ==, "libc6:i386");
  g_assert_cmpstr (change->new_version, ==, "2.41-12");
}



static void
test_versions (void)
{
  g_autofree gchar *upstream = shopno_apt_upstream_version ("1:2.4.1-3+deb13u1");
  g_autofree gchar *native = shopno_apt_upstream_version ("2026.10");
  g_autofree gchar *jre = shopno_apt_format_versions ("21.0.12+1-1~deb13u1", "21.0.13+9-1~deb13u2");
  g_autofree gchar *security = shopno_apt_format_versions ("1:9.20.15-1~deb13u1", "1:9.20.15-1~deb13u2");

  g_assert_cmpstr (upstream, ==, "2.4.1");
  g_assert_cmpstr (native, ==, "2026.10");
  g_assert_cmpstr (jre, ==, "21.0.12+1 → 21.0.13+9");

  /* Same upstream version: show the revisions, which are what changed */
  g_assert_cmpstr (security, ==, "9.20.15-1~deb13u1 → 9.20.15-1~deb13u2");
}



static void
test_download_size (void)
{
  const gchar *output =
    "'http://deb.debian.org/debian/pool/main/f/faac/faac_1.31.1%2bdfsg-1_amd64.deb' faac_1.31.1+dfsg-1_amd64.deb 41232 SHA256:00\n"
    "'http://deb.debian.org/debian/pool/main/t/tzdata/tzdata_2026f-0%2bdeb13u1_all.deb' tzdata_2026f-0+deb13u1_all.deb 268000 SHA256:11\n";

  g_assert_cmpuint (shopno_apt_parse_download_size (output), ==, 309232);
  g_assert_cmpuint (shopno_apt_parse_download_size (""), ==, 0);
  g_assert_cmpuint (shopno_apt_parse_download_size (NULL), ==, 0);
}



static void
test_status (void)
{
  ShopnoAptStatus status;

  g_assert_true (shopno_apt_parse_status ("dlstatus:3:42.5:Retrieving file 3 of 6", &status));
  g_assert_cmpint (status.kind, ==, SHOPNO_APT_STATUS_DOWNLOAD);
  g_assert_cmpfloat (status.percent, ==, 42.5);
  g_assert_null (status.package);
  g_assert_cmpstr (status.message, ==, "Retrieving file 3 of 6");
  shopno_apt_status_clear (&status);

  /* The package carries an architecture, the message a colon */
  g_assert_true (shopno_apt_parse_status ("pmstatus:code:amd64:62.5:Installing code (amd64): step 2", &status));
  g_assert_cmpint (status.kind, ==, SHOPNO_APT_STATUS_INSTALL);
  g_assert_cmpfloat (status.percent, ==, 62.5);
  g_assert_cmpstr (status.package, ==, "code:amd64");
  g_assert_cmpstr (status.message, ==, "Installing code (amd64): step 2");
  shopno_apt_status_clear (&status);

  g_assert_true (shopno_apt_parse_status ("pmstatus:dpkg-exec:50:Running dpkg\n", &status));
  g_assert_null (status.package);
  g_assert_cmpstr (status.message, ==, "Running dpkg");
  shopno_apt_status_clear (&status);

  g_assert_true (shopno_apt_parse_status ("pmerror:/var/cache/apt/archives/foo.deb:80:subprocess failed", &status));
  g_assert_cmpint (status.kind, ==, SHOPNO_APT_STATUS_ERROR);
  g_assert_cmpstr (status.package, ==, "/var/cache/apt/archives/foo.deb");
  g_assert_cmpstr (status.message, ==, "subprocess failed");
  shopno_apt_status_clear (&status);

  g_assert_true (shopno_apt_parse_status ("pmconffile:/etc/foo.conf '/etc/foo.conf' '/etc/foo.conf.dpkg-new' 1 1", &status));
  g_assert_cmpint (status.kind, ==, SHOPNO_APT_STATUS_CONFFILE);
  shopno_apt_status_clear (&status);

  g_assert_false (shopno_apt_parse_status ("Reading package lists...", &status));
  g_assert_false (shopno_apt_parse_status ("pmstatus:broken", &status));
}



static void
test_refresh (void)
{
  const gchar *offline =
    "Err:1 http://deb.debian.org/debian trixie InRelease\n"
    "  Temporary failure resolving 'deb.debian.org'\n"
    "Err:2 http://security.debian.org/debian-security trixie-security InRelease\n"
    "  Temporary failure resolving 'security.debian.org'\n"
    "W: Some index files failed to download. They have been ignored, or old ones used instead.\n";
  const gchar *partial =
    "Hit:1 http://deb.debian.org/debian trixie InRelease\n"
    "Err:2 https://example.com/repo stable InRelease\n"
    "  Could not connect to example.com:443\n";
  const gchar *ok =
    "Hit:1 http://deb.debian.org/debian trixie InRelease\n"
    "Get:2 http://security.debian.org/debian-security trixie-security InRelease [43.4 kB]\n";

  g_assert_true (shopno_apt_refresh_network_failed (offline));
  g_assert_false (shopno_apt_refresh_network_failed (partial));
  g_assert_false (shopno_apt_refresh_network_failed (ok));

  g_assert_true (shopno_apt_refresh_sources_failed (partial));
  g_assert_false (shopno_apt_refresh_sources_failed (ok));
}



static void
test_needs_input (void)
{
  /* whiptail, as drawn by VTE */
  g_assert_true (shopno_apt_screen_needs_input ("Package configuration\n"
                                                " ┌──────┤ Configuring code ├──────┐\n"
                                                " │ Add Microsoft apt repository? │\n"
                                                " │   <Yes>            <No>       │\n"
                                                " └───────────────────────────────┘\n"));

  /* dpkg conffile prompt */
  g_assert_true (shopno_apt_screen_needs_input ("Configuration file '/etc/foo.conf'\n"
                                                " ==> Modified (by you or by a script) since installation.\n"
                                                "   What would you like to do about it ?  Your options are:\n"
                                                " *** foo.conf (Y/I/N/O/D/Z) [default=N] ? \n\n\n"));

  g_assert_true (shopno_apt_screen_needs_input ("Do you want to continue? [Y/n] "));

  g_assert_false (shopno_apt_screen_needs_input ("Setting up code (1.144.0) ...\n"
                                                 "Processing triggers for man-db (2.13.1-1) ...\n"));
  g_assert_false (shopno_apt_screen_needs_input ("53% [6 google-chrome-stable 138 MB/146 MB 94%]"));
  g_assert_false (shopno_apt_screen_needs_input (""));
  g_assert_false (shopno_apt_screen_needs_input (NULL));
}



static void
test_needs_restart (void)
{
  g_autoptr(GPtrArray) kernel = shopno_apt_parse_simulation (
    "Inst linux-image-6.12.114+deb13-amd64 (6.12.114-1 Debian:13.8/stable [amd64])\n", NULL);
  g_autoptr(GPtrArray) app = shopno_apt_parse_simulation (
    "Inst code [1.143.1-1792691907] (1.144.0-1793133881 code stable:stable [amd64])\n"
    "Remv libc6-dev [2.41-11]\n", NULL);

  g_assert_true (shopno_apt_changes_need_restart (kernel));
  g_assert_false (shopno_apt_changes_need_restart (app));
  g_assert_false (shopno_apt_changes_need_restart (NULL));
}



int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/apt/simulation", test_simulation);
  g_test_add_func ("/apt/simulation/empty", test_simulation_empty);
  g_test_add_func ("/apt/simulation/multiarch", test_simulation_multiarch);
  g_test_add_func ("/apt/versions", test_versions);
  g_test_add_func ("/apt/download-size", test_download_size);
  g_test_add_func ("/apt/status", test_status);
  g_test_add_func ("/apt/refresh", test_refresh);
  g_test_add_func ("/apt/needs-input", test_needs_input);
  g_test_add_func ("/apt/needs-restart", test_needs_restart);

  return g_test_run ();
}
