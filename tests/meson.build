test_apt = executable(
  'test-apt',
  sources: ['test-apt.c'],
  include_directories: [config_inc, shopno_inc],
  dependencies: [glib, gio],
  link_with: shopno_apt,
)

test('apt', test_apt, protocol: 'tap', env: ['G_TEST_SRCDIR=' + meson.current_source_dir()])
