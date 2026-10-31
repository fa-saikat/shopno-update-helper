# Security

Update Helper installs software as root, so security reports are taken
seriously and handled first.

## Reporting a vulnerability

Please do **not** open a public issue. Instead, use
[GitHub's private vulnerability reporting](https://github.com/fa-saikat/shopno-update-helper/security/advisories/new),
or email fahimabrar.saikat@gmail.com with "Update Helper security" in the
subject.

Include what an attacker needs (a local account, an active session, a
network position), what they gain, and the steps to reproduce it. You will
get a reply within a week. Fixes are released before the details are made
public, and you will be credited unless you prefer not to be.

## Supported versions

Only the latest release gets security fixes.

## How privileges are handled

This is the design a report would be measured against.

- **The window runs as the user.** It never runs as root and never asks
  for a password itself.
- **Two root helpers, started through pkexec**, in
  `/usr/libexec/shopno-update-helper/`. Each has its own polkit action in
  `com.jadupc.shopno-update-helper.policy`, matched by its path:

  | Helper        | Action                                       | Default for the active user |
  |---------------|----------------------------------------------|-----------------------------|
  | `apt-refresh` | `com.jadupc.shopno-update-helper.apt-refresh` | allowed                     |
  | `apt-upgrade` | `com.jadupc.shopno-update-helper.apt-upgrade` | administrator password, kept for a few minutes |

  Inactive and remote sessions need an administrator password for both.
  Administrators can change these defaults with polkit rules.

- **The helpers accept no commands, packages or options.** `apt-refresh`
  takes no arguments and runs `apt-get update`. `apt-upgrade` takes one: the
  path of a FIFO to report progress on. It runs `dpkg --configure -a`,
  `apt-get -f install` and `apt-get full-upgrade` with fixed arguments and
  a fixed `PATH`. Being allowed to run a helper means only "refresh the
  package lists" or "install all available updates".
- **The progress FIFO is opened carefully.** The path comes from the user,
  so `apt-upgrade` opens it with `O_NOFOLLOW` and `O_NONBLOCK`, then checks
  that it is a FIFO owned by the user who started pkexec (`PKEXEC_UID`).
  Writing to it can never touch another file.
- **The helpers do not use GLib** or any library beyond the C library, to
  keep the root code small enough to read in one sitting
  (`src/shopno-apt-helper.c`).
- **Flatpak updates** go through libflatpak and Flatpak's own system
  helper and polkit policy. Update Helper adds no privileges for them.
- **Restarting** goes through logind, which applies its own polkit policy.

Changes to `src/shopno-apt-helper.c` or the polkit policy get extra review.
Please say so in the pull request.
