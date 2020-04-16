CFLAGS=/nologo /W3 /DDLL_EXPORTS /D_CRT_SECURE_NO_DEPRECATE

build: so_stdio.lib

so_stdio.lib: so_stdio.obj
	link /dll /out:so_stdio.dll /implib:so_stdio.lib so_stdio.obj

so_stdio.obj: so_stdio.c
	cl /Fo $(CFLAGS) /c so_stdio.c

clean:
	del /F *.obj *.dll *.lib *.exp
