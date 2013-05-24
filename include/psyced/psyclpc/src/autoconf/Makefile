../configure-do: configure.in configure.head Makefile
	autoreconf
	rm -f $@
	cat configure.head configure >>$@
	chmod +x $@
	@rm configure
	mv machine.h.in ..

