# Contributing to Update Helper

## Reporting bugs

Open an issue with the form that fits: **Bug report**, **Feature request**,
or **Packaging request or problem**. For questions, use
[Discussions](https://github.com/fa-saikat/shopno-update-helper/discussions)
instead. Report security problems privately, as described in
[SECURITY.md](SECURITY.md).

The **Details** section in the window has the full log of what apt and
Flatpak did. Run `shopno-update-helper` from a terminal to see GTK and GLib
warnings too, and set `G_MESSAGES_DEBUG=shopno-update` for debug messages.

## Development setup

Follow the build steps in the [README](README.md). The polkit policy only
matches the installed helpers, so to try a change for real, reinstall:

```sh
meson compile -C build && sudo meson install -C build && shopno-update-helper
```

Use a virtual machine or a system you can afford to break when working on
the upgrade path: it installs real updates.

### Preview tool

For layout and style work, build the preview tool. It opens any page with
sample data, and never runs apt or Flatpak:

```sh
meson setup build -Dpreview=true
meson compile -C build
./build/tools/shopno-update-preview updates
```

Pages: `checking`, `updates`, `updates-no-flatpak`, `installing`, `input`,
`done`, `done-restart`, `up-to-date`, `partial` and `error`. Buttons that
would start work do nothing useful.

The screenshots in `docs/screenshots/` were made with it, using the Adwaita
theme.

### Tests

```sh
meson test -C build --print-errorlogs
```

The tests cover parsing apt's output (`src/shopno-apt.c`). When apt changes
its output, add the real output to `tests/test-apt.c` before changing the
parser.

## Code style

The code follows the Xfce and GNOME C style:

- Two-space indentation, no tabs.
- GNU brace placement, and a space before the opening parenthesis of a call.
- Function return types on their own line.
- Prefix public symbols with `shopno_` and types with `Shopno`.
- Build with `-Dwerror=true` before sending a pull request.

When in doubt, match the code around your change.

`src/shopno-apt-helper.c` runs as root. Keep it free of GLib, keep it
taking no input beyond what it takes today, and explain any change to it,
or to the polkit policy, in the pull request.

## Translations

User-visible strings are marked with `_()`. To add a language, add its
code to `po/LINGUAS`, then:

```sh
meson compile -C build shopno-update-helper-pot
meson compile -C build shopno-update-helper-update-po
```

and translate `po/<code>.po`.

## Pull requests

- Keep each pull request focused on one change.
- Write commit messages in the imperative mood ("Add retry button"), with a
  short summary line and an optional body explaining why.
- Include a screenshot for visual changes.

## Releasing

Releases are drafted by CI and published by hand:

1. Branch `release/x.y.z` from `main`, set the version in `meson.build`, and
   add a `packaging/debian/changelog` entry for `x.y.z-1`.
2. Open a pull request into `main`. The package workflow builds and
   test-installs every package, and checks that the branch name, the
   version in `meson.build` and the changelog agree.
3. Merge it. The workflow builds the packages from `main` again and attaches
   them to a draft release for `vx.y.z`.
4. Test the packages from the draft, edit its notes, and publish it.
   Publishing creates the `vx.y.z` tag.

Merging a `release/` branch is the only way a release gets drafted; tags
pushed by hand do nothing.
