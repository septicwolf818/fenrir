# =============================================================================
# Fenrir - Terminal Desktop Environment
# =============================================================================

CMAKE        := cmake
CMAKE_BUILD  := $(CMAKE) --build
INSTALL      := $(CMAKE) --install

BUILD_DIR    := build
PREFIX       := $(HOME)/.local

.PHONY: clean build install uninstall run help

help:
	@echo "Usage:"
	@echo "  make build    - Build the project"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make run      - Run fenrir executable from build dir"
	@echo "  make install  - Install to $(PREFIX)"
	@echo "  make uninstall - Uninstall from $(PREFIX)"
	@echo ""
	@echo "CMake options (passed via CMAKE_OPTS):"
	@echo "  CMAKE_OPTS=-DCMAKE_BUILD_TYPE=Debug  Debug build"

build:
	$(CMAKE) -B $(BUILD_DIR)
	$(CMAKE_BUILD) $(BUILD_DIR)

install:
	$(INSTALL) $(BUILD_DIR) --prefix $(PREFIX)

uninstall:
	$(INSTALL) $(BUILD_DIR) --prefix $(PREFIX) --remove-build-dir

clean:
	rm -rf $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/fenrir
