all-local:
	mkdir -p ./libwebm && cd ./libwebm && cmake -DENABLE_WEBMTS=OFF -DENABLE_WEBMINFO=OFF $(abs_srcdir)/libwebm.fork && $(MAKE) $(AM_MAKEFLAGS)

check-local:

clean-local:
	rm -rf libwebm

install-data-local:

install-exec-local:

uninstall-local:

EXTRA_DIST = libwebm.fork json.upstream
