project('satl-source', 'cpp',
  version : '0.1.0',
  license : 'MIT',
  default_options : ['cpp_std=c++20', 'warning_level=2'])

gtkmm      = dependency('gtkmm-4.0')
sourceview = dependency('gtksourceview-5')
threads    = dependency('threads')

sources = files(
  'src/main.cpp',
  'src/gtk_host.cpp',
  'src/actor.cpp',
  'src/directory_view.cpp',
  'src/main_window.cpp',
  'src/source_editor_view.cpp',
  'src/editor_tabs.cpp',
  'src/search_bar.cpp',
  'src/session.cpp',
)

executable('satl-source', sources,
  dependencies : [gtkmm, sourceview, threads],
  install : true)
