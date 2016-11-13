###############################
## BUILD OPTIONS
###############################

TEMPLATE = lib
TARGET   = NifParser

QT += xml widgets
CONFIG += staticlib

# Require Qt 5.5 or higher
contains(QT_VERSION, ^5\\.[0-4]\\..*) {
	message("Cannot build NifParser with Qt version $${QT_VERSION}")
	error("Minimum required version is Qt 5.5")
}

# C++11 Support
CONFIG += c++11

# Dependencies

# Debug/Release options
CONFIG(debug, debug|release) {
	# Debug Options
	BUILD = debug
	CONFIG += console
} else {
	# Release Options
	BUILD = release
	CONFIG -= console
	DEFINES += QT_NO_DEBUG_OUTPUT
}
# TODO: Get rid of this define
#	uncomment this if you want the text stats gl option
#	DEFINES += USE_GL_QPAINTER

INCLUDEPATH += src lib

# Require explicit
DEFINES += \
	QT_NO_CAST_FROM_BYTEARRAY \ # QByteArray deprecations
	QT_NO_URL_CAST_FROM_STRING \ # QUrl deprecations
	QT_DISABLE_DEPRECATED_BEFORE=0x050300 #\ # Disable all functions deprecated as of 5.3

	# Useful for tracking down strings not using
	#	QObject::tr() for translations.
	# QT_NO_CAST_FROM_ASCII \
	# QT_NO_CAST_TO_ASCII


VISUALSTUDIO = false
*msvc* {
	######################################
	## Detect Visual Studio vs Qt Creator
	######################################
	#	Qt Creator = shadow build
	#	Visual Studio = no shadow build

	# Strips PWD (source) from OUT_PWD (build) to test if they are on the same path
	#	- contains() does not work
	#	- equals( PWD, $${OUT_PWD} ) is not sufficient
	REP = $$replace(OUT_PWD, $${PWD}, "")

	# Test if Build dir is outside Source dir
	#	if REP == OUT_PWD, not Visual Studio
	!equals( REP, $${OUT_PWD} ):VISUALSTUDIO = true
	unset(REP)

	# Set OUT_PWD to ./bin so that qmake doesn't clutter PWD
	#	Unfortunately w/ VS qmake still creates empty debug/release folders in PWD.
	#	They are never used but get auto-generated because of CONFIG += debug_and_release
	$$VISUALSTUDIO:OUT_PWD = $${_PRO_FILE_PWD_}/bin
}

###############################
## FUNCTIONS
###############################

include(NifSkope_functions.pri)


###############################
## MACROS
###############################

# NifSkope Version
VER = $$getVersion()
# NifSkope Revision
REVISION = $$getRevision()

# NIFSKOPE_VERSION macro
DEFINES += NIFSKOPE_VERSION=\\\"$${VER}\\\"

# NIFSKOPE_REVISION macro
!isEmpty(REVISION) {
	DEFINES += NIFSKOPE_REVISION=\\\"$${REVISION}\\\"
}


###############################
## OUTPUT DIRECTORIES
###############################

# build_pass is necessary
# Otherwise it will create empty .moc, .ui, etc. dirs on the drive root
build_pass|!debug_and_release {
	win32:equals( VISUALSTUDIO, true ) {
		# Visual Studio
		DESTDIR = $${_PRO_FILE_PWD_}/bin/$${BUILD}
		# INTERMEDIATE FILES
		INTERMEDIATE = $${DESTDIR}/../GeneratedFiles/$${BUILD}
	} else {
		# Qt Creator
		DESTDIR = $${OUT_PWD}/$${BUILD}
		# INTERMEDIATE FILES
		INTERMEDIATE = $${DESTDIR}/../GeneratedFiles/
	}

	UI_DIR = $${INTERMEDIATE}/.ui
	MOC_DIR = $${INTERMEDIATE}/.moc
	RCC_DIR = $${INTERMEDIATE}/.qrc
	OBJECTS_DIR = $${INTERMEDIATE}/.obj
}

###############################
## TARGETS
###############################

include(NifSkope_targets.pri)


###############################
## PROJECT SCOPES
###############################

HEADERS += \
	src/basemodel.h \
	src/config.h \
	src/message.h \
	src/nifexpr.h \
	src/nifitem.h \
	src/nifmodel.h \
	src/nifproxy.h \
	src/niftypes.h \
	src/nifvalue.h \
	src/settings.h \
	src/version.h \
	lib/half.h

SOURCES += \
	src/basemodel.cpp \
	src/message.cpp \
	src/nifexpr.cpp \
	src/nifmodel.cpp \
	src/nifproxy.cpp \
	src/niftypes.cpp \
	src/nifvalue.cpp \
	src/nifxml.cpp \
	src/version.cpp \
	lib/half.cpp

###############################
## DEPENDENCY SCOPES
###############################

###############################
## COMPILER SCOPES
###############################

QMAKE_CXXFLAGS_RELEASE -= -O
QMAKE_CXXFLAGS_RELEASE -= -O1
QMAKE_CXXFLAGS_RELEASE -= -O2

win32 {
	DEFINES += EDIT_ON_ACTIVATE
}

# MSVC
#  Both Visual Studio and Qt Creator
#  Required: msvc2013 or higher
*msvc* {

	# Grab _MSC_VER from the mkspecs that Qt was compiled with
	#	e.g. VS2013 = 1800, VS2012 = 1700, VS2010 = 1600
	_MSC_VER = $$find(QMAKE_COMPILER_DEFINES, "_MSC_VER")
	_MSC_VER = $$split(_MSC_VER, =)
	_MSC_VER = $$member(_MSC_VER, 1)

	# Reject unsupported MSVC versions
	!isEmpty(_MSC_VER):lessThan(_MSC_VER, 1800) {
		error("NifSkope only supports MSVC 2013 or later. If this is too prohibitive you may use Qt Creator with MinGW.")
	}

	# So VCProj Filters do not flatten headers/source
	CONFIG -= flat

	# COMPILER FLAGS

	#  Optimization flags
	QMAKE_CXXFLAGS_RELEASE *= -O2 -arch:SSE2 # SSE2 is the default, but make it explicit
	#  Multithreaded compiling for Visual Studio
	QMAKE_CXXFLAGS += -MP

	# LINKER FLAGS

	#  Relocate .lib and .exp files to keep release dir clean
	QMAKE_LFLAGS += /IMPLIB:$$syspath($${INTERMEDIATE}/NifSkope.lib)

	#  PDB location
	QMAKE_LFLAGS_DEBUG += /PDB:$$syspath($${INTERMEDIATE}/nifskope.pdb)

	#  Clean up .embed.manifest from release dir
	#	Fallback for `Manifest Embed` above
	QMAKE_POST_LINK += $$QMAKE_DEL_FILE $$syspath($${DESTDIR}/*.manifest) $$nt
}


# MinGW, GCC
#  Recommended: GCC 4.8.1+
*-g++ {

	# COMPILER FLAGS

	#  Optimization flags
	QMAKE_CXXFLAGS_DEBUG -= -O0 -g
	QMAKE_CXXFLAGS_DEBUG *= -Og -g3
	QMAKE_CXXFLAGS_RELEASE *= -O3 -mfpmath=sse

	# C++11 Support
	QMAKE_CXXFLAGS_RELEASE *= -std=c++11

	#  Extension flags
	QMAKE_CXXFLAGS_RELEASE *= -msse2 -msse
}

win32 {
    # GL libs for Qt 5.5+
    LIBS += -lopengl32 -lglu32
}

unix:!macx {
	LIBS += -lGLU
}

macx {
	LIBS += -framework CoreFoundation
}


# Build Messages
# (Add `buildMessages` to CONFIG to use)
buildMessages:build_pass|buildMessages:!debug_and_release {
	CONFIG(debug, debug|release) {
		message("Debug Mode")
	} CONFIG(release, release|debug) {
		message("Release Mode")
	}

	message(mkspec _______ $$QMAKESPEC)
	message(cxxflags _____ $$QMAKE_CXXFLAGS)
	message(arch _________ $$QMAKE_TARGET.arch)
	message(src __________ $$PWD)
	message(build ________ $$OUT_PWD)
	message(Qt binaries __ $$[QT_INSTALL_BINS])

	build_pass:equals( VISUALSTUDIO, true ) {
		message(Visual Studio __ Yes)
	}

	#message($$CONFIG)
}

# vim: set filetype=config :
