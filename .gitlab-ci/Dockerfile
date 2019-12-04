# Rebuild and push with
#
#  cd .gitlab-ci/
#  docker build --no-cache -t registry.freedesktop.org/libfprint/libfprint/master:v1 .
#  docker push registry.freedesktop.org/libfprint/libfprint/master:v1
#

FROM fedora:rawhide

RUN dnf -y update && dnf -y upgrade && \
  dnf -y install \
         doxygen \
         flatpak-builder \
         gcc \
         gcc-c++ \
         git \
         glib2-devel \
         glibc-devel \
         gobject-introspection-devel \
         gtk-doc \
         gtk3-devel \
         libabigail \
         libgusb-devel \
         libX11-devel \
         libXv-devel \
         meson \
         nss-devel \
         pixman-devel \
         python3-cairo \
         python3-gobject \
         systemd \
         umockdev \
         uncrustify \
         valgrind \
  && \
  dnf clean all
