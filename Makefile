# ===============================================
# NanoChessTurbo - One file Chess Engine
# ===============================================

# Compiler
CXX = g++

# Executable name
EXE = NanoChessTurbo

# Source files
SOURCES = main.cpp

# Compiler flags
CXXFLAGS = -std=c++11 -Wall -Wextra -Wshadow -pedantic

# Optimization flags for maximum performance
RELEASEFLAGS = -O3 -march=native -flto -funroll-loops -fomit-frame-pointer -DNDEBUG

# Debug flags
DEBUGFLAGS = -g -O0 -DDEBUG -fsanitize=address -fsanitize=undefined

# Profile flags for performance analysis
PROFILEFLAGS = -pg -O2 -DPROFILE

# Default target - build optimized version
all: release

# Release build - Maximum performance
release:
	@echo "========================================="
	@echo "Building NanoChessTurbo Release Version"
	@echo "========================================="
	$(CXX) $(CXXFLAGS) $(RELEASEFLAGS) $(SOURCES) -o $(EXE)
	@echo "Build complete: $(EXE)"
	@echo "Run with: ./$(EXE)"

# Debug build - For development and bug fixing
debug:
	@echo "========================================="
	@echo "Building NanoChessTurbo Debug Version"
	@echo "========================================="
	$(CXX) $(CXXFLAGS) $(DEBUGFLAGS) $(SOURCES) -o $(EXE)_debug
	@echo "Debug build complete: $(EXE)_debug"

# Profile build - For performance analysis
profile:
	@echo "========================================="
	@echo "Building NanoChessTurbo Profile Version"
	@echo "========================================="
	$(CXX) $(CXXFLAGS) $(PROFILEFLAGS) $(SOURCES) -o $(EXE)_profile
	@echo "Profile build complete: $(EXE)_profile"
	@echo "Run and then use: gprof $(EXE)_profile gmon.out"

# Fast build - Quick compilation for testing
fast:
	@echo "Fast build (less optimization)..."
	$(CXX) -O2 $(SOURCES) -o $(EXE)

# Windows build - Static linking for Windows
windows:
	@echo "========================================="
	@echo "Building NanoChessTurbo for Windows"
	@echo "========================================="
	$(CXX) $(CXXFLAGS) $(RELEASEFLAGS) $(SOURCES) -o $(EXE).exe -static -static-libgcc -static-libstdc++
	@echo "Windows build complete: $(EXE).exe"

# Cross-compile for Windows (from Linux)
windows-cross:
	@echo "Cross-compiling for Windows..."
	x86_64-w64-mingw32-g++ $(CXXFLAGS) $(RELEASEFLAGS) $(SOURCES) -o $(EXE).exe -static
	@echo "Windows cross-compile complete: $(EXE).exe"

# Clean all build files
clean:
	@echo "Cleaning build files..."
	rm -f $(EXE) $(EXE).exe $(EXE)_debug $(EXE)_profile
	rm -f *.o *.d *.gcda *.gcno *.gcov gmon.out
	rm -rf *.dSYM
	@echo "Clean complete"

# Install to system (Linux/Mac)
install: release
	@echo "Installing $(EXE)..."
	@if [ -w /usr/local/bin ]; then \
		cp $(EXE) /usr/local/bin/; \
		echo "Installation complete: /usr/local/bin/$(EXE)"; \
	else \
		echo "Error: Need sudo privileges. Run: sudo make install"; \
		exit 1; \
	fi

# Uninstall from system
uninstall:
	@echo "Uninstalling $(EXE)..."
	rm -f /usr/local/bin/$(EXE)
	@echo "Uninstallation complete"

# Run the engine
run: release
	@echo "Starting NanoChessTurbo..."
	@echo "========================================="
	./$(EXE)

# Test UCI protocol
test: release
	@echo "Testing UCI protocol..."
	@echo -e "uci\nisready\nquit" | ./$(EXE)

# Benchmark - Run a quick performance test
bench: release
	@echo "Running benchmark..."
	@echo -e "uci\nposition startpos\ngo depth 10\nquit" | ./$(EXE)

# Check for memory leaks (requires valgrind)
memcheck: debug
	@echo "Checking for memory leaks..."
	valgrind --leak-check=full --show-leak-kinds=all ./$(EXE)_debug

# Format code (requires clang-format)
format:
	@echo "Formatting code..."
	clang-format -i $(SOURCES) -style=file

# Static analysis (requires cppcheck)
analyze:
	@echo "Running static analysis..."
	cppcheck --enable=all --suppress=missingIncludeSystem $(SOURCES)

# Create distribution package
dist: clean
	@echo "Creating distribution package..."
	mkdir -p NanoChessTurbo-dist
	cp $(SOURCES) README.md LICENSE Makefile NanoChessTurbo-dist/
	tar -czf NanoChessTurbo.tar.gz NanoChessTurbo-dist/
	rm -rf NanoChessTurbo-dist/
	@echo "Distribution package created: NanoChessTurbo.tar.gz"

# Help - Show all available targets
help:
	@echo "NanoChessTurbo Build System"
	@echo "============================"
	@echo "Available targets:"
	@echo "  make           - Build optimized release version (default)"
	@echo "  make release   - Build optimized release version"
	@echo "  make debug     - Build debug version with sanitizers"
	@echo "  make profile   - Build with profiling support"
	@echo "  make fast      - Quick build with basic optimization"
	@echo "  make windows   - Build static Windows executable"
	@echo "  make clean     - Remove all build files"
	@echo "  make install   - Install to /usr/local/bin"
	@echo "  make uninstall - Remove from /usr/local/bin"
	@echo "  make run       - Build and run the engine"
	@echo "  make test      - Test UCI protocol"
	@echo "  make bench     - Run performance benchmark"
	@echo "  make memcheck  - Check for memory leaks (needs valgrind)"
	@echo "  make analyze   - Static code analysis (needs cppcheck)"
	@echo "  make dist      - Create distribution package"
	@echo "  make help      - Show this help message"
	@echo ""
	@echo "Build examples:"
	@echo "  make -j4       - Parallel build with 4 cores"
	@echo "  make CXX=clang++ - Build with clang instead of g++"

# Phony targets (not actual files)
.PHONY: all release debug profile fast windows windows-cross clean install uninstall run test bench memcheck format analyze dist help

# Print compiler version
version:
	@$(CXX) --version
