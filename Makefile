# LibRpc Makefile

CXX = g++
AR = ar

# Compiler flags
CXXFLAGS = -std=c++14 -Wall -Wextra -O2 -g
CXXFLAGS += -Iinclude
CXXFLAGS += -pthread

# Linker flags
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
LIB_DIR = lib

# Output
LIBRARY = $(LIB_DIR)/librpc.a
SHARED_LIB = $(LIB_DIR)/librpc.so

# Test programs (core test suite)
TEST_INTEGRITY = test_integrity_simple
TEST_MULTI_PROC = test_multi_process
TEST_BIDIRECTIONAL = test_bidirectional
TEST_INPROCESS_FD = test_inprocess_fullduplex

# Source files
SRCS = $(SRC_DIR)/NodeImpl.cpp \
       $(SRC_DIR)/UdpTransport.cpp

OBJECTS = $(BUILD_DIR)/NodeImpl.o \
          $(BUILD_DIR)/UdpTransport.o

# Targets
.PHONY: all clean lib tests help

all: lib

lib: $(LIBRARY) $(SHARED_LIB)

tests: $(TEST_INTEGRITY) $(TEST_MULTI_PROC) $(TEST_BIDIRECTIONAL) $(TEST_INPROCESS_FD)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

# Static library
$(LIBRARY): $(LIB_DIR) $(OBJECTS)
	@echo "Creating static library: $@"
	$(AR) rcs $@ $(OBJECTS)

# Shared library
$(SHARED_LIB): $(LIB_DIR) $(OBJECTS)
	@echo "Creating shared library: $@"
	$(CXX) -shared -o $@ $(OBJECTS) $(LDFLAGS)

# Object files
$(BUILD_DIR)/NodeImpl.o: $(SRC_DIR)/NodeImpl.cpp | $(BUILD_DIR)
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

$(BUILD_DIR)/UdpTransport.o: $(SRC_DIR)/UdpTransport.cpp | $(BUILD_DIR)
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

# Test programs
$(TEST_INTEGRITY): test_integrity_simple.cpp $(LIBRARY)
	@echo "Building test: $@"
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(LIB_DIR) -lrpc $(LDFLAGS)

$(TEST_MULTI_PROC): test_multi_process.cpp $(LIBRARY)
	@echo "Building test: $@"
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(LIB_DIR) -lrpc $(LDFLAGS)

$(TEST_BIDIRECTIONAL): test_bidirectional.cpp $(LIBRARY)
	@echo "Building test: $@"
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(LIB_DIR) -lrpc $(LDFLAGS)

$(TEST_INPROCESS_FD): test_inprocess_fullduplex.cpp $(LIBRARY)
	@echo "Building test: $@"
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(LIB_DIR) -lrpc $(LDFLAGS)

# Clean
clean:
	@echo "Cleaning..."
	rm -rf $(BUILD_DIR) $(LIB_DIR) $(TEST_INTEGRITY) $(TEST_MULTI_PROC) $(TEST_BIDIRECTIONAL) $(TEST_INPROCESS_FD)
	@echo "Clean complete"

# Help
help:
	@echo "LibRPC Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build library (default)"
	@echo "  lib     - Build static and shared libraries"
	@echo "  tests   - Build all test programs"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Test Programs:"
	@echo "  test_integrity_simple    - 200-message integrity test"
	@echo "  test_multi_process       - 4-process complex scenario"
	@echo "  test_bidirectional       - Bidirectional communication test"
	@echo "  test_inprocess_fullduplex - Same-process full-duplex test"
	@echo ""
	@echo "Usage:"
	@echo "  make           # Build library"
	@echo "  make tests     # Build all test programs"
	@echo "  make clean     # Clean build"
	@echo ""
	@echo "Run Tests:"
	@echo "  ./run_integrity_tests.sh  # Run complete test suite"
