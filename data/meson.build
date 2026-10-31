i18n.merge_file(
  input: 'shopno-update-helper.desktop.in',
  output: application_id + '.desktop',
  type: 'desktop',
  po_dir: meson.project_source_root() / 'po',
  install: true,
  install_dir: datadir / 'applications',
)

policy_conf = configuration_data()
policy_conf.set('HELPERDIR', helperdir)

configure_file(
  input: 'com.jadupc.shopno-update-helper.policy.in',
  output: 'com.jadupc.shopno-update-helper.policy',
  configuration: policy_conf,
  install: true,
  install_dir: datadir / 'polkit-1' / 'actions',
)

install_man('shopno-update-helper.1')
