envs = environment()
envs.set('G_DEBUG', 'fatal-warnings')
envs.set('G_MESSAGES_DEBUG', 'all')
envs.set('MESON_SOURCE_ROOT', meson.build_root())
envs.prepend('GI_TYPELIB_PATH', join_paths(meson.build_root(), 'libfprint'))
envs.prepend('LD_LIBRARY_PATH', join_paths(meson.build_root(), 'libfprint'))
envs.set('NO_AT_BRIDGE', '1')

if 'virtual_image' in drivers
    test(
        'virtual-image',
        find_program('virtual-image.py'),
        args: '--verbose',
        env: envs,
    )
endif

