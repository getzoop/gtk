gdk_broadway_sources = files([
  'broadway-output.c',
  'broadway-server.c',
  'broadwayd.c',
  'gdkbroadway-server.c',
  'gdkcairocontext-broadway.c',
  'gdkdrawcontext-broadway.c',
  'gdkcursor-broadway.c',
  'gdkdevice-broadway.c',
  'gdkdisplay-broadway.c',
  'gdkdnd-broadway.c',
  'gdkeventsource.c',
  'gdkglobals-broadway.c',
  'gdkkeys-broadway.c',
  'gdkmonitor-broadway.c',
  'gdkselection-broadway.c',
  'gdksurface-broadway.c',
])

gdk_broadway_public_headers = [
  'gdkbroadwaydisplay.h',
  'gdkbroadwaysurface.h',
  'gdkbroadwaycursor.h',
  'gdkbroadwaymonitor.h',
]

# Broadway backend headers aren't installed it seems
#install_headers(gdk_broadway_public_headers, subdir: 'gtk-4.0/gdk/broadway/')
#install_headers('gdkbroadway.h', subdir: 'gtk-4.0/gdk/')

gdk_broadway_deps = [shmlib]

gen_c_array = find_program('gen-c-array.py')

clienthtml_h = custom_target('clienthtml.h',
  input : 'client.html',
  output : 'clienthtml.h',
  command : [
    gen_c_array,
    '--array-name=client_html',
    '--output=@OUTPUT@',
    '@INPUT@',
  ],
)

libgdk_broadway = static_library('gdk-broadway',
  clienthtml_h,
  gdk_broadway_sources, gdkconfig, gdkenum_h,
  include_directories: [confinc, gdkinc],
  c_args: [
    '-DGDK_COMPILATION',
    '-DG_LOG_DOMAIN="Gdk"',
  ] + common_cflags,
  link_args: common_ldflags,
  dependencies: [gdk_deps, gdk_broadway_deps])

# gtk4-broadwayd

broadwayd_syslib = os_win32 ? find_library('ws2_32') : shmlib

broadwayjs_h = custom_target('broadwayjs.h',
  input : ['broadway.js'],
  output : 'broadwayjs.h',
  command : [
    gen_c_array,
    '--array-name=broadway_js',
    '--output=@OUTPUT@',
    '@INPUT0@',
  ],
)

executable('gtk4-broadwayd',
  clienthtml_h, broadwayjs_h,
  'broadwayd.c', 'broadway-server.c', 'broadway-output.c',
  include_directories: [confinc, gdkinc, include_directories('.')],
  c_args: ['-DGDK_COMPILATION', '-DG_LOG_DOMAIN="Gdk"', ],
  dependencies : [broadwayd_syslib, gdk_deps],
  install : true)
