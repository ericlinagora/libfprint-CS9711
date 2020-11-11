{ pkgs, ... }:

{
  # See full reference at https://devenv.sh/reference/options/

  # https://devenv.sh/languages/
  languages.python = {
    enable = true;
    venv = {
      enable = true;
      requirements = ''
        pygobject
        gobject-introspection
      '';
    };
  };

  # https://devenv.sh/packages/
  packages = [
    # (pkgs.python3.withPackages (python-pkgs: [
    #   python-pkgs.pygobject3
    # ]))

    pkgs.binutils
    pkgs.clang
    pkgs.cmake
    pkgs.glibc
    pkgs.gdb
    pkgs.valgrind

    pkgs.glib
    pkgs.gusb
    pkgs.gobject-introspection
    pkgs.pixman
    pkgs.nss
    pkgs.libgudev
    pkgs.gtk-doc
    pkgs.cairo
    pkgs.gtk3

    pkgs.libusb
    pkgs.libusb1

    pkgs.pkg-config
    pkgs.stdenv
    pkgs.meson
    pkgs.ninja
    # pkgs.python3
  ];

  # https://devenv.sh/basics/
  env.GREET = "devenv";

  # scripts.hello.exec = "echo hello from $GREET";

  enterShell = ''
  '';

  # https://devenv.sh/tests/
  enterTest = ''
  '';
}
