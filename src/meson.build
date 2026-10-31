resources = gnome.compile_resources(
  'shopno-update-helper-resources',
  'resources/shopno-update-helper.gresource.xml',
  source_dir: 'resources',
  c_name: 'shopno_update_helper',
)

shopno_deps = [glib, gio, gtk, vte, flatpak]

# Parsing and apt checks, shared with the tests
shopno_apt = static_library(
  'shopno-apt',
  sources: ['shopno-apt.c'],
  include_directories: config_inc,
  dependencies: [glib, gio],
)

# Everything except main(), shared with the preview tool
shopno_core = static_library(
  'shopno-core',
  sources: [
    'shopno-apt-upgrade.c',
    'shopno-flatpak.c',
    'shopno-update-window.c',
    resources,
  ],
  include_directories: config_inc,
  dependencies: shopno_deps,
  link_whole: shopno_apt,
)

shopno_inc = include_directories('.')

executable(
  'shopno-update-helper',
  sources: ['shopno-update-helper.c'],
  include_directories: config_inc,
  dependencies: shopno_deps,
  link_whole: shopno_core,
  install: true,
)

# The root helpers, started through pkexec. One binary per polkit action,
# so refreshing can be allowed without a password while upgrading still
# needs administrator authentication. No GLib, on purpose.
foreach helper : ['refresh', 'upgrade']
  executable(
    'apt-' + helper,
    sources: ['shopno-apt-helper.c'],
    c_args: '-DHELPER_' + helper.to_upper(),
    include_directories: config_inc,
    install: true,
    install_dir: helperdir,
  )
endforeach
