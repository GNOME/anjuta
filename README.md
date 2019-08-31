# Anjuta Version 2.0.0 Alpha Release

Copyright (C) 2000-2005 Naba Kumar  
Home site => <http://anjuta.org/>

## Introduction

Anjuta DevStudio is a versatile Integrated Development Environment (IDE)
on GNOME Desktop Environment and features a number of advanced
programming facilities. These include project management, application and
class wizards, an on-board interactive debugger, powerful source editor,
syntax highlighting, intellisense autocompletions, symbol navigation,
version controls, integrated GUI designing and other tools.

Anjuta is an effort to marry the flexibility and power of text-based
command-line tools with the ease-of-use of the GNOME graphical user
interface. We thrive to make it very usable by eleminating developers
burden with mudane programming tasks.

Anjuta 2.x is the next generation Anjuta development studio with extensible
plugin architecture. The new architecture allows writing independent
plugins that could interact with existing plugins. Most of the older
features have been already ported to the new architecture and are working.

The new architecture has:

* Complex docking shell that allows arbitrary docking of IDE widgets.
* Dynamic plugin activation and deactivation.
* Extensible architecture.
* Plugins can interact with other plugins through interface.
* Plugins can install dynamic UIs (menus/toolbars), including installing
  context menus in other plugins.

At present, Anjuta is only a beta release and lots of work needs to be done to
improve it. In the future, Anjuta is going to be much more capable and stable!
Extensive debugging has not yet been performed, so at the moment it will
undoubtedly have lots of bugs. It would be great if you use it and report any
problems to us. That way, you will be helping to improve the software as well
as helping the Open Source community. Please read the file COPYRIGHT for
details.

Any sort of suggestions or patches for Anjuta are also most welcome.

## Presently available features in Anjuta

1. Fully customizable integrated editor.
    * Automatic syntax highlighting.
    * Automatic code formatting.
    * Code folding/hiding.
    * Line numbers/markers display.
    * Text zooming.
    * Code autocompletion.
    * Calltips for Linux/GNOME function prototypes.
    * Automatic indentation and indentation guides.
    * ... and much more.
2. Each file opened can be operated in either paged mode
or windowed mode.
3. Highly interactive source level debugger (built over gdb).
    * Interactive execution.
    * Breakpoints/watches/signal/stack manipulation.
    * Local Variables view.
    * Variables tips.
    * ... and much more.
4. Inbuilt application wizards to create terminal/GTK/GNOME
applications on-the-fly.
5. Dynamic Tags browsing.
    * Function definition, structure, classes etc.
    * Full tags management for project.
    * Symbol Browser.
    * File Brwoser.
6. Full project and build files management.
7. Bookmark management.
8. Basic windows are attachable and detachable.
9. Support for other languages
    * Java, Perl, Pascal ... etc. (only file mode, no project
    managements).
10. Interactive messaging system.
11. GNOME2 platform compliant.

## Contact Email addresses for Anjuta

Anjuta Home site: <http://anjuta.org>.

For general discussions and queries use the discussion forums (two forums)
at the project site. Please report bugs at the projects bug tracking system
or the mailing lists at the project site. News of Anjuta is also available
at the project site.

To subscribe to one or more of the mailing lists, visit
[<http://mail.gnome.org/mailman/listinfo/anjuta-devel-list>].
or send a mail at `<listname>-request@gnome.org` with a subject
just "subscribe". Where, `<listname>` is anjuta-list or anjuta-devel-list.

Mailing lists:

* <anjuta-list@gnome.org>
  * For general discussion.
* <anjuta-devel-list@gnome.org>
  * For development discussion.

## REQUIREMENTS

1) Installation from tarball:
    * GTK libs (2.0 or later)
    * GNOME libs (2.0 or later)
    * gnome-xml (aka libxml1) (1.4.0 or later)
    * gnome-print
    * gdk-pixbuf
    * scrollkeeper
    * pkgconfig
    * gnome-build (the latest release)
    * gdl (the latest release)

    Optional:
    * devhelp (0.13 or later): for integrated API help
    * glade3 (3.1.5 or later): for integrated UI designing
    * libsvn: (www.subversion.org) to build subversion plugin
    * graphviz: for class hierarchy view and profiler.

2) Installation from repository:
    * All the requirements for tarball
    * GNU gettext 0.10.x (or later)
    * Automake 1.4 (or later)
    * Autoconf 2.13 (or later)

3) Running
    * X-Windows, any window manager should do as long as the gnome libs
      are installed)
    * Bash command shell
    * GNU Indent
    * yelp to browse docs
    * Automake/Autoconf
    * GNU Make
    * GNU C/C++ compiler
    * GNU debugger (gdb)
    * GNU grep
    * For cvs plugin: cvs

## INSTALLATION

Note:- The package as an example is taken as anjuta-1.0.tar.gz
and '#' is the shell prompt. You must be logged in as root to
install Anjuta.

1) Get the tarball
2) copy it to your home dir.
3) `tar xfzv anjuta-<version>.tar.gz`
4) Change dir: `cd anjuta-<version>`
5) Type: `./configure`
6) Type: `make`
7) Type: `make install`

That's all. If everything went smoothly, congratulations. If not,
then please check that you have the latest libgnome and libgnomeui
installed. Get them if you don't have and repeat the above steps.

## KNOWN ISSUES

* Calltips flicker when the cursor touches them if you have your window
manager set to follow-mouse-cursor for focus. Change it to focus-follow
mouse click to fix it.

## RUNNING

To run Anjuta, choose Applications -> Programming -> Anjuta IDE from the GNOME
Main Menu, or type:   `anjuta`

If you get the shell error 'Command not found', then make sure your PATH
variable contains the executable path of Anjuta binary (by default:
/usr/local/bin). Usually, the PATH variable is set in the file
/etc/profile. Check it out.

```sh
Usage: anjuta [--no-splash | -s] [ files ...] [ project ]
```

If you do not want to see the Anjuta splash screen / logo, run Anjuta with
the command-line option `--no-splash`. See the User Manual for further
instructions.

** Good Luck **
