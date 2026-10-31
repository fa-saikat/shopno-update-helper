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
 * The only code that runs as root. Started through pkexec, and built
 * twice, so each binary has its own polkit action:
 *
 *   apt-refresh            apt-get update
 *   apt-upgrade STATUS     dpkg --configure -a, apt-get -f install,
 *                          apt-get full-upgrade, reporting progress on
 *                          the STATUS FIFO owned by the calling user
 *
 * It takes no other input: no commands, packages or options come from
 * the caller. Keep it that way, and keep it free of GLib.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define APT_GET "/usr/bin/apt-get"
#define DPKG    "/usr/bin/dpkg"

/* apt writes machine-readable progress to this descriptor */
#define STATUS_FD   3
#define STATUS_FD_S "3"

static const char *program = "shopno-update-helper";



static int
run (char *const argv[])
{
  pid_t pid;
  int status;

  fflush (stdout);
  fflush (stderr);

  pid = fork ();
  if (pid < 0)
    {
      fprintf (stderr, "%s: fork: %s\n", program, strerror (errno));
      return 1;
    }

  if (pid == 0)
    {
      execv (argv[0], argv);
      fprintf (stderr, "%s: %s: %s\n", program, argv[0], strerror (errno));
      _exit (127);
    }

  while (waitpid (pid, &status, 0) < 0)
    {
      if (errno != EINTR)
        return 1;
    }

  if (WIFEXITED (status))
    return WEXITSTATUS (status);
  if (WIFSIGNALED (status))
    return 128 + WTERMSIG (status);

  return 1;
}



static void
setup_environment (void)
{
  const char *term = getenv ("TERM");

  /* pkexec already clears the environment; set what apt needs */
  setenv ("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
  if (term == NULL || *term == '\0')
    setenv ("TERM", "xterm-256color", 1);

  /* apt-listchanges would otherwise open a pager and wait */
  setenv ("APT_LISTCHANGES_FRONTEND", "none", 1);
}



#ifdef HELPER_REFRESH

int
main (int    argc,
      char **argv)
{
  char *const update[] = {
    APT_GET, "-o", "DPkg::Lock::Timeout=120", "update", NULL
  };

  program = "apt-refresh";

  if (geteuid () != 0 || argc != 1)
    {
      fprintf (stderr, "usage: pkexec %s\n", argv[0]);
      return 2;
    }

  setup_environment ();

  /* The caller parses the output */
  setenv ("LC_ALL", "C", 1);

  return run (update);
}

#endif /* HELPER_REFRESH */



#ifdef HELPER_UPGRADE

/* Opens the caller's status FIFO as STATUS_FD. The path comes from an
 * unprivileged user, so refuse to follow a final symlink and accept only
 * a FIFO owned by that user: writing to it cannot touch any other file. */
static int
open_status_fifo (const char *path)
{
  const char *uid_str = getenv ("PKEXEC_UID");
  struct stat st;
  char *end;
  long uid;
  int fd;

  if (uid_str == NULL)
    {
      fprintf (stderr, "%s: must be started through pkexec\n", program);
      return -1;
    }

  errno = 0;
  uid = strtol (uid_str, &end, 10);
  if (errno != 0 || *end != '\0' || uid < 0)
    {
      fprintf (stderr, "%s: invalid PKEXEC_UID\n", program);
      return -1;
    }

  /* O_NONBLOCK so a FIFO without a reader fails instead of hanging */
  fd = open (path, O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    {
      fprintf (stderr, "%s: %s: %s\n", program, path, strerror (errno));
      return -1;
    }

  if (fstat (fd, &st) < 0 || !S_ISFIFO (st.st_mode) || st.st_uid != (uid_t) uid)
    {
      fprintf (stderr, "%s: %s: not a FIFO owned by the caller\n", program, path);
      close (fd);
      return -1;
    }

  if (fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) & ~O_NONBLOCK) < 0
      || dup2 (fd, STATUS_FD) < 0)
    {
      fprintf (stderr, "%s: %s\n", program, strerror (errno));
      close (fd);
      return -1;
    }

  if (fd != STATUS_FD)
    close (fd);

  return 0;
}



int
main (int    argc,
      char **argv)
{
  char *const configure[] = {
    DPKG, "--configure", "-a", NULL
  };
  char *const fix_broken[] = {
    APT_GET, "-y", "-o", "DPkg::Lock::Timeout=300", "-f", "install", NULL
  };
  char *const upgrade[] = {
    APT_GET, "-y",
    "-o", "APT::Status-Fd=" STATUS_FD_S,
    "-o", "DPkg::Lock::Timeout=300",
    "-o", "Dpkg::Progress-Fancy=0",
    "full-upgrade", NULL
  };
  int status;

  program = "apt-upgrade";

  if (geteuid () != 0 || argc != 2)
    {
      fprintf (stderr, "usage: pkexec %s STATUS-FIFO\n", argv[0]);
      return 2;
    }

  if (open_status_fifo (argv[1]) < 0)
    return 1;

  /* If the window goes away, keep going rather than leave dpkg half done */
  signal (SIGPIPE, SIG_IGN);

  setup_environment ();

  /* Both are no-ops on a healthy system */
  status = run (configure);
  if (status == 0)
    status = run (fix_broken);
  if (status == 0)
    status = run (upgrade);

  return status;
}

#endif /* HELPER_UPGRADE */
