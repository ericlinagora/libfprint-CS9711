envs = environment()
# Enable debug messages and abort on warnings
envs.set('G_DEBUG', 'fatal-warnings')
envs.set('G_MESSAGES_DEBUG', 'all')

# Setup paths
envs.set('MESON_SOURCE_ROOT', meson.build_root())
envs.prepend('LD_LIBRARY_PATH', join_paths(meson.build_root(), 'libfprint'))

# Set FP_DEVICE_EMULATION so that drivers can adapt (e.g. to use fixed
# random numbers rather than proper ones)
envs.set('FP_DEVICE_EMULATION', '1')

# Set a colon-separated list of native drivers we enable in tests
envs.set('FP_DRIVERS_WHITELIST', 'virtual_image')

envs.set('NO_AT_BRIDGE', '1')

if get_option('introspection')
    envs.prepend('GI_TYPELIB_PATH', join_paths(meson.build_root(), 'libfprint'))

    if 'virtual_image' in drivers
        test('virtual-image',
            find_program('virtual-image.py'),
            args: '--verbose',
            env: envs,
            depends: libfprint_typelib,
        )
    endif

    drivers_tests = [
        'vfs5011',
        'synaptics',
    ]

    foreach driver_test: drivers_tests
        driver_envs = envs
        driver_envs.set('FP_DRIVERS_WHITELIST', driver_test)

        test(driver_test,
            find_program('umockdev-test.py'),
            args: join_paths(meson.current_source_dir(), driver_test),
            env: driver_envs,
            suite: ['drivers'],
            timeout: 10,
            depends: libfprint_typelib,
        )
    endforeach
endif

if 'virtual_image' in drivers
    test_utils = static_library('fprint-test-utils',
        sources: [
            'test-utils.c',
            'test-device-fake.c',
        ],
        dependencies: libfprint_private_dep,
        install: false)

    unit_tests = [
        'fp-context',
        'fp-device',
    ]

    foreach test_name: unit_tests
        basename = 'test-' + test_name
        test_exe = executable(basename,
            sources: basename + '.c',
            dependencies: libfprint_private_dep,
            c_args: common_cflags,
            link_with: test_utils)
        test(test_name,
            find_program('test-runner.sh'),
            suite: ['unit-tests'],
            args: [test_exe],
            env: envs,
        )
    endforeach
endif

gdb = find_program('gdb', required: false)
if gdb.found()
    add_test_setup('gdb',
        timeout_multiplier: 1000,
        env: [
            'LIBFPRINT_TEST_WRAPPER=@0@ --args'.format(
                gdb.path())
        ])
endif

valgrind = find_program('valgrind', required: false)
if valgrind.found()
    glib_share = glib_dep.get_pkgconfig_variable('prefix') / 'share' / glib_dep.name()
    glib_suppressions = glib_share + '/valgrind/glib.supp'
    python_suppressions = '@0@/@1@'.format(meson.source_root(),
        files('valgrind-python.supp')[0])
    add_test_setup('valgrind',
        timeout_multiplier: 10,
        env: [
            'G_SLICE=always-malloc',
            ('LIBFPRINT_TEST_WRAPPER=@0@ --tool=memcheck --leak-check=full ' +
             '--suppressions=@1@ --suppressions=@2@').format(
                valgrind.path(), glib_suppressions, python_suppressions)
        ])
endif
