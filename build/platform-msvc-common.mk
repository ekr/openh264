CC=cl
CXX=cl
AR=lib
CXX_O=-Fo$@
CFLAGS += -nologo -W3 -EHsc -fp:precise -Zc:wchar_t -Zc:forScope -DGTEST_HAS_TR1_TUPLE=0 -std=c++11
CXX_LINK_O=-nologo -Fe$@
AR_OPTS=-nologo -out:$@
CFLAGS_OPT=-O2 -Ob1 -Oy- -Zi -GF -Gm- -GS -Gy -MD -DNDEBUG
CFLAGS_DEBUG=-Od -Oy- -ZI -Gm -MDd -RTC1 -D_DEBUG
CFLAGS_M32=
CFLAGS_M64=
LINK_LIB=$(1).lib
LIBSUFFIX=lib
LIBPREFIX=
EXEEXT=.exe
OBJ=obj
SHAREDLIBSUFFIX=dll
SHARED=-LD
SHLDFLAGS=-link -def:wels.def -implib:wels_dll.lib
EXTRA_LIBRARY=wels_dll.lib
