
examples = [ 'enroll', 'verify', 'manage-prints' ]
foreach example: examples
    executable(example,
               [example + '.c', 'storage.c', 'utilities.c'],
               dependencies: [libfprint_dep, glib_dep],
               include_directories: [
                 root_inc,
               ],
               c_args: common_cflags)
endforeach

executable('cpp-test',
           'cpp-test.cpp',
           dependencies: libfprint_dep,
           include_directories: [
             root_inc,
           ],
           c_args: common_cflags)
