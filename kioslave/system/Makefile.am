SUBDIRS= . kdedmodule entries mimetypes

INCLUDES = $(all_includes)
METASOURCES = AUTO

kde_module_LTLIBRARIES = kio_system.la

kio_system_la_SOURCES = dummy.cpp
kio_system_la_LIBADD  = libkiosystem.la $(LIB_KIO)
kio_system_la_LDFLAGS = $(all_libraries) -module $(KDE_PLUGIN) -no-undefined

dummy.cpp:
	echo > dummy.cpp

kde_services_DATA = system.protocol

noinst_LTLIBRARIES = libkiosystem.la
libkiosystem_la_SOURCES = kio_system.cpp systemimpl.cpp

messages:
	$(XGETTEXT) `find . -name "*.cc" -o -name "*.cpp" -o -name "*.h"` -o $(podir)/kio_system.pot

