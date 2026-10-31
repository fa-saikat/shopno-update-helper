project(
  'shopno-update-helper',
  'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.59.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

gnome = import('gnome')
i18n = import('i18n')

application_id = 'com.jadupc.ShopnoUpdateHelper'
gettext_package = meson.project_name()

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
localedir = prefix / get_option('localedir')
helperdir = prefix / get_option('libexecdir') / meson.project_name()

glib = dependency('glib-2.0', version: '>= 2.72.0')
gio = dependency('gio-2.0', version: '>= 2.72.0')
gtk = dependency('gtk+-3.0', version: '>= 3.24.0')
vte = dependency('vte-2.91', version: '>= 0.68.0')
flatpak = dependency('flatpak', version: '>= 1.12.0', required: get_option('flatpak'))

conf = configuration_data()
conf.set_quoted('APPLICATION_ID', application_id)
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
conf.set_quoted('PACKAGE_LOCALE_DIR', localedir)
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('REFRESH_HELPER', helperdir / 'apt-refresh')
conf.set_quoted('UPGRADE_HELPER', helperdir / 'apt-upgrade')
conf.set10('HAVE_FLATPAK', flatpak.found())
configure_file(output: 'config.h', configuration: conf)

config_inc = include_directories('.')

add_project_arguments(
  '-DG_LOG_DOMAIN="shopno-update"',
  '-Wno-unused-parameter',
  language: 'c',
)

subdir('po')
subdir('data')
subdir('src')

if get_option('tests')
  subdir('tests')
endif

if get_option('preview')
  subdir('tools')
endif

summary({
  'Flatpak support': flatpak.found(),
  'Helpers': helperdir,
  'Tests': get_option('tests'),
  'Preview tool': get_option('preview'),
})
