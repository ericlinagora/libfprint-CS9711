envs = environment()
# Enable debug messages and abort on warnings
envs.set('G_DEBUG', 'fatal-warnings')
envs.set('G_MESSAGES_DEBUG', 'all')

# Setup paths
envs.set('MESON_SOURCE_ROOT', meson.build_root())
envs.prepend('GI_TYPELIB_PATH', join_paths(meson.build_root(), 'libfprint'))
envs.prepend('LD_LIBRARY_PATH', join_paths(meson.build_root(), 'libfprint'))

# Set FP_DEVICE_EMULATION so that drivers can adapt (e.g. to use fixed
# random numbers rather than proper ones)
envs.set('FP_DEVICE_EMULATION', '1')

envs.set('NO_AT_BRIDGE', '1')

if get_option('introspection')
    if 'virtual_image' in drivers
        test(
            'virtual-image',
            find_program('virtual-image.py'),
            args: '--verbose',
            env: envs,
            depends: libfprint_typelib,
        )
    endif

    if 'vfs5011' in drivers
        test(
            'vfs5011',
            find_program('umockdev-test.py'),
            args: join_paths(meson.current_source_dir(), 'vfs5011'),
            env: envs,
            timeout: 10,
            depends: libfprint_typelib,
        )
    endif

    if 'synaptics' in drivers
        test(
            'synaptics',
            find_program('umockdev-test.py'),
            args: join_paths(meson.current_source_dir(), 'synaptics'),
            env: envs,
            timeout: 10,
            depends: libfprint_typelib,
        )
    endif
endif
