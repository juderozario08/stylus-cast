# Thin wrapper: CMake is the single build definition.
.PHONY: all test clean

all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
	ln -sf build/penbridge-server penbridge-server
	ln -sf build/compile_commands.json compile_commands.json

test: all
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build penbridge-server compile_commands.json test_protocol
