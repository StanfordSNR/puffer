all-local:
	cp -dR --no-preserve=mode --preserve=timestamps --no-target-directory --update --reflink=auto $(abs_srcdir)/daala_tools.fork ./daala_tools && cd daala_tools && $(MAKE) $(AM_MAKEFLAGS)
	mkdir -p ./libwebm && cd ./libwebm && cmake -DENABLE_WEBMTS=OFF -DENABLE_WEBMINFO=OFF $(abs_srcdir)/libwebm.fork && $(MAKE) $(AM_MAKEFLAGS)
	mkdir -p ./yaml-cpp && cd ./yaml-cpp && cmake -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF $(abs_srcdir)/yaml-cpp.upstream && $(MAKE) $(AM_MAKEFLAGS)
	mkdir -p ./json && cp $(abs_srcdir)/json.upstream/single_include/nlohmann/json.hpp ./json/json.hpp

check-local:

clean-local:
	rm -rf daala_tools
	rm -rf libwebm
	rm -rf yaml-cpp
	rm -rf json

install-data-local:

install-exec-local:

uninstall-local:

EXTRA_DIST = daala_tools.fork libwebm.fork yaml-cpp.upstream json.upstream
