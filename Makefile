# pktc: build the single-file compiler, run the tests, fuzz the examples.
#
#   make              build pktc.pyz, with Lark inside (runs with just
#                     python3 >= 3.10; building it needs Lark installed)
#   make test         compiler tests (python3 -m unittest)
#   make check        compile and run each example's C tests
#   make fuzz         fuzz each example for FUZZ_SECONDS (needs clang)
#   make fuzz-stun    fuzz one example
#   make examples     regenerate examples/ after changing the emitter

PYTHON ?= python3
CC ?= gcc
CFLAGS ?= -std=c23 -Wall -Wextra -pedantic -Werror
SANITIZE = -g -fsanitize=address,undefined -fno-sanitize-recover=all
FUZZ_SECONDS ?= 60

EXAMPLES = tftp pcp ntp stun
SOURCES = pktc.py schema.py check.py ir.py emit.py
BUILD ?= build
PYZ ?= pktc.pyz

all: $(PYZ)

# The installed Lark (pure Python, MIT) goes inside, with its license.
LARK_DIR = $(shell $(PYTHON) -c 'import lark, os; print(os.path.dirname(lark.__file__))')
LARK_LICENSE = $(shell $(PYTHON) -c 'import importlib.metadata as m; \
    d = m.distribution("lark"); \
    print(next(d.locate_file(f) for f in d.files if f.name == "LICENSE"))')

# zipapp's own -m entry point ignores main()'s return value, so errors
# would exit 0; write __main__.py ourselves.
$(PYZ): $(SOURCES)
	@test -n "$(LARK_DIR)" || { echo "Lark is not installed (pip install lark)"; exit 1; }
	rm -rf $(BUILD)/pyz
	mkdir -p $(BUILD)/pyz
	cp $(SOURCES) $(BUILD)/pyz/
	cp -r $(LARK_DIR) $(BUILD)/pyz/lark
	cp $(LARK_LICENSE) $(BUILD)/pyz/lark/LICENSE
	rm -rf $(BUILD)/pyz/lark/__pyinstaller
	find $(BUILD)/pyz -name __pycache__ -prune -exec rm -rf {} +
	printf 'import sys\nimport pktc\nsys.exit(pktc.main())\n' > $(BUILD)/pyz/__main__.py
	$(PYTHON) -m zipapp $(BUILD)/pyz -p '/usr/bin/env python3' -c -o $@

examples:
	for p in $(EXAMPLES); do $(PYTHON) pktc.py examples/$$p/$$p.pkt --fuzz || exit 1; done

test:
	$(PYTHON) -m unittest discover tests

check: $(EXAMPLES:%=check-%)

check-%:
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SANITIZE) examples/$*/test_$*.c examples/$*/$*_gen.c -o $(BUILD)/test_$*
	$(BUILD)/test_$*

fuzz: $(EXAMPLES:%=fuzz-%)

# Crashes and the corpus go to $(BUILD)/fuzz-<proto>/.
fuzz-%:
	mkdir -p $(BUILD)/fuzz-$*/corpus
	clang $(CFLAGS) -g -fsanitize=fuzzer,address,undefined \
	    examples/$*/$*_fuzz.c examples/$*/$*_gen.c -o $(BUILD)/fuzz-$*/fuzz
	cd $(BUILD)/fuzz-$* && ./fuzz -max_total_time=$(FUZZ_SECONDS) -max_len=1200 corpus

clean:
	rm -rf $(BUILD) $(PYZ)

.PHONY: all examples test check fuzz clean
