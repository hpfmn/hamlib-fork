SATNOGSSRC = satnogs.c satnogs.h satnogs_gr.cc satnogs_cc.h
SATNOGS_LIBS = -lboost_system -lgnuradio-blocks -lgnuradio-runtime -lgnuradio-pmt -llog4cpp -lgnuradio-analog -lstdc++
AM_CFLAGS += $(SATNOGS_LIBS)

libhamlib_satnogs_la_LINK = $(CXXLINK) $(libhamlib_satnogs_la_LDFLAGS)

noinst_LTLIBRARIES = libhamlib-satnogs.la
libhamlib_satnogs_la_SOURCES = $(SATNOGSSRC)
libhamlib_satnogs_la_LIBADD = -lboost_system -lgnuradio-blocks -lgnuradio-runtime -lgnuradio-pmt -llog4cpp -lgnuradio-analog -lstdc++


EXTRA_DIST = Android.mk
