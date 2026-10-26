# =============================================================================
# Fenrir - Terminal Desktop Environment
# =============================================================================

CMAKE        := cmake
CMAKE_BUILD  := $(CMAKE) --build
INSTALL      := $(CMAKE) --install

BUILD_DIR    := build
PREFIX       := $(HOME)/.local
CONFIG_DIR   := $(HOME)/.config/fenrir

.PHONY: clean build build_all install uninstall run test build_test help

help:
	@echo "Usage:"
	@echo "  make build      - Build the project (binary only, no tests)"
	@echo "  make clean      - Remove build artifacts"
	@echo "  make run        - Build and run fenrir"
	@echo "  make test       - Build and run the test suite"
	@echo "  make build_test - Build the test suite"
	@echo "  make build_all  - Build the tests and application"
	@echo "  make install    - Install the compiled binary to $(PREFIX)"
	@echo "  make uninstall  - Uninstall the binary from $(PREFIX)"
	@echo ""
	@echo "CMake options (passed via CMAKE_OPTS):"
	@echo "  CMAKE_OPTS=-DCMAKE_BUILD_TYPE=Debug  Debug build"

build:
	$(CMAKE) -B $(BUILD_DIR) -DBUILD_TESTING=OFF $(CMAKE_OPTS)
	$(CMAKE_BUILD) $(BUILD_DIR) --target fenrir_desktop

build_all:
	$(CMAKE) -B $(BUILD_DIR) -DBUILD_TESTING=ON $(CMAKE_OPTS)
	$(CMAKE_BUILD) $(BUILD_DIR)

install: build
	$(INSTALL) $(BUILD_DIR) --prefix $(PREFIX)

uninstall:
	rm -f $(PREFIX)/bin/fenrir
	@printf "Remove configuration directory %s? [y/N] " "$(CONFIG_DIR)"; \
	read ans; \
	case "$$ans" in \
		[yY]|[yY][eE][sS]) rm -rf "$(CONFIG_DIR)"; echo "Removed $(CONFIG_DIR)";; \
		*) echo "Kept $(CONFIG_DIR)";; \
	esac

clean:
	rm -rf $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/fenrir

build_test:
	$(CMAKE) -B $(BUILD_DIR) -DBUILD_TESTING=ON $(CMAKE_OPTS)
	$(CMAKE_BUILD) $(BUILD_DIR) --target fenrir_tests

test: build_test
	./$(BUILD_DIR)/fenrir_tests
