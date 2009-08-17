-include Makefile.conf

SOURCES= spiderCaml_stubs.c spiderCaml.mli spiderCaml.ml
RESULT = spiderCaml

all: byte-code-library
opt: native-code-library
install: libinstall

clean::
	rm -f *~
	(cd samples; make clean)

clean:: clean-doc

PACKAGE = SpiderCaml-0.2
.PHONY: package
package: clean
	(cd ..; cp -aR SpiderCaml $(PACKAGE); \
	tar czf $(PACKAGE).tar.gz --exclude CVS $(PACKAGE); \
	rm -Rf $(PACKAGE))

release:
	$(MAKE) package
	mv ../$(PACKAGE).tar.gz ../public_html/SpiderCaml/download
	$(MAKE) htdoc
	rm -Rf ../public_html/SpiderCaml/doc
	mv doc/spiderCaml/html ../public_html/SpiderCaml/doc

-include OCamlMakefile

LIBINSTALL_FILES = \
  spiderCaml.cmi \
  spiderCaml.mli \
  spiderCaml.cma $(RES_CLIB) \
  $(wildcard $(DLLSONAME)) \
  $(wildcard spiderCaml.cmxa) $(wildcard spiderCaml.$(EXT_LIB))
