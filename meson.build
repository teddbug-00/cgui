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
m_dep      = cc.find_library('m', required: true)

glslang = find_program('glslangValidator', 'glslang', required: true)

shaders = [
  ['rounded_rect.vert', 'vert'],
  ['rounded_rect.frag', 'frag'],
  ['blur.vert', 'vert'],
  ['blur.frag', 'frag'],
  ['blit.vert', 'vert'],
  ['blit.frag', 'frag'],
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
  'src/style.c',
  'src/panel.c',
  'src/button.c',
  'src/vk_context.c',
  'src/vk_draw.c',
  'src/spv_loader.c',
)

executable(
  'cgui',
  sources,
  spv_targets,
  include_directories: include_directories('src'),
  dependencies: [sdl3_dep, vulkan_dep, m_dep],
  install: false,
)
