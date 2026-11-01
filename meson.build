project(
  'cgui',
  'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=c2x',
    'warning_level=2',
    'werror=false',
    'buildtype=debug',
  ],
)

cc = meson.get_compiler('c')

sdl3_dep   = dependency('sdl3', required: true, fallback: ['sdl3', 'sdl3_dep'])
vulkan_dep = dependency('vulkan', required: true)
freetype_dep = dependency('freetype2', required: true)
harfbuzz_dep = dependency('harfbuzz', required: true)
fontconfig_dep = dependency('fontconfig', required: host_machine.system() == 'linux')
if fontconfig_dep.found()
  add_project_arguments('-DCGUI_HAVE_FONTCONFIG=1', language: 'c')
endif
m_dep      = cc.find_library('m', required: true)

glslang = find_program('glslangValidator', 'glslang', required: true)

shaders = [
  ['rounded_rect.vert', 'vert'],
  ['rounded_rect.frag', 'frag'],
  ['blur.vert', 'vert'],
  ['blur.frag', 'frag'],
  ['blit.vert', 'vert'],
  ['blit.frag', 'frag'],
  ['icon.frag', 'frag'],
  ['text.vert', 'vert'],
  ['text.frag', 'frag'],
]

spv_targets = []
foreach s : shaders
  name = s[0]
  stage = s[1]
  spv_targets += custom_target(
    name.underscorify() + '_spv',
    input:  'src/shaders' / name,
    output: name + '.spv',
    command: [glslang, '-V', '-S', stage, '@INPUT@', '-o', '@OUTPUT@'],
  )
endforeach

sources = files(
  'src/main.c',
  'src/gui.c',
  'src/layout.c',
  'src/style.c',
  'src/text.c',
  'src/glyph_atlas.c',
  'src/panel.c',
  'src/button.c',
  'src/label.c',
  'src/toggle.c',
  'src/slider.c',
  'src/text_field.c',
  'src/tree_view.c',
  'src/vk_context.c',
  'src/vk_draw.c',
  'src/spv_loader.c',
)

executable(
  'cgui',
  sources,
  spv_targets,
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
  install: false,
)

executable(
  'cgui-text-demo',
  ['demos/text_demo.c', 'src/style.c', 'src/text.c', 'src/glyph_atlas.c',
   'src/vk_context.c', 'src/vk_draw.c', 'src/spv_loader.c'],
  spv_targets,
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
  install: false,
)

executable(
  'cgui-layout-demo',
  ['demos/layout_demo.c', 'src/gui.c', 'src/layout.c', 'src/style.c',
   'src/text.c', 'src/glyph_atlas.c', 'src/vk_context.c', 'src/vk_draw.c',
   'src/spv_loader.c'],
  spv_targets,
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
  install: false,
)

gui_test = executable(
  'cgui-gui-test',
  ['tests/gui_test.c', 'src/gui.c', 'src/layout.c', 'src/style.c'],
  include_directories: include_directories('src'),
  dependencies: [m_dep],
)
test('widget-hierarchy', gui_test)

layout_test = executable(
  'cgui-layout-test',
  ['tests/layout_test.c', 'src/gui.c', 'src/layout.c', 'src/style.c'],
  include_directories: include_directories('src'),
  dependencies: [m_dep],
)
test('widget-layout', layout_test)

text_test = executable(
  'cgui-text-test',
  ['tests/text_test.c', 'src/text.c'],
  include_directories: include_directories('src'),
  dependencies: [freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
test('text-rasterization', text_test)

label_test = executable(
  'cgui-label-test',
  ['tests/label_test.c', 'src/label.c', 'src/gui.c', 'src/style.c', 'src/vk_draw.c', 'src/glyph_atlas.c', 'src/text.c', 'src/vk_context.c', 'src/spv_loader.c'],
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
test('label-widget', label_test)

toggle_test = executable(
  'cgui-toggle-test',
  ['tests/toggle_test.c', 'src/toggle.c', 'src/gui.c', 'src/style.c', 'src/vk_draw.c', 'src/glyph_atlas.c', 'src/text.c', 'src/vk_context.c', 'src/spv_loader.c'],
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
test('toggle-widget', toggle_test)

slider_test = executable(
  'cgui-slider-test',
  ['tests/slider_test.c', 'src/slider.c', 'src/gui.c', 'src/style.c', 'src/vk_draw.c', 'src/glyph_atlas.c', 'src/text.c', 'src/vk_context.c', 'src/spv_loader.c'],
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
test('slider-widget', slider_test)

text_field_test = executable(
  'cgui-text-field-test',
  ['tests/text_field_test.c', 'src/text_field.c', 'src/gui.c', 'src/style.c', 'src/vk_draw.c', 'src/glyph_atlas.c', 'src/text.c', 'src/vk_context.c', 'src/spv_loader.c'],
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
test('text-field-widget', text_field_test)

tree_view_test = executable(
  'cgui-tree-view-test',
  ['tests/tree_view_test.c', 'src/tree_view.c', 'src/gui.c', 'src/style.c', 'src/vk_draw.c', 'src/glyph_atlas.c', 'src/text.c', 'src/vk_context.c', 'src/spv_loader.c'],
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
test('tree-view-widget', tree_view_test)

bench_exe = executable(
  'cgui-bench',
  ['benchmarks/bench_main.c', 'src/gui.c', 'src/style.c', 'src/glyph_atlas.c', 'src/text.c', 'src/panel.c', 'src/button.c', 'src/label.c', 'src/toggle.c', 'src/slider.c', 'src/text_field.c', 'src/vk_draw.c', 'src/vk_context.c', 'src/spv_loader.c'],
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
benchmark('core-bench', bench_exe)

render_bench_exe = executable(
  'cgui-render-bench',
  ['benchmarks/render_bench.c', 'src/style.c', 'src/glyph_atlas.c', 'src/text.c', 'src/vk_draw.c', 'src/vk_context.c', 'src/spv_loader.c'],
  spv_targets,
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, freetype_dep, harfbuzz_dep, fontconfig_dep, m_dep],
)
