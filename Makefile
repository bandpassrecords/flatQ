.PHONY: setup build debug install run validate test clean universal
setup:     ; scripts/setup-macos.sh
build:     ; scripts/build.sh Release
debug:     ; scripts/build.sh Debug
universal: ; FREEQ_UNIVERSAL=1 scripts/build.sh Release
install: build ; scripts/install-macos.sh
run:       ; scripts/run-standalone.sh
validate:  ; scripts/validate.sh
test:      ; ctest --test-dir build --output-on-failure
clean:     ; rm -rf build dist
