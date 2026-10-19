.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(CPPFLAGS)
DWLDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server wayland-client xkbcommon libinput
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

all: mywm mywmctl
SRC  = dwl.c nested.c keyboard-shortcuts-inhibit-unstable-v1-protocol.c
DEPS = $(SRC) client.h config.h ime.h ipc.h util.h config.mk cursor-shape-v1-protocol.h \
	ext-image-copy-capture-v1-protocol.h \
	keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h
mywm: $(DEPS)
	$(CC) $(SRC) $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

mywmctl: mywmctl.c
	$(CC) mywmctl.c $(DWLDEVCFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

# ASAN/UBSAN build for crash investigation, see DESIGN.md 11.1
DEBUGFLAGS = -O0 -fno-omit-frame-pointer -fsanitize=address,undefined
debug: mywm-debug
mywm-debug: $(DEPS)
	$(CC) $(SRC) $(DWLCFLAGS) $(DEBUGFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
ext-image-copy-capture-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml $@
keyboard-shortcuts-inhibit-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

clean:
	rm -f mywm mywmctl mywm-debug *.o *-protocol.h *-protocol.c

dist: clean
	mkdir -p mywm-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md client.h config.h \
		config.mk protocols mywm.1 dwl.c nested.c ime.h ipc.h mywmctl.c util.h mywm.desktop \
		mywm-portals.conf mywm-$(VERSION)
	tar -caf mywm-$(VERSION).tar.gz mywm-$(VERSION)
	rm -rf mywm-$(VERSION)

install: mywm mywmctl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/mywm
	cp -f mywm $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/mywm
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f mywm.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/mywm.1
	mkdir -p $(DESTDIR)$(SESSIONDIR)
	cp -f mywm.desktop $(DESTDIR)$(SESSIONDIR)/mywm.desktop
	chmod 644 $(DESTDIR)$(SESSIONDIR)/mywm.desktop
	mkdir -p $(DESTDIR)$(DATADIR)/xdg-desktop-portal
	cp -f mywm-portals.conf $(DESTDIR)$(DATADIR)/xdg-desktop-portal/mywm-portals.conf
	chmod 644 $(DESTDIR)$(DATADIR)/xdg-desktop-portal/mywm-portals.conf
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mywm $(DESTDIR)$(PREFIX)/bin/mywmctl \
		$(DESTDIR)$(MANDIR)/man1/mywm.1 $(DESTDIR)$(SESSIONDIR)/mywm.desktop \
		$(DESTDIR)$(DATADIR)/xdg-desktop-portal/mywm-portals.conf
