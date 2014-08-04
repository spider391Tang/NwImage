INCLUDES += -I$(abs_top_builddir)/src
INCLUDES += -I$(abs_top_srcdir)/src

ggivnc_SOURCES += src/bandwidth.c
ggivnc_SOURCES += src/d3des.c
ggivnc_SOURCES += src/d3des.h
ggivnc_SOURCES += src/resource.h
ggivnc_SOURCES += src/scrollbar.h
ggivnc_SOURCES += src/vnc.c
ggivnc_SOURCES += src/vnc.h
ggivnc_SOURCES += src/vnc-debug.h
ggivnc_SOURCES += src/vnc-endian.h

if HAVE_WIDGETS
ggivnc_SOURCES += src/about_widget.c
ggivnc_SOURCES += src/conn_widget.c
ggivnc_SOURCES += src/dialog.c
ggivnc_SOURCES += src/dialog.h
ggivnc_SOURCES += src/ggivnc-icon.h
ggivnc_SOURCES += src/menu_widget.c
ggivnc_SOURCES += src/pass_widget.c
ggivnc_SOURCES += src/scrollbar.c
ggivnc_LDADD += -lm
else
ggivnc_SOURCES += src/conn_none.c
ggivnc_SOURCES += src/pass_getpass.c
endif

MAINTAINERCLEANFILES += src/config.h.in

include src/encoding/Makefile.am
