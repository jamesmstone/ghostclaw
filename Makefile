.PHONY: configure build release test clean run

configure:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

build: configure
	cmake --build build -j

release:
	cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
	cmake --build build-release -j

test: build
	ctest --test-dir build --output-on-failure

run: build
	./build/ghostclaw --help

clean:
	rm -rf build build-release
