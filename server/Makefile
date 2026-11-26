# Thin wrapper: CMake is the single build definition for both Windows and macOS.
.PHONY: all test clean

ifeq ($(OS),Windows_NT)
GENERATOR ?= Ninja
CXX       ?= g++
EXE       := .exe

all:
	cmake -S . -B build -G "$(GENERATOR)" -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
	cmake -E copy_if_different build/stylus-cast-server$(EXE) stylus-cast-server$(EXE)
	cmake -E copy_if_different build/compile_commands.json compile_commands.json

clean:
	cmake -E rm -rf build stylus-cast-server$(EXE) compile_commands.json
else
all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
	ln -sf build/stylus-cast-server stylus-cast-server
	ln -sf build/compile_commands.json compile_commands.json

clean:
	rm -rf build stylus-cast-server compile_commands.json
endif

test: all
	ctest --test-dir build --output-on-failure
