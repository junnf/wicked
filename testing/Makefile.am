
CLEANFILES			= *~
MAINTAINERCLEANFILES		= Makefile.in

noinst_PROGRAMS			= rtnl-test	\
				  hex-test	\
				  uuid-test	\
				  xml-test	\
				  ibft-test	\
				  xpath-test	\
				  cstate-test

AM_CPPFLAGS			= -I$(top_srcdir)/src	\
				  -I$(top_srcdir)/include

AM_CFLAGS			= $(LIBNL_CFLAGS)	\
				  $(LIBDBUS_CFLAGS)	\
				  $(LIBGCRYPT_CFLAGS)

AM_LDFLAGS			= -rdynamic
LDADD				= $(top_builddir)/src/libwicked.la

rtnl_test_SOURCES		= rtnl-test.c
hex_test_SOURCES		= hex-test.c
uuid_test_SOURCES		= uuid-test.c
xml_test_SOURCES		= xml-test.c
ibft_test_SOURCES		= ibft-test.c
xpath_test_SOURCES		= xpath-test.c
cstate_test_SOURCES		= cstate-test.c

EXTRA_DIST			= ibft xpath

# vim: ai
