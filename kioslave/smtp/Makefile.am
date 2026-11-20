
INCLUDES= -I$(srcdir)/../.. -I$(srcdir)/.. $(SSL_INCLUDES) $(all_includes)

kde_module_LTLIBRARIES = kio_smtp.la

kio_smtp_la_SOURCES = smtp.cc request.cc response.cc capabilities.cc command.cc transactionstate.cc
kio_smtp_la_LIBADD  = $(LIB_KIO) $(SASL2_LIBS)
kio_smtp_la_LDFLAGS = $(all_libraries) -module $(KDE_PLUGIN)

noinst_HEADERS = smtp.h request.h response.h capabilities.h command.h transactionstate.h

kdelnk_DATA = smtp.protocol smtps.protocol
kdelnkdir = $(kde_servicesdir)

messages:
	$(XGETTEXT) *.cc -o $(podir)/kio_smtp.pot
