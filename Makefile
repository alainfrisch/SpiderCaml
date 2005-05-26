SOURCES= spiderCaml_stubs.c spiderCaml.mli spiderCaml.ml
CLIBS = smjs
CFLAGS = -I /usr/include/smjs
RESULT = spiderCaml
CC = gcc

LIBINSTALL_FILES = \
  spiderCaml.cmi \
  spiderCaml.mli \
  spiderCaml.cma libspiderCaml_stubs.a \
  $(wildcard dllspiderCaml_stubs.so) \
  $(wildcard spiderCaml.cmxa) $(wildcard spiderCaml.a)

all: byte-code-library
opt: native-code-library
install: libinstall

clean::
	rm -f *~
	(cd samples; make clean)

clean:: clean-doc


-include OCamlMakefile
