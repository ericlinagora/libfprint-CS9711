FLAG_FILE=builddir/.makefile.flag

.PHONY: all
all: test
.PHONY: test
test: compile
	builddir/examples/img-capture
.PHONY: compile
compile: $(FLAG_FILE)
	@(cd builddir && meson compile)
$(FLAG_FILE): meson_options.txt meson.build libfprint/meson.build tests/meson.build examples/meson.build
	@(meson setup --wipe builddir && touch $@) || (rm -rf $@ && false)