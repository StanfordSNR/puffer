all-local:
	mkdir -p ./libwebm && cd ./libwebm && cmake -DENABLE_WEBMTS=OFF -DENABLE_WEBMINFO=OFF $(abs_srcdir)/libwebm.fork && $(MAKE) $(AM_MAKEFLAGS)

clean-local:
	rm -rf libwebm

EXTRA_DIST = libwebm.fork json.upstream libtorch
