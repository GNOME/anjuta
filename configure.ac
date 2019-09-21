dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.65)

m4_define(anjuta_major_version,  3)
m4_define(anjuta_minor_version,  34)
m4_define(anjuta_micro_version,  0)
m4_define(anjuta_version, anjuta_major_version.anjuta_minor_version.anjuta_micro_version)
m4_define(bugzilla_version, anjuta_major_version.anjuta_minor_version.anjuta_micro_version)

AC_INIT([Anjuta],[anjuta_version],[http://bugzilla.gnome.org/enter_bug.cgi?product=anjuta],[anjuta],[http://www.anjuta.org/])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_MACRO_DIR([m4])


AC_DEFINE(ANJUTA_MAJOR_VERSION, anjuta_major_version, [Anjuta major version])
AC_SUBST(ANJUTA_MAJOR_VERSION, anjuta_major_version)
AC_DEFINE(ANJUTA_MINOR_VERSION, anjuta_minor_version, [Anjuta minor version])
AC_SUBST(ANJUTA_MINOR_VERSION, anjuta_minor_version)
AC_DEFINE(ANJUTA_MICRO_VERSION, anjuta_micro_version, [Anjuta micro version])
AC_SUBST(ANJUTA_MICRO_VERSION, anjuta_micro_version)
AC_DEFINE(ANJUTA_VERSION, anjuta_version, [Anjuta version])
AC_SUBST(ANJUTA_VERSION)

ANJUTA_VERSION=anjuta_version
AC_SUBST(ANJUTA_VERSION)

BUGZILLA_VERSION=bugzilla_version
AC_SUBST(BUGZILLA_VERSION)

dnl Anjuta core
GLIB_REQUIRED=2.34.0
GTK_REQUIRED=3.10.0
GTHREAD_REQUIRED=2.22.0
GDK_PIXBUF_REQUIRED=2.0.0
GDA4_REQUIRED=4.2.0
GDA5_REQUIRED=5.0.0
GDA6_REQUIRED=5.90.0
LIBXML_REQUIRED=2.4.23
GDL_REQUIRED=3.5.5
LIBWNCK_REQUIRED=2.12

dnl GtkSourceView
GTKSOURCEVIEW_REQUIRED=3.0.0

dnl Terminal
VTE_REQUIRED=0.27.6

dnl Devhelp
LIBDEVHELP_REQUIRED=3.7.4

dnl Glade
GLADEUI_REQUIRED=3.12.0

dnl Introspection
GI_REQUIRED=0.9.5

dnl Subversion plugin
NEON_REQUIRED=0.28.2
SERF_REQUIRED=1.2.0
SVN_MAJOR=1
SVN_MINOR=5
SVN_PATCH=0
SUBVERSION_REQUIRED=$SVN_MAJOR.$SVN_MINOR.$SVN_PATCH

AM_INIT_AUTOMAKE([1.10 dist-xz no-dist-gzip tar-ustar])
AM_MAINTAINER_MODE([enable])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Check for C Compiler
AC_PROG_CC
AC_PROG_CPP
AC_PROG_LEX
if test "$LEX" != "flex"; then
	AC_MSG_ERROR(flex is required)
fi
AC_PROG_YACC
case "$YACC" in
  'bison '*)
    ;;
  *)
    AC_MSG_ERROR(bison is required)
    ;;
esac
AC_LANG([C])
AC_LANG([C++])
AC_PROG_CXX
AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])],,
   AC_MSG_ERROR([C++ Compiler required to compile Anjuta]))
AM_PROG_CC_C_O

GNOME_COMPILE_WARNINGS([yes])
AM_CFLAGS="$AM_CFLAGS $WARN_CFLAGS"
AM_CXXFLAGS="$AM_CXXFLAGS $WARN_CXXFLAGS"

dnl Enable debugging mode
AC_ARG_ENABLE(debug,
  AS_HELP_STRING([--enable-debug],[Enable debug messages]),
  [if test "x$enableval" = "xyes"; then
      AM_CFLAGS="$AM_CFLAGS -DDEBUG"
  fi]
  AM_CXXFLAGS="$AM_CXXFLAGS -DDEBUG")
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CXXFLAGS)

ANJUTA_LDFLAGS="-no-undefined"
ANJUTA_PLUGIN_LDFLAGS="-module -avoid-version -no-undefined"
AC_SUBST(ANJUTA_LDFLAGS)
AC_SUBST(ANJUTA_PLUGIN_LDFLAGS)

dnl GSettings
GLIB_GSETTINGS

dnl Disable deprecated APIs
dnl if test x$MAINT = x; then
dnl 	DEPRECATED_FLAGS="-DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED"
dnl	AC_SUBST(DEPRECATED_FLAGS)
dnl fi

dnl Check for pkg-config
PKG_PROG_PKG_CONFIG([0.22])

dnl Enable versioned user preferences directory
AC_ARG_WITH(pref-suffix,
  AS_HELP_STRING([--with-pref-suffix=VALUE],[Suffix to add to user preferences dir.]),
  PREF_SUFFIX="$withval")

AC_SUBST(PREF_SUFFIX)
AC_DEFINE_UNQUOTED(PREF_SUFFIX,
      "${PREF_SUFFIX}",
	  [Suffix to add to preferences directory])

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

dnl ***************************************************************************
dnl Check for GObject-Introspection
dnl ***************************************************************************
m4_ifdef([GOBJECT_INTROSPECTION_CHECK], [GOBJECT_INTROSPECTION_CHECK([0.6.7])],
[AM_CONDITIONAL(HAVE_INTROSPECTION, false)])

dnl ***************************************************************************
dnl Set gjsdir
dnl ***************************************************************************
gjsdir=`pkg-config --variable=jsdir gjs-1.0`
AC_SUBST(gjsdir)

dnl Check base modules

PKG_CHECK_MODULES([ANJUTA],
   [gthread-2.0 >= $GTHREAD_REQUIRED
	glib-2.0 >= $GLIB_REQUIRED
	gio-2.0 >= $GLIB_REQUIRED
	gmodule-2.0 >= $GLIB_REQUIRED
	gtk+-3.0 >= $GTK_REQUIRED
	gdk-pixbuf-2.0 >= $GDK_PIXBUF_REQUIRED])

PKG_CHECK_MODULES([XML],
   [libxml-2.0 >= $LIBXML_REQUIRED])

PKG_CHECK_MODULES([GDL],
   [gdl-3.0 >= $GDL_REQUIRED])

PKG_CHECK_MODULES([GDA],
   [libgda-6.0 >= $GDA6_REQUIRED], [
		AC_DEFINE(HAVE_GDA6,[1],[libgda-6.0 support])
	],
   [PKG_CHECK_MODULES([GDA],
      [libgda-5.0 >= $GDA5_REQUIRED],,
         [PKG_CHECK_MODULES([GDA],
            [libgda-4.0 >= $GDA4_REQUIRED])])])

dnl Check for autogen
dnl -----------------
AC_PATH_PROG(AUTOGEN_PATH, autogen,no)
if test x$AUTOGEN_PATH = xno; then
   AC_MSG_WARN([Couldn't find autogen. You will be able to build anjuta without autogen \
                but several things won't work. You can get it from http://autogen.sourceforge.net/])
fi

dnl Check for Terminal
dnl ---------------------

AC_ARG_ENABLE(plugin-terminal,
  AS_HELP_STRING([--disable-plugin-terminal],[Disable terminal plugin support in Anjuta.]),
  [ if test "$enableval" = "no"; then
       user_disabled_terminal=1
    fi ],
  [ user_disabled_terminal=0 ] )

AC_MSG_CHECKING(if terminal plugin is disabled)
if test "$user_disabled_terminal" = 1; then
	AC_MSG_RESULT(yes)
	terminal_enabled="no"
else
	AC_MSG_RESULT(no)
	PKG_CHECK_EXISTS([vte-2.91 >= $VTE_REQUIRED],
		 [vte_2_91_enabled=yes],
		 [vte_2_91_enabled=no])
	if test $vte_2_91_enabled = yes; then
		PKG_CHECK_MODULES(PLUGIN_TERMINAL, [vte-2.91 >= $VTE_REQUIRED],
	    		[terminal_enabled=yes],
			[terminal_enabled=no])
		AC_DEFINE(HAVE_VTE_2_91,1,[libvte 2.91 support])
	else
		PKG_CHECK_MODULES(PLUGIN_TERMINAL, [vte-2.90 >= $VTE_REQUIRED],
	    		[terminal_enabled=yes],
			[terminal_enabled=no])
		AC_DEFINE(HAVE_VTE_2_90,1,[libvte 2.90 support])
	fi
fi

AM_CONDITIONAL(HAVE_PLUGIN_TERMINAL, [test x$terminal_enabled = xyes])

dnl Check for Devhelp
dnl -----------------

AC_ARG_ENABLE(plugin-devhelp,
  AS_HELP_STRING([--disable-plugin-devhelp],[Disable devhelp plugin support in Anjuta.]),
  [ if test "$enableval" = "no"; then
       user_disabled_devhelp=1
    fi ],
  [ user_disabled_devhelp=0 ] )

AC_MSG_CHECKING(if devhelp plugin is disabled)
if test "$user_disabled_devhelp" = 1; then
	AC_MSG_RESULT(yes)
	devhelp_enabled="no"
else
	AC_MSG_RESULT(no)
	PKG_CHECK_MODULES(PLUGIN_DEVHELP,
				[libdevhelp-3.0 >= $LIBDEVHELP_REQUIRED],
				[
				    devhelp_enabled=yes
				], [
				    devhelp_enabled=no
				])
fi

if test "x$devhelp_enabled" = "xyes"; then
	case "$PLUGIN_DEVHELP_LIBS" in
		*webkit2gtk-3.0*)
			AC_DEFINE([HAVE_WEBKIT2], [1], [Defined if devhelp is built against webkitgtk2])
			;;
		*webkit2gtk-4.0*)
			AC_DEFINE([HAVE_WEBKIT2], [1], [Defined if devhelp is built against webkitgtk2])
			;;
		esac
fi
AM_CONDITIONAL(HAVE_PLUGIN_DEVHELP, [test x$devhelp_enabled = xyes])

dnl Check for Glade3
dnl ---------------------

AC_ARG_ENABLE(plugin-glade,
  AS_HELP_STRING([--disable-plugin-glade],[Disable glade plugin support in Anjuta.]),
  [ if test "$enableval" = "no"; then
       user_disabled_glade=1
    fi ],
  [ user_disabled_glade=0 ] )

AC_MSG_CHECKING(if glade plugin is disabled)
if test "$user_disabled_glade" = 1; then
	AC_MSG_RESULT(yes)
	gladeui_found="no"
else
	AC_MSG_RESULT(no)
	gladeui_version=0
	PKG_CHECK_MODULES(PLUGIN_GLADE, gladeui-2.0 >= $GLADEUI_REQUIRED,
	    [
		gladeui_found=yes
	    ], [
		gladeui_found=no
	    ])
fi

AM_CONDITIONAL(HAVE_PLUGIN_GLADE, [test x$gladeui_found = xyes])

dnl Glade catalog
dnl ---------------------

AC_ARG_ENABLE(glade-catalog,
  AS_HELP_STRING([--enable-glade-catalog], [Enable glade catalog. Glade UI libraries must be installed]),
  [ user_enabled_glade_catalog=1 ],
  [ user_enabled_glade_catalog=0 ])

glade_catalog_enabled="no"

AC_MSG_CHECKING(if glade catalog is enabled)
if test "$gladeui_found" = "yes"; then
  if test "$user_enabled_glade_catalog" = 1; then
    AC_MSG_RESULT(yes)
    glade_catalog_enabled="yes"
    GLADE_CATALOG_PATH=`pkg-config --variable=catalogdir gladeui-2.0`
    GLADE_PLUGINS_PATH=`pkg-config --variable=moduledir gladeui-2.0`

    AC_SUBST(GLADE_CATALOG_PATH)
    AC_SUBST(GLADE_PLUGINS_PATH)
  else
    AC_MSG_RESULT(no) 
  fi
else
  AC_MSG_RESULT(no)
fi

AM_CONDITIONAL(ENABLE_GLADE_CATALOG, [test x$glade_catalog_enabled = xyes])

dnl Check for gtksourceview
dnl -------------------------------------------------------------
PKG_CHECK_MODULES(SOURCEVIEW,
		[gtksourceview-3.0 >= $GTKSOURCEVIEW_REQUIRED])


dnl Check for libsqlite3 used in symbol-db benchmarks
dnl -------------------------------------------------------------
PKG_CHECK_MODULES(SQLITE, sqlite3, sqlite3_found=yes, sqlite3_found=no)
AM_CONDITIONAL(HAVE_SQLITE, [test x$sqlite3_found = xyes])


dnl Check for vala
dnl -------------------------------------------------------------

AC_ARG_ENABLE(vala,
	AS_HELP_STRING([--disable-vala],[Disable vala support]),
	[ if test "$enableval" = "no"; then
		user_disabled_vala=1
	  fi ],
	[ user_disabled_vala=0 ])

AC_MSG_CHECKING(if vala support is disabled)
if test "$user_disabled_vala" = 1; then
        AC_MSG_RESULT(yes)
	enable_vala="no"
else
        AC_MSG_RESULT(no)
	AC_MSG_CHECKING(if libvala is available)
	m4_foreach([VERSION], [[0.44], [0.42], [0.40], [0.38], [0.36], [0.34], [0.32], [0.30], [0.28], [0.26], [0.24], [0.22], [0.20], [0.18]],
		[PKG_CHECK_EXISTS([ libvala-VERSION ],
			 [ valaver="-VERSION" ])
			 ])
	if test "x$valaver" != "x"; then
		AC_MSG_RESULT(yes)
		enable_vala="yes"
		LIBVALA="libvala${valaver}"
		PKG_CHECK_MODULES([VALA], ${LIBVALA})
	else
		AC_MSG_RESULT(no)
		enable_vala="no"
		VALA_REQUIRED=0.18.0
	fi
	AC_SUBST(LIBVALA)
fi

if test x$enable_vala = xyes; then
	AC_PATH_PROGS(VALAC, [valac${valaver} valac], valac)
	AC_SUBST(VALAC)
fi

AM_CONDITIONAL(ENABLE_VALA, [test x$enable_vala = xyes])

dnl Check for Python
dnl -------------------------------------------------------------

AM_PATH_PYTHON([],
	[have_python="yes"],
	[have_python="no"])

if test x$have_python = xyes; then
	if test -x $PYTHON-config; then
		PYTHON_CFLAGS=`$PYTHON-config --cflags`
		PYTHON_LIBS=`$PYTHON-config --libs`
		AC_SUBST(PYTHON_CFLAGS)
		AC_SUBST(PYTHON_LIBS)
	elif test -x $PYTHON$PYTHON_VERSION-config; then
		PYTHON_CFLAGS=`$PYTHON$PYTHON_VERSION-config --cflags`
		PYTHON_LIBS=`$PYTHON$PYTHON_VERSION-config --libs`
		AC_SUBST(PYTHON_CFLAGS)
		AC_SUBST(PYTHON_LIBS)		
	else
		have_python="no"
		echo "$PYTHON-config or $PYTHON$PYTHON_VERSION-config not found - disabling python"
	fi
fi

AM_CONDITIONAL(HAVE_PYTHON, [test x$have_python = xyes])

dnl Disable packagekit support
dnl -----------------------------------
AC_ARG_ENABLE(packagekit,
  [AC_HELP_STRING([--disable-packagekit],[build without PackageKit support])],,
  [enable_packagekit=yes])
if test "x$enable_packagekit" != "xno"; then
  AC_DEFINE(ENABLE_PACKAGEKIT, 1, [define to enable PackageKit installer])
fi

dnl Setup Anjuta Library flags
dnl --------------------------
LIBANJUTA_CFLAGS='$(ANJUTA_CFLAGS) $(GDL_CFLAGS) $(DEPRECATED_FLAGS) -I$(top_srcdir) -I$(top_builddir)/libanjuta -DPACKAGE_PIXMAPS_DIR=\""$(datadir)/pixmaps/$(PACKAGE)"\" -DPACKAGE_LIB_DIR=\""$(pkglibdir)"\" -DPACKAGE_DATA_DIR=\""$(datadir)/$(PACKAGE)"\"'
LIBANJUTA_LIBS='$(top_builddir)/libanjuta/libanjuta-3.la'
AC_SUBST(LIBANJUTA_CFLAGS)
AC_SUBST(LIBANJUTA_LIBS)

dnl Setup FooCanvas Library flags
dnl --------------------------
LIBFOOCANVAS_CFLAGS='-I$(top_srcdir)'
LIBFOOCANVAS_LIBS='$(top_builddir)/libfoocanvas/libanjuta-foocanvas.la'
AC_SUBST(LIBFOOCANVAS_CFLAGS)
AC_SUBST(LIBFOOCANVAS_LIBS)

dnl Checks for Xft/XRender for foocanvas
dnl
AC_CHECK_LIB(Xrender, XRenderFindFormat,
	[AC_SUBST(RENDER_LIBS, "-lXrender -lXext")
	 AC_DEFINE(HAVE_RENDER, 1, [Define if libXrender is available.])],
	[AC_SUBST(RENDER_LIBS, "")],
	[-lXext])

dnl Setup Plugin directories
dnl ------------------------
anjuta_plugin_dir='$(libdir)/anjuta'
anjuta_data_dir='$(datadir)/anjuta'
anjuta_ui_dir='$(datadir)/anjuta/ui'
anjuta_glade_dir='$(datadir)/anjuta/glade'
anjuta_image_dir='$(datadir)/pixmaps/anjuta'
AC_SUBST(anjuta_plugin_dir)
AC_SUBST(anjuta_data_dir)
AC_SUBST(anjuta_ui_dir)
AC_SUBST(anjuta_glade_dir)
AC_SUBST(anjuta_image_dir)

dnl ***************************************************************************
dnl Check for marshal and enum generators
dnl ***************************************************************************
GLIB_GENMARSHAL="`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`"
AC_SUBST(GLIB_GENMARSHAL)
GLIB_MKENUMS="`$PKG_CONFIG --variable=glib_mkenums glib-2.0`"
AC_SUBST(GLIB_MKENUMS)

dnl ***************************************************************************
dnl Internatinalization
dnl ***************************************************************************
IT_PROG_INTLTOOL([0.40.1])

AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.17])

GETTEXT_PACKAGE=anjuta
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])

BISON_I18N

dnl ***************************************************************************
dnl Check for gtk-doc.
dnl ***************************************************************************
GTK_DOC_CHECK([1.4])

dnl ***************************************************************************
dnl User manual
dnl ***************************************************************************
YELP_HELP_INIT

dnl Define convenience macros
dnl -------------------------
dnl CHECK_HEADER_DEFINE(LABEL, HEADER [,ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND ] ])
AC_DEFUN([CHECK_HEADER_DEFINE],
[
	AC_MSG_CHECKING("if $1 is defined in $2")
	AC_EGREP_CPP(yes,
[#include <$2>
#ifdef $1
  yes
#endif
], [
	AC_MSG_RESULT(yes)
	$3
], [
	AC_MSG_RESULT(no)
	$4
]) ])

dnl Checks for header files.
AC_CHECK_HEADERS(dirent.h fcntl.h fnmatch.h stat.h stdlib.h string.h stdint.h)
AC_CHECK_HEADERS(time.h types.h unistd.h)
AC_CHECK_HEADERS(sys/dir.h sys/stat.h sys/times.h sys/types.h)

dnl Checks for typedefs, structures, and compiler characteristics.

AC_CANONICAL_HOST
CYGWIN=no
MINGW32=no
MACOSX=no
case $host_os in
  *cygwin* ) CYGWIN=yes;;
  *mingw32* ) MINGW32=yes;;
  *darwin* ) MACOSX=yes;;
esac


dnl Check for bind in libsocket (needed on Solaris)
AC_CHECK_LIB(socket, bind)

dnl Check for function forkpty and related headers
AC_SEARCH_LIBS(forkpty, util, , AC_DEFINE(EMULATE_FORKPTY, [], [Need  to implement our own forkpty()]))
AC_CHECK_HEADERS(libutil.h util.h pty.h)

dnl Checks for library functions.
AC_CHECK_FUNCS(fnmatch)
AC_CHECK_FUNCS(strstr)
AC_CHECK_FUNCS(stricmp)
AC_CHECK_FUNCS(strnicmp)
AC_CHECK_FUNCS(getline)

dnl for libegg/egg-recent-model.c
AC_CHECK_DECLS([lockf])
AC_CHECK_FUNCS([lockf])

AC_CHECK_FUNCS(fgetpos, have_fgetpos=yes)

dnl SEEK_SET should be in stdio.h, but may be in unistd.h on SunOS 4.1.x
if test "$have_fgetpos" != yes ; then
    CHECK_HEADER_DEFINE(SEEK_SET, stdio.h,,
        CHECK_HEADER_DEFINE(SEEK_SET, unistd.h,,
            AC_DEFINE(SEEK_SET, 0, [Seek set 0])))
fi

AC_CHECK_FUNCS(mkstemp, have_mkstemp=yes)
if test "$have_mkstemp" != yes ; then
    AC_CHECK_FUNCS(chmod)
    if test "$tmpdir_specified" = yes ; then
        AC_MSG_RESULT(use of tmpnam overrides temporary directory selection)
    fi
else
    AC_MSG_CHECKING(directory to use for temporary files)
    if test -z "$enable_tmpdir" -o "$enable_tmpdir" = no -o "$enable_tmpdir" = yes ;
        then tmpdir=/tmp
        else tmpdir="$enable_tmpdir"
    fi
    if test -d $tmpdir ; then
        AC_MSG_RESULT($tmpdir)
        AC_DEFINE_UNQUOTED(TMPDIR, "$tmpdir", [Directory for temporary files])
    else
        AC_MSG_WARN($tmpdir does not exist)
    fi
fi

dnl -----------------------------
dnl Checks for FreeBSD Build
dnl -----------------------------
AC_MSG_CHECKING(if building on FreeBSD)

if test `uname -s` = "FreeBSD" ; then
        AC_MSG_RESULT(yes)
        FREEBSD_BUILD=-DFREEBSD
        AC_SUBST(FREEBSD_BUILD)
	AC_PATH_PROG(GMAKE, gmake, no)
	if test "x$GMAKE" = "xno"; then
		AC_MSG_ERROR(You need gmake installed to build Anjuta!)
	fi
else
        AC_MSG_RESULT(no)
	GMAKE="make"
fi

AC_SUBST(GMAKE)

dnl Checks for missing prototypes
dnl -----------------------------
AC_MSG_NOTICE([checking for missing prototypes...])

AC_DEFUN([CHECK_PROTO], [AC_EGREP_HEADER([[^A-Za-z0-9_]$1([   ]+[A-Za-z0-9_]*)?\(], $2,, AC_DEFINE(patsubst([NEED_PROTO_NAME], [NAME], translit($1, [a-z], [A-Z])),,[Do not know]) AC_MSG_RESULT(adding prototype for $1))])

if test "$have_remove" = yes ; then
    CHECK_PROTO(remove, stdio.h)
else
    CHECK_PROTO(unlink, unistd.h)
fi
CHECK_PROTO(malloc, stdlib.h)
CHECK_PROTO(getenv, stdlib.h)
CHECK_PROTO(stat,   sys/stat.h)
CHECK_PROTO(lstat,  sys/stat.h)
if test "$have_fgetpos" = yes ; then
    CHECK_PROTO(fgetpos, stdio.h)
fi
if test "$have_truncate" = yes ; then
    CHECK_PROTO(truncate, unistd.h)
fi
if test "$have_ftruncate" = yes ; then
    CHECK_PROTO(ftruncate, unistd.h)
fi

dnl  **********************************************************
dnl  check if we have svn libraries to build subversion plugin
dnl  (stolen from kdevelop ;-)
dnl  **********************************************************

AC_MSG_CHECKING(for Subversion svn-config)
AC_ARG_WITH(subversion-dir,
		AS_HELP_STRING([--with-subversion-dir=DIR],[Where Subversion is installed ]),
		[
				SVNCONFIG="$withval/bin/svn-config"
		])

if test -z "$SVNCONFIG"; then
		_SVNCONFIG="`svn-config --prefix 2> /dev/null`"
		if test -n "$_SVNCONFIG"; then
				SVNCONFIG="$_SVNCONFIG/bin/svn-config"
		fi
fi

AC_SUBST(SVNCONFIG)
if test -x "$SVNCONFIG"; then
		SVNLD="`$SVNCONFIG --ldflags 2> /dev/null`"
		SVN_LIB="`$SVNCONFIG --libs --cflags 2> /dev/null` -lsvn_client-1 -lsvn_subr-1"
		dnl ugly hack for subversion svn-config problems in 0.14.x, to be removed when svn-config is fixed
		SVN_INCLUDE="`$SVNCONFIG --includes 2> /dev/null` -I$_SVNCONFIG/include/subversion-1/"
		AC_MSG_RESULT(yes)
else
		AC_MSG_RESULT(not found)

		dnl just a fallback to debian's config so that it works for me :)
		AC_ARG_WITH(svn-include,
								[[  --with-svn-include=DIR   Use the given path to the subversion headers.]],
								[
								if test "$withval" != "yes" -a "$withval" != ""; then
										SVN_INCLUDES=$withval
								fi
								])
		if test -z "$SVN_INCLUDES"; then
			SVN_INCLUDES="/usr/local/include /usr/include"
		fi
		AC_MSG_CHECKING([for Subversion headers])
		SVN_INCLUDE=""
		for VALUE in $SVN_INCLUDES ; do
				if test -f $VALUE/subversion-1/svn_types.h ; then
						SVN_INCLUDE=$VALUE/subversion-1
						break
				fi
				if test -f $VALUE/svn_types.h ; then
						SVN_INCLUDE=$VALUE
						break
				fi
		done
		if test $SVN_INCLUDE ; then
				AC_MSG_RESULT([found])
		else
				AC_MSG_RESULT([not found])
		fi
		SVN_LIBS="/usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib"
		AC_ARG_WITH(svn-lib,
						[[  --with-svn-lib=DIR  Use the given path to the subversion libraries.]],
						[
							if test "$withval" != "yes" -a "$withval" != ""; then
								SVN_LIBS=$withval
							fi
						])
		AC_MSG_CHECKING([for Subversion libraries])
		SVN_LIB=""
		for VALUE in $SVN_LIBS ; do
				if ls $VALUE/libsvn_client-1.* 1>/dev/null 2>&1; then
						SVN_LIB=$VALUE
						break
				fi
		done
		if test $SVN_LIB ; then
				AC_MSG_RESULT([found])
		else
				AC_MSG_RESULT([not found])
		fi
fi

dnl ******************************************************************
dnl Check for extra libs required by subversion.
dnl FIXME: This should actually be done by subversion and not by us.
dnl        See http://subversion.tigris.org/issues/show_bug.cgi?id=4435
dnl ******************************************************************

AC_ARG_ENABLE(plugin-subversion,
  AS_HELP_STRING([--disable-plugin-subversion],[Disable subversion support in Anjuta.]),
  [ if test "$enableval" = "no"; then
        user_disabled_subversion=1
    fi ],
  [ user_disabled_subversion=0 ] )

AC_ARG_ENABLE(neon,
	AS_HELP_STRING([--disable-neon],[Disable neon support in Subversion plugin]),
	[ if test "$enableval" = "no"; then
		user_disabled_neon=1
	  fi ],
	[ user_disabled_neon=0 ])

AC_ARG_ENABLE(serf,
	AS_HELP_STRING([--disable-serf],[Disable serf support in Subversion plugin]),
	[ if test "$enableval" = "no"; then
		user_disabled_serf=1
	  fi ],
	[ user_disabled_serf=0 ])

AC_MSG_CHECKING(if subversion support is disabled)
if test "$user_disabled_subversion" = 1; then
	AC_MSG_RESULT(yes)
	SVN_INCLUDE=""
	SVN_LIB=""
else
	AC_MSG_RESULT(no)
fi

if test -n "$SVN_INCLUDE" ; then
	dnl ------------------------------------
	dnl APR. Required by subversion (devel)
	dnl ------------------------------------
	have_atr=no
	PKG_CHECK_MODULES(APR,                            \
		apr-1, have_apr=yes, have_apr=no)
	if test x"$have_apr" = "xno"; then
		SVN_INCLUDE=""
	fi

	dnl -----------------------------------------
	dnl APR util. Required by subversion (devel)
	dnl------------------------------------------
	have_atr_util=no
	PKG_CHECK_MODULES(APR_UTILS,                            \
		apr-util-1, have_apr_util=yes, have_apr_util=no)
	if test x"$have_apr_util" = "xno"; then
		SVN_INCLUDE=""
	fi

	dnl ----------------------------------------------------------
	dnl NEON/SERF. Required by subversion (devel) for HTTP support
	dnl-----------------------------------------------------------
	# Subversion <= 1.7 relies on neon (recommended) or serf.
	# Subversion 1.8 dropped neon support and uses serf to talk
	# to HTTP servers.
	#
	# Subversion can be built without HTTP client support so neon and
	# serf aren't hard requirements. But if present on the system
	# it is reasonable to assume that Subversion is linked to them.
	if test "$user_disabled_neon" = 0; then
		have_neon=no
		PKG_CHECK_MODULES(NEON,                            \
			neon, have_neon=yes, have_neon=no)
	fi
	if test "$user_disabled_serf" = 0; then
		have_serf=no
		PKG_CHECK_MODULES(SERF,                            \
			serf-1, have_serf=yes, have_serf=no)
	fi

	dnl Make sure our SVN version is high enough
	dnl Eww, eww, eww, this is dirty
	AC_MSG_CHECKING([for subversion >= $SUBVERSION_REQUIRED])
	AC_LANG([C])
	dnl Temporarily set CFLAGS so that include dirs are found
	CFLAGS_TMP=$CFLAGS
	CFLAGS="-I$SVN_INCLUDE $APR_CFLAGS"
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([[#include <svn_version.h>]],
			[[
				#if (SVN_VER_MAJOR < $SVN_MAJOR) || (SVN_VER_MINOR < $SVN_MINOR) || (SVN_VER_PATCH < $SVN_PATCH)
				#	error "Version too low"
				#endif
			]])], AC_MSG_RESULT(yes), have_svn_version=no)
	if test x"$have_svn_version" = "xno"; then
		AC_MSG_RESULT(no)
		SVN_INCLUDE=""
	fi
	CFLAGS=$CFLAGS_TMP
fi

dnl ------------------------------------------
dnl Finally prepare subversion build flags
dnl ------------------------------------------

if test -n "$SVN_INCLUDE" ; then
	SVN_INCLUDE="-I$SVN_INCLUDE"
	if test x != "x$SVN_LIB" ; then
	    SVN_LIB="-L$SVN_LIB $APR_LIBS $APR_UTILS_LIBS $NEON_LIBS $SERF_LIBS -lsvn_client-1 -lsvn_subr-1"
	else
	    SVN_LIB="$APR_LIBS $APR_UTILS_LIBS $NEON_LIBS $SERF_LIBS -lsvn_client-1 -lsvn_subr-1"
	fi
	SVN_CFLAGS="$APR_CFLAGS $APR_UTILS_CFLAGS $NEON_CFLAGS $SERF_CFLAGS"
fi

AM_CONDITIONAL(BUILD_SVN, [test -n "$SVN_INCLUDE"])
AC_SUBST(SVN_INCLUDE)
AC_SUBST(SVN_LIB)
AC_SUBST(SVN_CFLAGS)

dnl ------------------------------------------
dnl Symbol-db shared memory checks
dnl ------------------------------------------

SYMBOL_DB_SHM="/dev/shm"
user_specified_shm=0
AC_MSG_CHECKING(for symbol-db shared memory)
AC_ARG_WITH(symbol-db-shm,
		AS_HELP_STRING([--with-symbol-db-shm=DIR],[Where shared memory is placed ]),
		[
				user_specified_shm=1
				SYMBOL_DB_SHM="$withval"
		])
if test "$user_specified_shm" = 1; then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

AC_SUBST(SYMBOL_DB_SHM)

dnl On Linux, need librt for shm_open/shm_unlink
bck_LIBS="$LIBS"
AC_SEARCH_LIBS(shm_open, rt, [SHM_LIBS="$LIBS"], [AC_MSG_ERROR([Failed to find library with shm_open()])])
AC_SUBST(SHM_LIBS)
LIBS="$bck_LIBS"

dnl Test using autotest
dnl -----------------------------
AC_CONFIG_TESTDIR(plugins/am-project/tests)
AM_MISSING_PROG([AUTOM4TE], [autom4te])

AC_CONFIG_FILES([
Makefile
po/Makefile.in
libanjuta/Makefile
libanjuta/tests/Makefile
libanjuta/interfaces/Makefile
libanjuta/libanjuta-3.0.pc
libanjuta/anjuta-version.h
src/Makefile
pixmaps/Makefile
pixmaps/hicolor/Makefile
pixmaps/hicolor/16x16/Makefile
pixmaps/hicolor/22x22/Makefile
pixmaps/hicolor/24x24/Makefile
pixmaps/hicolor/32x32/Makefile
pixmaps/hicolor/48x48/Makefile
pixmaps/hicolor/scalable/Makefile
pixmaps/hicolor/symbolic/Makefile
data/Makefile
data/welcome.txt
data/templates/Makefile
doc/anjuta.1
doc/anjuta-launcher.1
doc/Makefile
scripts/Makefile
launcher/Makefile
plugins/Makefile
plugins/class-gen/Makefile
plugins/class-gen/templates/Makefile
plugins/patch/Makefile
plugins/document-manager/Makefile
plugins/document-manager/images/Makefile
plugins/message-view/Makefile
plugins/gdb/Makefile
plugins/gdb/images/Makefile
plugins/debug-manager/Makefile
plugins/debug-manager/images/Makefile
plugins/devhelp/Makefile
plugins/devhelp/images/Makefile
plugins/glade/Makefile
plugins/glade/images/Makefile
plugins/js-debugger/Makefile
plugins/file-manager/Makefile
plugins/file-loader/Makefile
plugins/file-wizard/Makefile
plugins/build-basic-autotools/Makefile
plugins/project-manager/Makefile
plugins/symbol-db/benchmark/Makefile
plugins/symbol-db/benchmark/symbol-db/Makefile
plugins/symbol-db/benchmark/libgda/Makefile
plugins/symbol-db/benchmark/sqlite/Makefile
plugins/symbol-db/images/Makefile
plugins/symbol-db/Makefile
plugins/symbol-db/anjuta-tags/Makefile
plugins/project-wizard/Makefile
plugins/snippets-manager/Makefile
plugins/cvs-plugin/Makefile
plugins/subversion/Makefile
plugins/git/Makefile
plugins/git/images/Makefile
plugins/git/tests/Makefile
plugins/sourceview/Makefile
plugins/terminal/Makefile
plugins/tools/Makefile
plugins/tools/scripts/Makefile
plugins/language-manager/Makefile
plugins/project-import/Makefile
plugins/project-wizard/templates/Makefile
plugins/project-wizard/templates/mkfile/Makefile
plugins/project-wizard/templates/mkfile/src/Makefile
plugins/project-wizard/templates/mkfile/po/Makefile
plugins/project-wizard/templates/minimal/Makefile
plugins/project-wizard/templates/js_minimal/Makefile
plugins/project-wizard/templates/js_minimal/src/Makefile
plugins/project-wizard/templates/terminal/Makefile
plugins/project-wizard/templates/terminal/src/Makefile
plugins/project-wizard/templates/terminal/po/Makefile
plugins/project-wizard/templates/cpp/Makefile
plugins/project-wizard/templates/cpp/src/Makefile
plugins/project-wizard/templates/cpp/po/Makefile
plugins/project-wizard/templates/gtk/Makefile
plugins/project-wizard/templates/gtk/src/Makefile
plugins/project-wizard/templates/gtk/po/Makefile
plugins/project-wizard/templates/gtkapplication/Makefile
plugins/project-wizard/templates/gtkapplication/src/Makefile
plugins/project-wizard/templates/gtkapplication/po/Makefile
plugins/project-wizard/templates/anjuta-plugin/Makefile
plugins/project-wizard/templates/anjuta-plugin/src/Makefile
plugins/project-wizard/templates/anjuta-plugin/po/Makefile
plugins/project-wizard/templates/gtkmm/Makefile
plugins/project-wizard/templates/gtkmm/src/Makefile
plugins/project-wizard/templates/gtkmm/po/Makefile
plugins/project-wizard/templates/gcj/Makefile
plugins/project-wizard/templates/gcj/src/Makefile
plugins/project-wizard/templates/java/Makefile
plugins/project-wizard/templates/java/src/Makefile
plugins/project-wizard/templates/java/po/Makefile
plugins/project-wizard/templates/python/Makefile
plugins/project-wizard/templates/python/src/Makefile
plugins/project-wizard/templates/python/po/Makefile
plugins/project-wizard/templates/pygtk/Makefile
plugins/project-wizard/templates/pygtk/src/Makefile
plugins/project-wizard/templates/pygtk/po/Makefile
plugins/project-wizard/templates/vala-gtk/Makefile
plugins/project-wizard/templates/vala-gtk/src/Makefile
plugins/project-wizard/templates/wxwin/Makefile
plugins/project-wizard/templates/wxwin/src/Makefile
plugins/project-wizard/templates/wxwin/po/Makefile
plugins/project-wizard/templates/xlib/Makefile
plugins/project-wizard/templates/xlib/src/Makefile
plugins/project-wizard/templates/xlib/po/Makefile
plugins/project-wizard/templates/xlib-dock/Makefile
plugins/project-wizard/templates/xlib-dock/src/Makefile
plugins/project-wizard/templates/xlib-dock/po/Makefile
plugins/project-wizard/templates/sdl/Makefile
plugins/project-wizard/templates/sdl/src/Makefile
plugins/project-wizard/templates/sdl/po/Makefile
plugins/project-wizard/templates/library/Makefile
plugins/project-wizard/templates/library/src/Makefile
plugins/project-wizard/templates/library/po/Makefile
plugins/project-wizard/templates/directory/Makefile
plugins/project-wizard/templates/m4/Makefile
plugins/project-wizard/templates/gnome-shell-extension/Makefile
plugins/project-wizard/templates/gnome-shell-extension/src/Makefile
plugins/project-wizard/templates/vala-library/Makefile
plugins/project-wizard/templates/vala-library/src/Makefile
plugins/project-wizard/templates/vala-library/po/Makefile
plugins/am-project/Makefile
plugins/am-project/tests/atlocal
plugins/am-project/tests/Makefile
plugins/mk-project/Makefile
plugins/dir-project/Makefile
plugins/language-support-cpp-java/Makefile
plugins/run-program/Makefile
plugins/starter/Makefile
plugins/indentation-c-style/Makefile
plugins/indentation-python-style/Makefile
plugins/language-support-js/Makefile
plugins/language-support-vala/Makefile
plugins/language-support-python/Makefile
plugins/parser-cxx/Makefile
plugins/parser-cxx/cxxparser/Makefile
plugins/python-loader/Makefile
plugins/jhbuild/Makefile
plugins/quick-open/Makefile
anjuta.desktop.in
manuals/Makefile
manuals/reference/Makefile
manuals/reference/libanjuta/Makefile
manuals/reference/libanjuta/version.xml
manuals/anjuta-manual/Makefile
manuals/anjuta-faqs/Makefile
manuals/anjuta-build-tutorial/Makefile
mime/Makefile
])
AC_OUTPUT
echo " "
echo "-------------------------------------------------------------------"
echo "Conditionally built plugins:"
echo "-------------------------------------------------------------------"
if [ test -n "$SVN_INCLUDE" ]; then
        echo "Building subversion plugin: ............................YES"
else
        echo "Building subversion plugin: ............................NO"
		echo "        Requires apr (>= 0.9.4); http://subversion.org"
		echo "        Requires apr-util (>= 0.9.4); http://subversion.org"
		echo "        Requires neon (>= 0.28.2); http://subversion.org"
		echo "        Requires subversion (>= 1.5); http://subversion.org"
fi

if [ test x$gladeui_found = xyes ]; then
       echo "Building glade plugin: .................................YES"
else
       echo "Building glade plugin: .................................NO"
       echo "        Requires glade-3 (>= $GLADEUI_REQUIRED)"
fi

if [ test x$devhelp_enabled = xyes ]; then
       echo "Building devhelp plugin: ...............................YES"
else
       echo "Building devhelp plugin: ...............................NO"
       echo "        Requires libdevhelp-3.0 (>= $LIBDEVHELP_REQUIRED)"
fi

if [ test x$terminal_enabled = xyes ]; then
       echo "Building terminal plugin: ..............................YES"
else
       echo "Building terminal plugin: ..............................NO"
       echo "        Requires vte-2.91 (>= $VTE_REQUIRED)"
fi

if [ test x$enable_vala = xyes ]; then
        echo "Building Vala support: .................................YES"
else
        echo "Building Vala support: .................................NO"
        echo "        Requires libvala (>= $VALA_REQUIRED)"
fi
if [ test x$have_python = xyes]; then
        echo "Building python plugin loaeder: ........................YES"
else
        echo "Building python plugin loaeder: ........................NO"
        echo "        Requires python"
fi
echo "-------------------------------------------------------------------"
echo "AM_CFLAGS = $AM_CFLAGS"
echo "AM_CXXFLAGS = $AM_CXXFLAGS"
