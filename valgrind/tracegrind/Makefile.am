include $(top_srcdir)/Makefile.tool.am

noinst_PROGRAMS = 
if VGCONF_PLATFORMS_INCLUDE_X86_LINUX
noinst_PROGRAMS += tracegrind-x86-linux
endif
if VGCONF_PLATFORMS_INCLUDE_AMD64_LINUX
noinst_PROGRAMS += tracegrind-amd64-linux
endif
if VGCONF_PLATFORMS_INCLUDE_PPC32_LINUX
noinst_PROGRAMS += tracegrind-ppc32-linux
endif
if VGCONF_PLATFORMS_INCLUDE_PPC64_LINUX
noinst_PROGRAMS += tracegrind-ppc64-linux
endif
if VGCONF_PLATFORMS_INCLUDE_PPC32_AIX5
noinst_PROGRAMS += tracegrind-ppc32-aix5
endif
if VGCONF_PLATFORMS_INCLUDE_PPC64_AIX5
noinst_PROGRAMS += tracegrind-ppc64-aix5
endif

TRACEGRIND_SOURCES_COMMON = tg_main.c buffer.c copy.c
noinst_HEADERS = buffer.h copy.h

tracegrind_x86_linux_SOURCES      = $(TRACEGRIND_SOURCES_COMMON)
tracegrind_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
tracegrind_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX)
tracegrind_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
tracegrind_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
tracegrind_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

tracegrind_amd64_linux_SOURCES      = $(TRACEGRIND_SOURCES_COMMON)
tracegrind_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
tracegrind_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX)
tracegrind_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
tracegrind_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
tracegrind_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

tracegrind_ppc32_linux_SOURCES      = $(TRACEGRIND_SOURCES_COMMON)
tracegrind_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
tracegrind_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX)
tracegrind_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
tracegrind_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
tracegrind_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

tracegrind_ppc64_linux_SOURCES      = $(TRACEGRIND_SOURCES_COMMON)
tracegrind_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
tracegrind_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX)
tracegrind_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
tracegrind_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
tracegrind_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

tracegrind_ppc32_aix5_SOURCES      = $(TRACEGRIND_SOURCES_COMMON)
tracegrind_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
tracegrind_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5)
tracegrind_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
tracegrind_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
tracegrind_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

tracegrind_ppc64_aix5_SOURCES      = $(TRACEGRIND_SOURCES_COMMON)
tracegrind_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
tracegrind_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5)
tracegrind_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
tracegrind_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
tracegrind_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)