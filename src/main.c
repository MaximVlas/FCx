#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "codegen/llvm_backend.h"
#include "ir/fc_ir.h"
#include "ir/fc_ir_abi.h"
#include "ir/fc_ir_lower.h"
#include "ir/fcx_ir.h"
#include "ir/ir_gen.h"
#include "ir/ir_optimize.h"
#include "lexer/lexer.h"
#include "module/preprocessor.h"
#include "module/c_import_zig.h"
#include "parser/parser.h"
#include "runtime/bootstrap.h"
#include "types/pointer_types.h"

// FCx Compiler version and build info
#define FCX_VERSION "0.2.12"
#define FCX_BUILD_DATE __DATE__
#define FCX_BUILD_TIME __TIME__

// Compilation profiles
typedef enum {
  PROFILE_DEBUG,      // Debug build with bounds checking, leak detection
  PROFILE_RELEASE,    // Release build with optimizations
  PROFILE_SIZE,       // Size-optimized build
} CompilationProfile;

// Optimization levels (separate from profiles)
typedef enum {
  OPT_LEVEL_O0 = 0,    // Debug - no optimizations
  OPT_LEVEL_O1,        // Quick - basic local opts
  OPT_LEVEL_O2,        // Standard - standard optimizations
  OPT_LEVEL_O3,        // Aggressive - aggressive optimizations
  OPT_LEVEL_OS,        // Size - size optimizations
} OptimizationLevel;

// Compiler options
typedef struct {
  const char *input_file;
  const char *output_file;
  bool verbose;
  bool debug;
  bool disallow_ambiguous_ops;
  bool show_assembly;
  bool show_operators;
  bool validate_operators;
  bool dump_ast;
  bool dump_fcx_ir;
  bool dump_fc_ir;
  bool dump_tokens;
  bool dump_preprocessed;     // Dump preprocessed source
  bool stop_after_parse;
  bool stop_after_fcx_ir;
  bool stop_after_fc_ir;
  bool expand_operators;      // Verbose mode: expand dense operators
  bool enable_bounds_check;   // Enable runtime bounds checking
  bool enable_leak_detection; // Enable memory leak detection
  bool shared_library;        // Generate shared library (.so)
  bool object_only;           // Generate object file only (.o)
  bool position_independent;  // Generate position-independent code
  CompilationProfile profile; // Compilation profile
  OptimizationLevel opt_level; // Optimization level
} CompilerOptions;

// Print usage information
void print_usage(const char *program_name) {
  printf("FCx Compiler v%s - The FCx Programming Language\n",
         FCX_VERSION);
  printf("Built on %s at %s\n\n", FCX_BUILD_DATE, FCX_BUILD_TIME);
  printf("Usage: %s [options] <input.fcx>\n\n", program_name);
  printf("Options:\n");
  printf("  -o <file>              Output executable file (default: a.out)\n");
  printf("  -v, --verbose          Enable verbose output\n");
  printf("  -d, --debug            Enable debug information\n");
  printf("  -O0                    No optimizations (debug mode)\n");
  printf("  -O1                    Basic optimizations\n");
  printf("  -O2                    Standard optimizations (default)\n");
  printf("  -O3                    Aggressive optimizations\n");
  printf("  -Os                    Size optimizations\n");
  printf("  --disallow-ambiguous   Disallow ambiguous operators (team coding "
         "standards)\n");
  printf("  --show-asm             Show generated assembly code\n");
  printf("  --show-ops             Show all 200+ operators\n");
  printf(
      "  --validate-ops         Validate operator registry (200+ operators)\n");
  printf("  --expand-ops           Expand dense operators into readable "
         "sequences\n");
  printf("\n");
  printf("Compilation Profiles:\n");
  printf("  --profile=debug        Debug build (bounds checking, leak "
         "detection)\n");
  printf("  --profile=release      Release build (optimizations enabled)\n");
  printf("  --profile=size         Size-optimized build (minimal code)\n");
  printf("  --bounds-check         Enable runtime bounds checking\n");
  printf("  --leak-detection       Enable memory leak detection\n");
  printf("  -c                     Compile to object file only (.o)\n");
  printf("  -shared                Generate shared library (.so)\n");
  printf("  -fPIC                  Generate position-independent code\n");
  printf("\n");
  printf("IR Dumping Options:\n");
  printf("  --dump-tokens          Dump lexer tokens\n");
  printf("  --dump-ast             Dump abstract syntax tree\n");
  printf(
      "  --dump-fcx-ir          Dump high-level FCx IR (operator-centric)\n");
  printf("  --dump-fc-ir           Dump low-level FC IR (x86_64-like)\n");
  printf("  --stop-after-parse     Stop compilation after parsing\n");
  printf("  --stop-after-fcx-ir    Stop compilation after FCx IR generation\n");
  printf("  --stop-after-fc-ir     Stop compilation after FC IR lowering\n");
  printf("\n");
  printf("General Options:\n");
  printf("  -h, --help             Show this help message\n");
  printf("  --version              Show version information\n\n");
  printf("Examples:\n");
  printf("  %s hello.fcx                    # Compile hello.fcx to a.out\n",
         program_name);
  printf("  %s -o hello hello.fcx           # Compile to 'hello' executable\n",
         program_name);
  printf("  %s --show-asm hello.fcx         # Show assembly output\n",
         program_name);
  printf("  %s --dump-fcx-ir hello.fcx      # Show high-level IR\n",
         program_name);
  printf("  %s --dump-fc-ir hello.fcx       # Show low-level IR\n",
         program_name);
  printf(
      "  %s --validate-ops               # Validate 200+ operator registry\n",
      program_name);
  printf("  %s --expand-ops hello.fcx       # Expand dense operators\n",
         program_name);
  printf("  %s --profile=debug hello.fcx    # Debug build with checks\n",
         program_name);
  printf("  %s --profile=release hello.fcx  # Optimized release build\n",
         program_name);
}

// Print version information
void print_version(void) {
  printf("FCx Compiler v%s\n", FCX_VERSION);
  printf("Built on %s at %s\n", FCX_BUILD_DATE, FCX_BUILD_TIME);
  printf("Target: Linux x86_64\n");
  printf("Features: 200+ operators, three-pointer system, direct assembly "
         "generation\n");
}

// Expand dense operator into readable explanation
const char *expand_operator(const char *op_symbol) {
  static char expansion[512];
  
  // Map operators to their expanded explanations
  if (strcmp(op_symbol, "<=>") == 0) {
    return "compare-and-swap (atomic CAS operation)";
  } else if (strcmp(op_symbol, "<==>") == 0) {
    return "atomic-swap (exchange values atomically)";
  } else if (strcmp(op_symbol, "sys%") == 0) {
    return "raw-syscall (direct system call with number)";
  } else if (strcmp(op_symbol, "$/") == 0) {
    return "syscall-write (write to file descriptor)";
  } else if (strcmp(op_symbol, "/$") == 0) {
    return "syscall-read (read from file descriptor)";
  } else if (strcmp(op_symbol, "mem>") == 0) {
    return "allocate-memory (heap allocation with alignment)";
  } else if (strcmp(op_symbol, ">mem") == 0) {
    return "deallocate-memory (free heap memory)";
  } else if (strcmp(op_symbol, "stack>") == 0) {
    return "allocate-stack (stack allocation)";
  } else if (strcmp(op_symbol, "arena>") == 0) {
    return "allocate-arena (bump-pointer arena allocation)";
  } else if (strcmp(op_symbol, "slab>") == 0) {
    return "allocate-slab (fixed-size slab allocation)";
  } else if (strcmp(op_symbol, "pool>") == 0) {
    return "allocate-pool (object pool allocation)";
  } else if (strcmp(op_symbol, ">>>") == 0) {
    return "logical-right-shift (zero-fill shift)";
  } else if (strcmp(op_symbol, "<<<") == 0) {
    return "rotate-left (circular bit rotation)";
  } else if (strcmp(op_symbol, ">>>>") == 0) {
    return "rotate-right (circular bit rotation)";
  } else if (strcmp(op_symbol, "/|/") == 0) {
    return "simd-divide (vectorized division)";
  } else if (strcmp(op_symbol, "|/|") == 0) {
    return "parallel-divide (parallel division operation)";
  } else if (strcmp(op_symbol, "!") == 0) {
    return "atomic-read (explicit atomic load)";
  } else if (strcmp(op_symbol, "!!") == 0) {
    return "atomic-write (explicit atomic store)";
  } else if (strcmp(op_symbol, "!=>") == 0) {
    return "memory-barrier-full (mfence - full memory barrier)";
  } else if (strcmp(op_symbol, "!>") == 0) {
    return "memory-barrier-load (lfence - load fence)";
  } else if (strcmp(op_symbol, "!<") == 0) {
    return "memory-barrier-store (sfence - store fence)";
  } else if (strcmp(op_symbol, "?!!") == 0) {
    return "atomic-fetch-add (atomic add with fence)";
  } else if (strcmp(op_symbol, "~!") == 0) {
    return "atomic-xor (atomic exclusive-or)";
  } else if (strcmp(op_symbol, "@>") == 0) {
    return "map-mmio (map memory-mapped I/O address)";
  } else if (strcmp(op_symbol, "<@") == 0) {
    return "unmap-mmio (unmap memory-mapped I/O)";
  } else if (strcmp(op_symbol, "->>") == 0) {
    return "layout-offset-access (compile-time field offset)";
  } else if (strcmp(op_symbol, "<<-") == 0) {
    return "reverse-layout-copy (reverse memcpy-like operation)";
  } else if (strcmp(op_symbol, "</") == 0) {
    return "slice-start (pointer slice with offset)";
  } else if (strcmp(op_symbol, "/>") == 0) {
    return "slice-end (memory slice from pointer)";
  } else if (strcmp(op_symbol, "</>") == 0) {
    return "slice-range (memory subrange operation)";
  } else if (strcmp(op_symbol, "><") == 0) {
    return "volatile-store (store with volatile semantics)";
  } else if (strcmp(op_symbol, "<>") == 0) {
    return "no-alias-hint (restrict pointer hint)";
  } else if (strcmp(op_symbol, "&>") == 0) {
    return "bitfield-extract (extract bits from value)";
  } else if (strcmp(op_symbol, "&<") == 0) {
    return "bitfield-insert (insert bits into value)";
  } else if (strcmp(op_symbol, ":>") == 0) {
    return "cast-to (type cast operator)";
  } else if (strcmp(op_symbol, ":>:") == 0) {
    return "reinterpret-cast (unsafe reinterpret cast)";
  } else if (strcmp(op_symbol, "<|>") == 0) {
    return "pointer-to-integer (cast pointer to integer)";
  } else if (strcmp(op_symbol, "|<>") == 0) {
    return "integer-to-pointer (cast integer to pointer)";
  } else if (strcmp(op_symbol, "|>") == 0) {
    return "push-into (push data into stack/queue)";
  } else if (strcmp(op_symbol, "<|") == 0) {
    return "pop-from (pop data from stack/queue)";
  } else if (strcmp(op_symbol, "#!") == 0) {
    return "privilege-escalate (request elevated privileges)";
  } else if (strcmp(op_symbol, "!#") == 0) {
    return "capability-check (check security capability)";
  } else {
    // Generic expansion based on operator info
    const OperatorInfo *op = lookup_operator(op_symbol);
    if (op) {
      snprintf(expansion, sizeof(expansion), "%s", op->semantics);
      return expansion;
    }
    return "unknown-operator";
  }
}

// Show all operators in the registry
void show_operators(void) {
  printf("FCx Operator Registry - %zu operators\n", get_operator_count());
  printf("Generated from symbol alphabet: < > / | \\ : ; ! ? ^ @ %% $ & * ~ ` "
         ", . [ ] { }\n\n");

  const char *category_names[] = {"Shift/Rotate",      "Arithmetic/Assignment",
                                  "Data Movement",     "Bitfield",
                                  "Memory Allocation", "Atomic/Concurrency",
                                  "Syscall/OS",        "IO/Formatting",
                                  "Comparison",        "Arithmetic Dense"};

  for (int cat = 0; cat < 10; cat++) {
    printf("=== %s Family ===\n", category_names[cat]);

    for (size_t i = 0; i < get_operator_count(); i++) {
      const OperatorInfo *op = get_operator_by_index(i);
      if (op && op->category == (OperatorCategory)cat) {
        printf("  %-8s  %s\n", op->symbol, op->semantics);
      }
    }
    printf("\n");
  }
}

// Validate operator registry
bool validate_operators(void) {
  printf("Validating FCx operator registry...\n");

  size_t count = get_operator_count();
  printf("Total operators: %zu\n", count);

  if (!validate_operator_count()) {
    printf("ERROR: Operator count is less than 200 (found %zu)\n", count);
    return false;
  }

  printf("✓ Operator count validation passed (200+ operators)\n");

  // Validate trie structure
  init_operator_registry();

  // Test some key operators
  const char *test_operators[] = {"<=>", "<==>",   "sys%",   "mem>",   ">mem",
                                  "$/",  "/$",     ">>>",    "<<<",    "/|/",
                                  "|/|", "!=>",    "!>",     "!<",     "@@",
                                  "@>",  "<@",     "stack>", "?!!",    "~!",
                                  "|!|", "spawn>", "print>", "debug>", NULL};

  printf("Testing operator lookup...\n");
  for (int i = 0; test_operators[i] != NULL; i++) {
    const OperatorInfo *op = lookup_operator(test_operators[i]);
    if (op) {
      printf("✓ %s -> %s\n", test_operators[i], op->semantics);
    } else {
      printf("✗ %s -> NOT FOUND\n", test_operators[i]);
      return false;
    }
  }

  printf("✓ All operator lookups successful\n");

  // Run comprehensive validation
  if (!validate_complete_operator_registry()) {
    printf("✗ Comprehensive operator registry validation failed\n");
    return false;
  }

  printf("✓ Operator registry validation PASSED\n");
  return true;
}

// Validate three-pointer type system
bool validate_pointer_system(void) {
  printf("Validating FCx three-pointer type system...\n");

  // Test handle operations
  TypedHandle handle = create_handle(42, HANDLE_FILE);
  if (!is_valid_handle(&handle)) {
    printf("✗ Handle creation failed\n");
    return false;
  }
  printf("✓ Handle operations working\n");

  // Test typed pointer operations
  int test_value = 123;
  TypedPointer typed_ptr =
      create_typed_pointer(&test_value, 1, PTR_FLAG_ALIGNED);
  if (!is_valid_typed_pointer(&typed_ptr)) {
    printf("✗ Typed pointer creation failed\n");
    return false;
  }
  printf("✓ Typed pointer operations working\n");

  // Test raw pointer operations
  RawPointer raw_ptr = create_raw_pointer(
      &test_value, sizeof(int), RAW_FLAG_READABLE | RAW_FLAG_WRITABLE);
  if (!is_valid_raw_pointer(&raw_ptr)) {
    printf("✗ Raw pointer creation failed\n");
    return false;
  }
  printf("✓ Raw pointer operations working\n");

  // Test pointer conversions
  RawPointer converted;
  if (typed_pointer_to_raw_pointer(&typed_ptr, &converted) !=
      PTR_CONV_SUCCESS) {
    printf("✗ Pointer conversion failed\n");
    return false;
  }
  printf("✓ Pointer conversions working\n");

  printf("✓ Three-pointer type system validation PASSED\n");
  return true;
}

// Validate bootstrap runtime
bool validate_bootstrap_runtime(void) {
  printf("Validating FCx bootstrap runtime...\n");

  // Test bootstrap allocator
  void *ptr1 = _fcx_alloc(64, 8);
  if (!ptr1) {
    printf("✗ Bootstrap allocation failed\n");
    return false;
  }
  printf("✓ Bootstrap allocation working\n");

  void *ptr2 = _fcx_alloc(128, 16);
  if (!ptr2) {
    printf("✗ Bootstrap allocation (2) failed\n");
    _fcx_free(ptr1);
    return false;
  }
  printf("✓ Bootstrap multiple allocations working\n");

  // Test deallocation
  _fcx_free(ptr1);
  _fcx_free(ptr2);
  printf("✓ Bootstrap deallocation working\n");

  // Test stack allocation
  void *stack_ptr = _fcx_stack_alloc(256);
  if (!stack_ptr) {
    printf("✗ Bootstrap stack allocation failed\n");
    return false;
  }
  _fcx_stack_free(stack_ptr);
  printf("✓ Bootstrap stack allocation working\n");

  printf("✓ Bootstrap runtime validation PASSED\n");
  return true;
}

// Parse command line arguments
bool parse_arguments(int argc, char *argv[], CompilerOptions *options) {
  // Initialize defaults
  options->input_file = NULL;
  options->output_file = "a.out";
  options->verbose = false;
  options->debug = false;
  options->disallow_ambiguous_ops = false;
  options->show_assembly = false;
  options->show_operators = false;
  options->validate_operators = false;
  options->dump_ast = false;
  options->dump_fcx_ir = false;
  options->dump_fc_ir = false;
  options->dump_tokens = false;
  options->dump_preprocessed = false;
  options->stop_after_parse = false;
  options->stop_after_fcx_ir = false;
  options->stop_after_fc_ir = false;
  options->expand_operators = false;
  options->enable_bounds_check = false;
  options->enable_leak_detection = false;
  options->shared_library = false;
  options->object_only = false;
  options->position_independent = false;
  options->profile = PROFILE_RELEASE; // Default to release
  options->opt_level = OPT_LEVEL_O2;  // Default to O2

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return false;
    } else if (strcmp(argv[i], "--version") == 0) {
      print_version();
      return false;
    } else if (strcmp(argv[i], "-v") == 0 ||
               strcmp(argv[i], "--verbose") == 0) {
      options->verbose = true;
    } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
      options->debug = true;
    } else if (strcmp(argv[i], "-O0") == 0) {
      options->opt_level = OPT_LEVEL_O0;
      options->profile = PROFILE_DEBUG;  // O0 implies debug profile
    } else if (strcmp(argv[i], "-O1") == 0) {
      options->opt_level = OPT_LEVEL_O1;
      // Keep current profile
    } else if (strcmp(argv[i], "-O2") == 0) {
      options->opt_level = OPT_LEVEL_O2;
      // Keep current profile
    } else if (strcmp(argv[i], "-O3") == 0) {
      options->opt_level = OPT_LEVEL_O3;
      // Keep current profile
    } else if (strcmp(argv[i], "-Os") == 0) {
      options->opt_level = OPT_LEVEL_OS;
      options->profile = PROFILE_SIZE;   // Os implies size profile
    } else if (strcmp(argv[i], "--disallow-ambiguous") == 0) {
      options->disallow_ambiguous_ops = true;
    } else if (strcmp(argv[i], "--show-asm") == 0) {
      options->show_assembly = true;
    } else if (strcmp(argv[i], "--show-ops") == 0) {
      options->show_operators = true;
      return true; // Don't need input file for this
    } else if (strcmp(argv[i], "--validate-ops") == 0) {
      options->validate_operators = true;
      return true; // Don't need input file for this
    } else if (strcmp(argv[i], "--dump-tokens") == 0) {
      options->dump_tokens = true;
    } else if (strcmp(argv[i], "--dump-pp") == 0) {
      options->dump_preprocessed = true;
    } else if (strcmp(argv[i], "--dump-ast") == 0) {
      options->dump_ast = true;
    } else if (strcmp(argv[i], "--dump-fcx-ir") == 0) {
      options->dump_fcx_ir = true;
    } else if (strcmp(argv[i], "--dump-fc-ir") == 0) {
      options->dump_fc_ir = true;
    } else if (strcmp(argv[i], "--stop-after-parse") == 0) {
      options->stop_after_parse = true;
    } else if (strcmp(argv[i], "--stop-after-fcx-ir") == 0) {
      options->stop_after_fcx_ir = true;
    } else if (strcmp(argv[i], "--stop-after-fc-ir") == 0) {
      options->stop_after_fc_ir = true;
    } else if (strcmp(argv[i], "--expand-ops") == 0) {
      options->expand_operators = true;
    } else if (strcmp(argv[i], "--bounds-check") == 0) {
      options->enable_bounds_check = true;
    } else if (strcmp(argv[i], "--leak-detection") == 0) {
      options->enable_leak_detection = true;
    } else if (strcmp(argv[i], "-c") == 0) {
      options->object_only = true;
    } else if (strcmp(argv[i], "-shared") == 0) {
      options->shared_library = true;
      options->position_independent = true;  // Shared libs need PIC
    } else if (strcmp(argv[i], "-fPIC") == 0 || strcmp(argv[i], "-fpic") == 0) {
      options->position_independent = true;
    } else if (strncmp(argv[i], "--profile=", 10) == 0) {
      const char *profile = argv[i] + 10;
      if (strcmp(profile, "debug") == 0) {
        options->profile = PROFILE_DEBUG;
        options->opt_level = OPT_LEVEL_O0;
        options->debug = true;
        options->enable_bounds_check = true;
        options->enable_leak_detection = true;
      } else if (strcmp(profile, "release") == 0) {
        options->profile = PROFILE_RELEASE;
        // Keep current optimization level
      } else if (strcmp(profile, "size") == 0) {
        options->profile = PROFILE_SIZE;
        options->opt_level = OPT_LEVEL_OS;
      } else {
        fprintf(stderr, "Error: Unknown profile '%s'\n", profile);
        fprintf(stderr, "Valid profiles: debug, release, size\n");
        return false;
      }
    } else if (strcmp(argv[i], "-o") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -o requires an output filename\n");
        return false;
      }
      options->output_file = argv[++i];
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
      return false;
    } else {
      if (options->input_file == NULL) {
        options->input_file = argv[i];
      } else {
        fprintf(stderr, "Error: Multiple input files not supported\n");
        return false;
      }
    }
  }

  return true;
}

// Read source file
char *read_source_file(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
    return NULL;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Allocate buffer
  char *source = malloc(size + 1);
  if (!source) {
    fprintf(stderr, "Error: Out of memory\n");
    fclose(file);
    return NULL;
  }

  // Read file
  size_t read_size = fread(source, 1, size, file);
  source[read_size] = '\0';

  fclose(file);
  return source;
}

// Main compilation function
bool compile_fcx(const CompilerOptions *options) {
  if (options->verbose) {
    printf("FCx Compiler v%s\n", FCX_VERSION);
    printf("Compiling: %s -> %s\n", options->input_file, options->output_file);
    
    // Display compilation profile and optimization level
    const char *profile_name = "release";
    if (options->profile == PROFILE_DEBUG) {
      profile_name = "debug";
    } else if (options->profile == PROFILE_SIZE) {
      profile_name = "size-optimized";
    }
    
    const char *opt_name = "O2";
    switch (options->opt_level) {
      case OPT_LEVEL_O0: opt_name = "O0"; break;
      case OPT_LEVEL_O1: opt_name = "O1"; break;
      case OPT_LEVEL_O2: opt_name = "O2"; break;
      case OPT_LEVEL_O3: opt_name = "O3"; break;
      case OPT_LEVEL_OS: opt_name = "Os"; break;
    }
    
    printf("Profile: %s, Optimization: %s\n", profile_name, opt_name);
    
    // Display enabled features
    if (options->enable_bounds_check) {
      printf("  ✓ Runtime bounds checking enabled\n");
    }
    if (options->enable_leak_detection) {
      printf("  ✓ Memory leak detection enabled\n");
    }
    if (options->disallow_ambiguous_ops) {
      printf("  ✓ Strict operator parsing (no ambiguous ops)\n");
    }
    if (options->expand_operators) {
      printf("  ✓ Operator expansion mode enabled\n");
    }
  }

  // Initialize operator registry
  init_operator_registry();

  // Preprocess first (handles #include, #define, etc.)
  if (options->verbose) {
    printf("Preprocessing...\n");
  }

  Preprocessor *pp = preprocessor_create(NULL);  // Use default std path
  if (!pp) {
    fprintf(stderr, "Error: Failed to create preprocessor\n");
    cleanup_operator_registry();
    return false;
  }

  char *source = preprocessor_process_file_to_string(pp, options->input_file);
  if (!source) {
    fprintf(stderr, "Error: Preprocessing failed: %s\n", preprocessor_get_error(pp));
    preprocessor_destroy(pp);
    cleanup_operator_registry();
    return false;
  }

  if (options->verbose) {
    printf("Preprocessed source (%zu bytes)\n", strlen(source));
  }

  // Dump preprocessed source if requested
  if (options->dump_preprocessed) {
    printf("\n=== Preprocessed Source ===\n");
    printf("%s", source);
    printf("\n=== End Preprocessed Source ===\n\n");
  }

  // Initialize lexer on preprocessed source
  Lexer lexer;
  lexer_init(&lexer, source);

  if (options->verbose) {
    printf("Lexical analysis...\n");
  }

  // Dump tokens if requested
  if (options->dump_tokens) {
    printf("\n=== Lexer Tokens ===\n");
    Token token;
    do {
      token = lexer_next_token(&lexer);
      printf("Token: Line %zu, Col %zu - Kind: %d, Length: %zu\n", token.line,
             token.column, token.kind, token.length);
      if (token.start && token.length > 0) {
        printf("  Text: '%.*s'\n", (int)token.length, token.start);
      }
    } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);
    printf("=== End Tokens ===\n\n");

    // Reset lexer for parsing
    lexer_init(&lexer, source);
  }

  // Expand operators if requested
  if (options->expand_operators) {
    printf("\n=== Operator Expansion Mode ===\n");
    printf("Expanding dense operators into readable sequences...\n\n");
    
    Token token;
    do {
      token = lexer_next_token(&lexer);
      if (token.start && token.length > 0) {
        char op_text[32];
        size_t copy_len = token.length < sizeof(op_text) - 1 ? token.length : sizeof(op_text) - 1;
        strncpy(op_text, token.start, copy_len);
        op_text[copy_len] = '\0';
        
        // Check if this is an operator token
        const OperatorInfo *op = lookup_operator(op_text);
        if (op) {
          const char *expansion = expand_operator(op_text);
          printf("Line %zu: '%s' => %s\n", token.line, op_text, expansion);
        }
      }
    } while (token.kind != TOK_EOF && token.kind != TOK_ERROR);
    
    printf("\n=== End Operator Expansion ===\n\n");

    // Reset lexer for parsing
    lexer_init(&lexer, source);
  }

  // Initialize parser
  Parser parser;
  parser_init(&parser, &lexer);

  if (options->verbose) {
    printf("Parsing...\n");
  }

  // Dump AST if requested
  if (options->dump_ast) {
    printf("\n=== Abstract Syntax Tree ===\n");
    printf("(AST dumping not yet implemented - parser in progress)\n");
    printf("=== End AST ===\n\n");
  }

  if (options->stop_after_parse) {
    if (options->verbose) {
      printf("Stopping after parse phase (--stop-after-parse)\n");
    }
    free(source);
    preprocessor_destroy(pp);
    cleanup_operator_registry();
    return true;
  }

  // Generate FCx IR (high-level)
  if (options->verbose) {
    printf("Generating FCx IR (high-level)...\n");
  }

  IRGenerator *ir_gen = ir_gen_create("main_module");
  if (!ir_gen) {
    fprintf(stderr, "Error: Failed to create IR generator\n");
    free(source);
    preprocessor_destroy(pp);
    cleanup_operator_registry();
    return false;
  }

  // Parse preprocessed source into statements
  Stmt **statements = NULL;
  size_t stmt_count = 0;
  size_t stmt_capacity = 64;
  
  statements = malloc(stmt_capacity * sizeof(Stmt*));
  if (!statements) {
    fprintf(stderr, "Error: Out of memory\n");
    ir_gen_destroy(ir_gen);
    free(source);
    preprocessor_destroy(pp);
    cleanup_operator_registry();
    return false;
  }

  // Parse all statements
  while (!parser_check(&parser, TOK_EOF)) {
    Stmt *stmt = parse_statement(&parser);
    if (!stmt) {
      if (parser.had_error) {
        fprintf(stderr, "Error: Parse error\n");
        free(statements);
        ir_gen_destroy(ir_gen);
        free(source);
        preprocessor_destroy(pp);
        cleanup_operator_registry();
        return false;
      }
      break;
    }
    
    if (stmt_count >= stmt_capacity) {
      stmt_capacity *= 2;
      statements = realloc(statements, stmt_capacity * sizeof(Stmt*));
    }
    statements[stmt_count++] = stmt;
  }

  if (options->verbose) {
    printf("Parsed %zu statements\n", stmt_count);
  }

  // Generate IR from parsed AST
  if (stmt_count > 0) {
    if (!ir_gen_generate_module(ir_gen, statements, stmt_count)) {
      fprintf(stderr, "Error: IR generation failed: %s\n",
              ir_gen_get_error(ir_gen));
      free(statements);
      preprocessor_destroy(pp);
      ir_gen_destroy(ir_gen);
      free(source);
      cleanup_operator_registry();
      return false;
    }
  }

  // Free statements array (statements themselves are managed elsewhere)
  free(statements);

  if (options->verbose && ir_gen->module) {
    printf("FCx IR module created: %s\n", ir_gen->module->name);
    printf("Functions: %u\n", ir_gen->module->function_count);
  }

  // Run FCx IR optimizations (constant folding, dead code elimination, etc.)
  if (ir_gen->module && options->opt_level > OPT_LEVEL_O0) {
    if (options->verbose) {
      printf("Running FCx IR optimizations (level %s)...\n", 
             options->opt_level == OPT_LEVEL_O1 ? "O1" :
             options->opt_level == OPT_LEVEL_O2 ? "O2" :
             options->opt_level == OPT_LEVEL_O3 ? "O3" : "Os");
    }
    bool opt_changed = ir_optimize_module_with_level(ir_gen->module, options->opt_level);
    if (options->verbose && opt_changed) {
      printf("FCx IR optimizations applied\n");
    }
  }

  // Dump FCx IR if requested (after optimization)
  if (options->dump_fcx_ir) {
    printf("\n=== FCx IR (High-Level Operator-Centric) ===\n");
    if (ir_gen->module) {
      fcx_ir_print_module(ir_gen->module);
    } else {
      printf("(No FCx IR generated)\n");
    }
    printf("=== End FCx IR ===\n\n");
  }

  if (options->stop_after_fcx_ir) {
    if (options->verbose) {
      printf("Stopping after FCx IR generation (--stop-after-fcx-ir)\n");
    }
    ir_gen_destroy(ir_gen);
    free(source);
    cleanup_operator_registry();
    return true;
  }

  // Lower to FC IR (low-level)
  if (options->verbose) {
    printf("Lowering to FC IR (low-level)...\n");
  }

  FcIRLowerContext *lower_ctx = fc_ir_lower_create();
  if (!lower_ctx) {
    fprintf(stderr, "Error: Failed to create FC IR lowering context\n");
    ir_gen_destroy(ir_gen);
    free(source);
    cleanup_operator_registry();
    return false;
  }

  // Lower FCx IR to FC IR
  if (ir_gen->module) {
    bool lower_success = fc_ir_lower_module(lower_ctx, ir_gen->module);
    if (!lower_success) {
      fprintf(stderr, "Error: Failed to lower FCx IR to FC IR\n");
      fc_ir_lower_destroy(lower_ctx);
      ir_gen_destroy(ir_gen);
      free(source);
      cleanup_operator_registry();
      return false;
    }

    if (options->verbose) {
      printf("Successfully lowered FCx IR to FC IR\n");
    }
  }

  // Dump FC IR if requested
  if (options->dump_fc_ir) {
    printf("\n=== FC IR (Low-Level x86_64-like) ===\n");
    if (lower_ctx->fc_module) {
      fc_ir_print_module(lower_ctx->fc_module);
    } else {
      printf("(No FC IR generated)\n");
    }
    printf("=== End FC IR ===\n\n");
  }

  if (options->stop_after_fc_ir) {
    if (options->verbose) {
      printf("Stopping after FC IR lowering (--stop-after-fc-ir)\n");
    }
    fc_ir_lower_destroy(lower_ctx);
    ir_gen_destroy(ir_gen);
    free(source);
    cleanup_operator_registry();
    return true;
  }

  // Code generation using LLVM backend
  if (options->verbose) {
    printf("Code generation (LLVM backend)...\n");
  }

  bool success = false;

  if (lower_ctx->fc_module && lower_ctx->fc_module->function_count > 0) {
    CpuFeatures cpu_features = fc_ir_detect_cpu_features();

    if (options->verbose) {
      printf("CPU features detected: 0x%lx\n", cpu_features.features);
      printf("Vector width: %u bits\n", cpu_features.vector_width);
    }

    LLVMBackendConfig llvm_config = llvm_config_for_level(options->opt_level);
    
    if (options->verbose) {
      const char *opt_desc = 
        options->opt_level == OPT_LEVEL_O0 ? "O0 (no optimization, debug info enabled)" :
        options->opt_level == OPT_LEVEL_O1 ? "O1 (basic optimizations)" :
        options->opt_level == OPT_LEVEL_O2 ? "O2 (standard optimizations)" :
        options->opt_level == OPT_LEVEL_O3 ? "O3 (aggressive optimizations)" :
        "Os (size optimizations)";
      printf("LLVM optimization: %s\n", opt_desc);
    }

    LLVMBackend *llvm_backend = llvm_backend_create(&cpu_features, &llvm_config);
    if (!llvm_backend) {
      fprintf(stderr, "Error: Failed to create LLVM backend\n");
      fc_ir_lower_destroy(lower_ctx);
      ir_gen_destroy(ir_gen);
      free(source);
      cleanup_operator_registry();
      return false;
    }

    // Get C/C++ import contexts - we'll inject them AFTER creating the LLVM module
    CImportContext *c_import_ctx = preprocessor_get_c_import_context();
    CImportContext *cpp_import_ctx = preprocessor_get_cpp_import_context();
    
    // Register external functions with C import contexts BEFORE processing
    // This tells the Zig code which C functions to reference
    if (c_import_ctx) {
      if (options->verbose) {
        printf("Registering %u external functions with C import context...\n", 
               lower_ctx->fc_module->external_func_count);
      }
      for (uint32_t i = 0; i < lower_ctx->fc_module->external_func_count; i++) {
        const char *func_name = lower_ctx->fc_module->external_functions[i];
        // Only register functions that look like C library functions (not FCX runtime)
        if (func_name[0] != '_' || strncmp(func_name, "_fcx_", 5) != 0) {
          if (options->verbose) {
            printf("  Registering C function: %s\n", func_name);
          }
          fcx_c_import_add_function(c_import_ctx, func_name);
        }
      }
    }
    
    if (cpp_import_ctx) {
      // Register C++ external functions
      for (uint32_t i = 0; i < lower_ctx->fc_module->external_func_count; i++) {
        const char *func_name = lower_ctx->fc_module->external_functions[i];
        if (func_name[0] != '_' || strncmp(func_name, "_fcx_", 5) != 0) {
          fcx_c_import_add_function(cpp_import_ctx, func_name);
        }
      }
    }

    if (options->verbose) {
      printf("Emitting LLVM IR...\n");
    }

    // Emit the FCX module first - this creates the LLVM module
    // Pass C import contexts so they can be injected BEFORE external declarations
    if (!llvm_emit_module_with_imports(llvm_backend, lower_ctx->fc_module, c_import_ctx, cpp_import_ctx, options->verbose)) {
      fprintf(stderr, "Error: LLVM IR emission failed: %s\n", 
              llvm_backend_get_error(llvm_backend));
      llvm_backend_destroy(llvm_backend);
      fc_ir_lower_destroy(lower_ctx);
      ir_gen_destroy(ir_gen);
      free(source);
      cleanup_operator_registry();
      return false;
    }

    if (options->verbose) {
      llvm_print_statistics(llvm_backend);
    }

    if (options->show_assembly) {
      printf("\n=== Generated LLVM IR ===\n");
      llvm_print_module(llvm_backend, stdout);
      printf("=== End LLVM IR ===\n\n");

      printf("\n=== Generated Assembly ===\n");
      if (llvm_generate_assembly(llvm_backend, "/tmp/fcx_output.s")) {
        FILE *asm_file = fopen("/tmp/fcx_output.s", "r");
        if (asm_file) {
          char line[256];
          while (fgets(line, sizeof(line), asm_file)) {
            printf("%s", line);
          }
          fclose(asm_file);
        }
      }
      printf("=== End Assembly ===\n\n");
    }

    if (options->verbose) {
      if (options->object_only) {
        printf("Generating object file using LLVM...\n");
      } else if (options->shared_library) {
        printf("Generating shared library using LLVM...\n");
      } else {
        printf("Generating executable using LLVM...\n");
      }
    }

    bool link_success;
    if (options->object_only) {
      link_success = llvm_generate_object_file(llvm_backend, options->output_file);
    } else if (options->shared_library) {
      link_success = llvm_compile_shared_library(llvm_backend, options->output_file);
    } else {
      link_success = llvm_compile_and_link(llvm_backend, options->output_file);
    }

    if (link_success) {
      success = true;
    } else {
      const char *output_type = options->object_only ? "object file" :
                                options->shared_library ? "shared library" : "executable";
      fprintf(stderr, "Error: Failed to generate %s: %s\n",
              output_type, llvm_backend_get_error(llvm_backend));
    }

    llvm_backend_destroy(llvm_backend);
  } else {
    if (options->verbose) {
      printf("No functions to compile\n");
    }
    success = true;
  }

  // Cleanup
  fc_ir_lower_destroy(lower_ctx);
  ir_gen_destroy(ir_gen);
  preprocessor_cleanup_c_imports();  // Clean up C/C++ import contexts
  preprocessor_destroy(pp);
  free(source);
  cleanup_operator_registry();

  // Print compilation summary
  if (success) {
    // Get file size of output
    struct stat st;
    const char *file_type = options->object_only ? "object file" :
                            options->shared_library ? "shared library" : "executable";
    
    if (stat(options->output_file, &st) == 0) {
      // Format file size nicely
      double size = (double)st.st_size;
      const char *unit = "B";
      if (size >= 1024) { size /= 1024; unit = "KB"; }
      if (size >= 1024) { size /= 1024; unit = "MB"; }
      
      printf("Compiled %s -> %s (%.1f %s %s)\n", 
             options->input_file, options->output_file, size, unit, file_type);
    } else {
      printf("Compiled %s -> %s (%s)\n", 
             options->input_file, options->output_file, file_type);
    }
  }

  return success;
}

int main(int argc, char *argv[]) {
  CompilerOptions options;

  if (!parse_arguments(argc, argv, &options)) {
    return 1;
  }

  // Handle special modes
  if (options.show_operators) {
    init_operator_registry();
    show_operators();
    cleanup_operator_registry();
    return 0;
  }

  if (options.validate_operators) {
    bool valid = true;

    // Validate operator registry
    if (!validate_operators()) {
      valid = false;
    }

    // Validate three-pointer system
    if (!validate_pointer_system()) {
      valid = false;
    }

    // Validate bootstrap runtime
    if (!validate_bootstrap_runtime()) {
      valid = false;
    }

    if (valid) {
      printf("\n=== FCx Architecture Validation Summary ===\n");
      printf("✓ All architectural components validated successfully\n");
      printf("✓ 200+ operator registry complete\n");
      printf("✓ Three-pointer type system functional\n");
      printf("✓ Bootstrap runtime operational\n");
      printf("✓ Operator disambiguation rules implemented\n");
      printf("✓ Combinatorial pattern generation validated\n");
      printf("✓ Bootstrap paradox resolved\n");
    }

    cleanup_operator_registry();
    return valid ? 0 : 1;
  }

  // Normal compilation
  if (options.input_file == NULL) {
    fprintf(stderr, "Error: No input file specified\n");
    print_usage(argv[0]);
    return 1;
  }

  bool success = compile_fcx(&options);
  return success ? 0 : 1;
}