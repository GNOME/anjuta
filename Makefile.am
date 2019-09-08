## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

@INTLTOOL_DESKTOP_RULE@

SUBDIRS = \
	pixmaps scripts data \
	doc libanjuta plugins src manuals mime launcher po

README: README.md

dist_doc_DATA = \
	MAINTAINERS\
	README.md\
	COPYING\
	AUTHORS\
	ChangeLog\
	NEWS\
	TODO\
	FUTURE\
	ROADMAP\
	THANKS

README: README.md
	cp $< $@

## Install it separately in datadir also because it is used in credits
## listing and packaging could compress the ones in docs dir.

anjuta_datadir = $(datadir)/anjuta
anjuta_data_DATA = AUTHORS

gnomerelease:
	scp *.tar.bz2 master.gnome.org:

sfrelease:
	scp *.tar.bz2 clubfan@frs.sf.net:uploads

gnomemenudir = $(datadir)/applications
gnomemenu_premerge_file = anjuta.desktop.in
gnomemenu_DATA = $(gnomemenu_premerge_file:.desktop.in=.desktop)

@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/metainfo
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = anjuta.appdata.xml.in

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

EXTRA_DIST = \
	anjuta.desktop.in.in \
	autogen.sh \
	anjuta.prj \
	setup-gettext \
	intltool-extract.in\
	intltool-merge.in\
	intltool-update.in\
	$(appdata_in_files)


CLEANFILES = \
	anjuta.appdata.xml \
	$(gnomemenu_DATA)

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache.lock \
	$(gnomemenu_DATA)

-include $(top_srcdir)/git.mk
