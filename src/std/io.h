// FCx Standard Library - I/O Header
// printf-style formatted output with escape sequence support

#ifndef FCX_STD_IO_H
#define FCX_STD_IO_H

// ============================================================================
// Low-level output
// ============================================================================

// Output a single character to stdout
fn putchar(c) {
    asm% {
        subq $8, %rsp
        movq ${c}, %rax
        movb %al, (%rsp)
        movq $1, %rax
        movq $1, %rdi
        leaq (%rsp), %rsi
        movq $1, %rdx
        syscall
        addq $8, %rsp
    }
    ret 0
}

// ============================================================================
// String utilities
// ============================================================================

// Calculate length of null-terminated string
fn strlen(s) {
    let len = 0
    let p = s
    loop {
        let c = @p & 255
        if c == 0 -> break
        len = len + 1
        p = p + 1
    }
    ret len
}

// ============================================================================
// Integer output helpers
// ============================================================================

// Print unsigned integer (internal helper)
fn _print_uint(n) {
    let val = n
    if val == 0 {
        putchar(48)
        ret 0
    }
    
    let div = val / 10
    if div > 0 {
        _print_uint(div)
    }
    putchar(48 + val - div * 10)
    ret 0
}

// Print signed integer
fn print_int(n) {
    let val = n
    if val < 0 {
        putchar(45)
        val = 0 - val
    }
    _print_uint(val)
    ret 0
}

// Print unsigned integer in hexadecimal
fn print_hex(n) {
    let val = n
    if val == 0 {
        putchar(48)
        ret 0
    }
    
    let div = val / 16
    if div > 0 {
        print_hex(div)
    }
    
    let digit = val - div * 16
    if digit < 10 {
        putchar(48 + digit)
    } else {
        putchar(97 + digit - 10)
    }
    ret 0
}

// Print unsigned integer in uppercase hexadecimal
fn print_HEX(n) {
    let val = n
    if val == 0 {
        putchar(48)
        ret 0
    }
    
    let div = val / 16
    if div > 0 {
        print_HEX(div)
    }
    
    let digit = val - div * 16
    if digit < 10 {
        putchar(48 + digit)
    } else {
        putchar(65 + digit - 10)
    }
    ret 0
}

// Print unsigned integer in binary
fn print_bin(n) {
    let val = n
    if val == 0 {
        putchar(48)
        ret 0
    }
    
    let div = val / 2
    if div > 0 {
        print_bin(div)
    }
    putchar(48 + val - div * 2)
    ret 0
}

// Print unsigned integer in octal
fn print_oct(n) {
    let val = n
    if val == 0 {
        putchar(48)
        ret 0
    }
    
    let div = val / 8
    if div > 0 {
        print_oct(div)
    }
    putchar(48 + val - div * 8)
    ret 0
}


// ============================================================================
// Core print function - handles escape sequences
// ============================================================================

// Print a string with escape sequence handling
// Supports: \n \t \r \\ \' \" \0
fn print(s) {
    let p = s
    loop {
        let c = @p & 255
        if c == 0 -> break
        
        if c == 92 {
            // Backslash - check next char for escape
            p = p + 1
            let next = @p & 255
            if next == 0 -> break
            
            if next == 110 {
                // \n - newline
                putchar(10)
            } else {
                if next == 116 {
                    // \t - tab
                    putchar(9)
                } else {
                    if next == 114 {
                        // \r - carriage return
                        putchar(13)
                    } else {
                        if next == 92 {
                            // \\ - backslash
                            putchar(92)
                        } else {
                            if next == 39 {
                                // \' - single quote
                                putchar(39)
                            } else {
                                if next == 34 {
                                    // \" - double quote
                                    putchar(34)
                                } else {
                                    if next == 48 {
                                        // \0 - null (skip)
                                        putchar(0)
                                    } else {
                                        // Unknown escape, print as-is
                                        putchar(92)
                                        putchar(next)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            putchar(c)
        }
        p = p + 1
    }
    ret 0
}

// Print string and newline
fn println(s) {
    print(s)
    putchar(10)
    ret 0
}

// Print just a newline
fn newline() {
    putchar(10)
    ret 0
}


// ============================================================================
// printf-style formatted output
// ============================================================================

// printf - formatted print with variadic arguments
// Format specifiers:
//   %d, %i  - signed decimal integer
//   %u      - unsigned decimal integer
//   %x      - hexadecimal (lowercase)
//   %X      - hexadecimal (uppercase)
//   %o      - octal
//   %b      - binary
//   %c      - character
//   %s      - string
//   %p      - pointer (hex with 0x prefix)
//   %%      - literal percent sign
//
// Usage: printf("Hello %s, you are %d years old\n", name, age)

fn printf(fmt, a1, a2, a3, a4, a5, a6, a7, a8) {
    let p = fmt
    let arg_idx = 0
    
    loop {
        let c = @p & 255
        if c == 0 -> break
        
        if c == 37 {
            // Percent sign - format specifier
            p = p + 1
            let spec = @p & 255
            if spec == 0 -> break
            
            if spec == 37 {
                // %% - literal percent
                putchar(37)
            } else {
                if spec == 100 {
                    // %d - signed decimal
                    if arg_idx == 0 { print_int(a1) }
                    if arg_idx == 1 { print_int(a2) }
                    if arg_idx == 2 { print_int(a3) }
                    if arg_idx == 3 { print_int(a4) }
                    if arg_idx == 4 { print_int(a5) }
                    if arg_idx == 5 { print_int(a6) }
                    if arg_idx == 6 { print_int(a7) }
                    if arg_idx == 7 { print_int(a8) }
                    arg_idx = arg_idx + 1
                } else {
                    if spec == 105 {
                        // %i - signed decimal (same as %d)
                        if arg_idx == 0 { print_int(a1) }
                        if arg_idx == 1 { print_int(a2) }
                        if arg_idx == 2 { print_int(a3) }
                        if arg_idx == 3 { print_int(a4) }
                        if arg_idx == 4 { print_int(a5) }
                        if arg_idx == 5 { print_int(a6) }
                        if arg_idx == 6 { print_int(a7) }
                        if arg_idx == 7 { print_int(a8) }
                        arg_idx = arg_idx + 1
                    } else {
                        if spec == 117 {
                            // %u - unsigned decimal
                            if arg_idx == 0 { _print_uint(a1) }
                            if arg_idx == 1 { _print_uint(a2) }
                            if arg_idx == 2 { _print_uint(a3) }
                            if arg_idx == 3 { _print_uint(a4) }
                            if arg_idx == 4 { _print_uint(a5) }
                            if arg_idx == 5 { _print_uint(a6) }
                            if arg_idx == 6 { _print_uint(a7) }
                            if arg_idx == 7 { _print_uint(a8) }
                            arg_idx = arg_idx + 1
                        } else {
                            if spec == 120 {
                                // %x - hex lowercase
                                if arg_idx == 0 { print_hex(a1) }
                                if arg_idx == 1 { print_hex(a2) }
                                if arg_idx == 2 { print_hex(a3) }
                                if arg_idx == 3 { print_hex(a4) }
                                if arg_idx == 4 { print_hex(a5) }
                                if arg_idx == 5 { print_hex(a6) }
                                if arg_idx == 6 { print_hex(a7) }
                                if arg_idx == 7 { print_hex(a8) }
                                arg_idx = arg_idx + 1
                            } else {
                                if spec == 88 {
                                    // %X - hex uppercase
                                    if arg_idx == 0 { print_HEX(a1) }
                                    if arg_idx == 1 { print_HEX(a2) }
                                    if arg_idx == 2 { print_HEX(a3) }
                                    if arg_idx == 3 { print_HEX(a4) }
                                    if arg_idx == 4 { print_HEX(a5) }
                                    if arg_idx == 5 { print_HEX(a6) }
                                    if arg_idx == 6 { print_HEX(a7) }
                                    if arg_idx == 7 { print_HEX(a8) }
                                    arg_idx = arg_idx + 1
                                } else {
                                    if spec == 111 {
                                        // %o - octal
                                        if arg_idx == 0 { print_oct(a1) }
                                        if arg_idx == 1 { print_oct(a2) }
                                        if arg_idx == 2 { print_oct(a3) }
                                        if arg_idx == 3 { print_oct(a4) }
                                        if arg_idx == 4 { print_oct(a5) }
                                        if arg_idx == 5 { print_oct(a6) }
                                        if arg_idx == 6 { print_oct(a7) }
                                        if arg_idx == 7 { print_oct(a8) }
                                        arg_idx = arg_idx + 1
                                    } else {
                                        if spec == 98 {
                                            // %b - binary
                                            if arg_idx == 0 { print_bin(a1) }
                                            if arg_idx == 1 { print_bin(a2) }
                                            if arg_idx == 2 { print_bin(a3) }
                                            if arg_idx == 3 { print_bin(a4) }
                                            if arg_idx == 4 { print_bin(a5) }
                                            if arg_idx == 5 { print_bin(a6) }
                                            if arg_idx == 6 { print_bin(a7) }
                                            if arg_idx == 7 { print_bin(a8) }
                                            arg_idx = arg_idx + 1
                                        } else {
                                            if spec == 99 {
                                                // %c - character
                                                if arg_idx == 0 { putchar(a1) }
                                                if arg_idx == 1 { putchar(a2) }
                                                if arg_idx == 2 { putchar(a3) }
                                                if arg_idx == 3 { putchar(a4) }
                                                if arg_idx == 4 { putchar(a5) }
                                                if arg_idx == 5 { putchar(a6) }
                                                if arg_idx == 6 { putchar(a7) }
                                                if arg_idx == 7 { putchar(a8) }
                                                arg_idx = arg_idx + 1
                                            } else {
                                                if spec == 115 {
                                                    // %s - string
                                                    if arg_idx == 0 { print(a1) }
                                                    if arg_idx == 1 { print(a2) }
                                                    if arg_idx == 2 { print(a3) }
                                                    if arg_idx == 3 { print(a4) }
                                                    if arg_idx == 4 { print(a5) }
                                                    if arg_idx == 5 { print(a6) }
                                                    if arg_idx == 6 { print(a7) }
                                                    if arg_idx == 7 { print(a8) }
                                                    arg_idx = arg_idx + 1
                                                } else {
                                                    if spec == 112 {
                                                        // %p - pointer
                                                        putchar(48)
                                                        putchar(120)
                                                        if arg_idx == 0 { print_hex(a1) }
                                                        if arg_idx == 1 { print_hex(a2) }
                                                        if arg_idx == 2 { print_hex(a3) }
                                                        if arg_idx == 3 { print_hex(a4) }
                                                        if arg_idx == 4 { print_hex(a5) }
                                                        if arg_idx == 5 { print_hex(a6) }
                                                        if arg_idx == 6 { print_hex(a7) }
                                                        if arg_idx == 7 { print_hex(a8) }
                                                        arg_idx = arg_idx + 1
                                                    } else {
                                                        // Unknown specifier, print as-is
                                                        putchar(37)
                                                        putchar(spec)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            if c == 92 {
                // Backslash - escape sequence
                p = p + 1
                let next = @p & 255
                if next == 0 -> break
                
                if next == 110 { putchar(10) }
                else {
                    if next == 116 { putchar(9) }
                    else {
                        if next == 114 { putchar(13) }
                        else {
                            if next == 92 { putchar(92) }
                            else {
                                if next == 48 { putchar(0) }
                                else {
                                    putchar(92)
                                    putchar(next)
                                }
                            }
                        }
                    }
                }
            } else {
                putchar(c)
            }
        }
        p = p + 1
    }
    ret 0
}

#endif // FCX_STD_IO_H
