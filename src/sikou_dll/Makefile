DEF_MACROS = /DNDEBUG /DSTRICT
CFLAGS     = /W4 /nologo /MT /O2 /GL
LDFLAGS    = /INCREMENTAL:NO /NOLOGO /LTCG

sikou.dll: sikou.obj sikou.res
	link $(LDFLAGS) /dll /out:sikou.dll sikou.obj sikou.res\
	user32.lib gdi32.lib comctl32.lib

sikou.obj: sikou.c sikou.h
	cl $(DEF_MACROS) $(CFLAGS) /c sikou.c

sikou.res: sikou.rc sikou.h bonanza.ico
	rc $(DEF_MACROS) /fosikou.res sikou.rc

clean:
	del *.obj *.lib *.exp *.dll *.res
