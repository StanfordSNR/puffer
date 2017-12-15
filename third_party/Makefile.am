all-local:
	cp -dR --no-preserve=mode --preserve=timestamps --no-target-directory --update --reflink=auto $(abs_srcdir)/daala_tools.fork ./daala_tools && cd daala_tools && $(MAKE) $(AM_MAKEFLAGS)
	mkdir -p ./libwebm && cd ./libwebm && cmake -DENABLE_WEBMTS:BOOL=OFF -DENABLE_WEBMINFO:BOOL=OFF $(abs_srcdir)/libwebm.fork && $(MAKE) $(AM_MAKEFLAGS)

check-local:

clean-local:
	rm -rf daala_tools
	rm -rf libwebm

install-data-local:

install-exec-local:

uninstall-local:

EXTRA_DIST = daala_tools.fork libwebm.fork
