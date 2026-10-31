.POSIX:
.PHONY: all debug release test check bench clean setup

all:
	@echo "Building Debug and Release in parallel..."
	@$(MAKE) -j2 debug release
	@echo "Build complete: Debug (build/cgui) | Release (build-release/cgui)"

debug:
	@ninja -C build

release:
	@ninja -C build-release

test:
	@echo "--- Testing Debug ---"
	@meson test -C build
	@echo "--- Testing Release ---"
	@meson test -C build-release

check: test

bench: release
	@echo "--- Running Release Performance Benchmarks (-O3 -flto) ---"
	@./build-release/cgui-bench

clean:
	@ninja -C build clean 2>/dev/null || true
	@ninja -C build-release clean 2>/dev/null || true

setup:
	@test -d build || meson setup build --buildtype=debug
	@test -d build-release || meson setup build-release --buildtype=release -Db_lto=true -Db_ndebug=true
