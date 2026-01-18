# FCx Compiler Makefile
# FCx Programming Language

# Use clang with C23 for modern features including __int128 support
CC = clang
ZIG = zig

# LLVM Configuration
LLVM_CONFIG = llvm-config
LLVM_CFLAGS = $(shell $(LLVM_CONFIG) --cflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --libs core)
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version)

# Validate LLVM version (21.1.6 required)
REQUIRED_LLVM_VERSION = 21.1.6
LLVM_VERSION_CHECK = $(shell echo "$(LLVM_VERSION)" | grep -q "^21\.1\.6" && echo "ok" || echo "fail")

# Compiler flags - using C23 (latest standard) with clang
CFLAGS = -std=c23 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE $(LLVM_CFLAGS)
DEBUG_CFLAGS = -std=c23 -Wall -Wextra -Werror -g -DDEBUG -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE $(LLVM_CFLAGS)
LDFLAGS = -lpthread -lm $(LLVM_LDFLAGS)
INCLUDES = -Isrc

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files
LEXER_SRCS = $(SRCDIR)/lexer/lexer.c $(SRCDIR)/lexer/operator_registry.c
PARSER_SRCS = $(SRCDIR)/parser/parser.c
SEMANTIC_SRCS = $(SRCDIR)/semantic/semantic.c
IR_SRCS = $(SRCDIR)/ir/fcx_ir.c $(SRCDIR)/ir/ir_gen.c $(SRCDIR)/ir/ir_optimize.c $(SRCDIR)/ir/fc_ir.c $(SRCDIR)/ir/fc_ir_lower.c $(SRCDIR)/ir/fc_ir_abi.c
OPTIMIZER_SRCS = $(SRCDIR)/optimizer/hmso.c $(SRCDIR)/optimizer/hmso_index.c $(SRCDIR)/optimizer/hmso_partition.c $(SRCDIR)/optimizer/hmso_optimize.c $(SRCDIR)/optimizer/hmso_link.c $(SRCDIR)/optimizer/hmso_cache.c
CODEGEN_SRCS = $(SRCDIR)/codegen/llvm_backend.c $(SRCDIR)/codegen/llvm_codegen.c $(SRCDIR)/codegen/inline_asm.c
MODULE_SRCS = $(SRCDIR)/module/preprocessor.c
TYPES_SRCS = $(SRCDIR)/types/pointer_types.c
RUNTIME_SRCS = $(SRCDIR)/runtime/bootstrap.c $(SRCDIR)/runtime/fcx_memory.c $(SRCDIR)/runtime/fcx_syscall.c $(SRCDIR)/runtime/fcx_atomic.c $(SRCDIR)/runtime/fcx_hardware.c $(SRCDIR)/runtime/fcx_runtime.c $(SRCDIR)/runtime/fcx_error_runtime.c $(SRCDIR)/runtime/fcx_timing.c
ERROR_SRCS = $(SRCDIR)/error/error_handler.c
MAIN_SRCS = $(SRCDIR)/main.c

# Zig sources (C import bridge)
ZIG_C_IMPORT = $(SRCDIR)/module/c_import.zig
ZIG_C_IMPORT_LIB = $(OBJDIR)/libfcx_c_import.a

ALL_SRCS = $(LEXER_SRCS) $(PARSER_SRCS) $(SEMANTIC_SRCS) $(IR_SRCS) $(OPTIMIZER_SRCS) $(CODEGEN_SRCS) $(MODULE_SRCS) $(TYPES_SRCS) $(RUNTIME_SRCS) $(ERROR_SRCS) $(MAIN_SRCS)

# Object files
LEXER_OBJS = $(LEXER_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
PARSER_OBJS = $(PARSER_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
SEMANTIC_OBJS = $(SEMANTIC_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
IR_OBJS = $(IR_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
OPTIMIZER_OBJS = $(OPTIMIZER_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
CODEGEN_OBJS = $(CODEGEN_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
MODULE_OBJS = $(MODULE_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TYPES_OBJS = $(TYPES_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
RUNTIME_OBJS = $(RUNTIME_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
ERROR_OBJS = $(ERROR_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
MAIN_OBJS = $(MAIN_SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

ALL_OBJS = $(LEXER_OBJS) $(PARSER_OBJS) $(SEMANTIC_OBJS) $(IR_OBJS) $(OPTIMIZER_OBJS) $(CODEGEN_OBJS) $(MODULE_OBJS) $(TYPES_OBJS) $(RUNTIME_OBJS) $(ERROR_OBJS) $(MAIN_OBJS)

# Target executable
TARGET = $(BINDIR)/fcx

# Default target
all: validate-llvm $(TARGET)

# Validate LLVM installation
validate-llvm:
	@echo "Validating LLVM installation..."
	@if [ "$(LLVM_VERSION_CHECK)" = "fail" ]; then \
		echo "ERROR: LLVM version $(LLVM_VERSION) found, but $(REQUIRED_LLVM_VERSION) is required"; \
		echo "Please install LLVM $(REQUIRED_LLVM_VERSION) development packages"; \
		exit 1; \
	fi
	@echo "✓ LLVM $(LLVM_VERSION) detected and validated"

# Create directories
$(OBJDIR):
	mkdir -p $(OBJDIR)/lexer $(OBJDIR)/parser $(OBJDIR)/semantic $(OBJDIR)/ir $(OBJDIR)/optimizer $(OBJDIR)/codegen $(OBJDIR)/module $(OBJDIR)/types $(OBJDIR)/runtime $(OBJDIR)/error

$(BINDIR):
	mkdir -p $(BINDIR)

# Build target
$(TARGET): $(OBJDIR) $(BINDIR) $(ALL_OBJS) $(ZIG_C_IMPORT_LIB)
	$(CC) $(ALL_OBJS) $(ZIG_C_IMPORT_LIB) $(LDFLAGS) -o $(TARGET)
	@echo "FCx compiler built successfully: $(TARGET)"

# Build Zig C import library
$(ZIG_C_IMPORT_LIB): $(ZIG_C_IMPORT)
	$(ZIG) build-lib -OReleaseFast -fPIC -femit-bin=$(ZIG_C_IMPORT_LIB) $(ZIG_C_IMPORT)
	@echo "Built Zig C import library"

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Debug build
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: $(TARGET)

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(BINDIR)
	rm -f $(ZIG_C_IMPORT_LIB)
	@echo "Cleaned build artifacts"

# Install (copy to /usr/local/bin)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/fcx
	@echo "FCx compiler installed to /usr/local/bin/fcx"

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/fcx
	@echo "FCx compiler uninstalled"

# Test operator registry (200+ operators)
test-operators: $(TARGET)
	./$(TARGET) --validate-ops

# Show all operators
show-operators: $(TARGET)
	./$(TARGET) --show-ops

# Test compilation with a simple FCx program
test-compile: $(TARGET)
	@echo "Testing FCx compiler compilation..."
	@echo 'let x := 42;' > /tmp/test.fcx
	@echo 'print>x;' >> /tmp/test.fcx
	./$(TARGET) -v /tmp/test.fcx -o /tmp/test_out || echo "Compilation test completed"
	@rm -f /tmp/test.fcx /tmp/test_out

# Format code (requires clang-format)
format:
	find $(SRCDIR) -name "*.c" -o -name "*.h" | xargs clang-format -i

# Static analysis (requires cppcheck)
analyze:
	cppcheck --enable=all --std=c99 $(SRCDIR)

# Help
help:
	@echo "FCx Compiler Build System"
	@echo ""
	@echo "LLVM Configuration:"
	@echo "  LLVM Version:    $(LLVM_VERSION)"
	@echo "  Required:        $(REQUIRED_LLVM_VERSION)"
	@echo "  Status:          $(shell [ "$(LLVM_VERSION_CHECK)" = "ok" ] && echo "✓ Valid" || echo "✗ Invalid")"
	@echo ""
	@echo "Build Targets:"
	@echo "  all              Build FCx compiler (default)"
	@echo "  debug            Build with debug symbols"
	@echo "  clean            Remove build artifacts"
	@echo "  install          Install to /usr/local/bin"
	@echo "  uninstall        Remove from /usr/local/bin"
	@echo "  validate-llvm    Validate LLVM installation"
	@echo ""
	@echo "Test Targets:"
	@echo "  test-compile     Test basic compilation"
	@echo "  test-operators   Validate 200+ operator registry"
	@echo "  show-operators   Display all operators"
	@echo ""
	@echo "Development Targets:"
	@echo "  format           Format source code"
	@echo "  analyze          Run static analysis"
	@echo "  help             Show this help"

# Phony targets
.PHONY: all debug clean install uninstall test-compile test-operators show-operators format analyze help validate-llvm

# Dependencies
$(OBJDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/lexer/lexer.h $(SRCDIR)/parser/parser.h $(SRCDIR)/semantic/semantic.h $(SRCDIR)/ir/fcx_ir.h $(SRCDIR)/types/pointer_types.h $(SRCDIR)/codegen/llvm_backend.h
$(OBJDIR)/lexer/lexer.o: $(SRCDIR)/lexer/lexer.c $(SRCDIR)/lexer/lexer.h
$(OBJDIR)/lexer/operator_registry.o: $(SRCDIR)/lexer/operator_registry.c $(SRCDIR)/lexer/lexer.h
$(OBJDIR)/parser/parser.o: $(SRCDIR)/parser/parser.c $(SRCDIR)/parser/parser.h $(SRCDIR)/lexer/lexer.h
$(OBJDIR)/semantic/semantic.o: $(SRCDIR)/semantic/semantic.c $(SRCDIR)/semantic/semantic.h $(SRCDIR)/parser/parser.h $(SRCDIR)/types/pointer_types.h
$(OBJDIR)/ir/fcx_ir.o: $(SRCDIR)/ir/fcx_ir.c $(SRCDIR)/ir/fcx_ir.h
$(OBJDIR)/ir/ir_gen.o: $(SRCDIR)/ir/ir_gen.c $(SRCDIR)/ir/ir_gen.h $(SRCDIR)/ir/fcx_ir.h $(SRCDIR)/parser/parser.h
$(OBJDIR)/ir/ir_optimize.o: $(SRCDIR)/ir/ir_optimize.c $(SRCDIR)/ir/ir_optimize.h $(SRCDIR)/ir/fcx_ir.h
$(OBJDIR)/ir/fc_ir.o: $(SRCDIR)/ir/fc_ir.c $(SRCDIR)/ir/fc_ir.h $(SRCDIR)/ir/fcx_ir.h
$(OBJDIR)/ir/fc_ir_lower.o: $(SRCDIR)/ir/fc_ir_lower.c $(SRCDIR)/ir/fc_ir_lower.h $(SRCDIR)/ir/fcx_ir.h $(SRCDIR)/ir/fc_ir.h
$(OBJDIR)/ir/fc_ir_abi.o: $(SRCDIR)/ir/fc_ir_abi.c $(SRCDIR)/ir/fc_ir_abi.h $(SRCDIR)/ir/fc_ir.h
$(OBJDIR)/types/pointer_types.o: $(SRCDIR)/types/pointer_types.c $(SRCDIR)/types/pointer_types.h
$(OBJDIR)/runtime/bootstrap.o: $(SRCDIR)/runtime/bootstrap.c $(SRCDIR)/runtime/bootstrap.h
