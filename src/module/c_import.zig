// FCx C Import Bridge - Uses Zig's translate-c to generate LLVM IR
// The whole point: Zig already does C parsing -> LLVM IR, we just use it

const std = @import("std");
const builtin = @import("builtin");

var gpa = std.heap.GeneralPurposeAllocator(.{}){};

const ImportContext = struct {
    allocator: std.mem.Allocator,
    headers: std.ArrayListUnmanaged([]const u8),
    include_paths: std.ArrayListUnmanaged([]const u8),
    link_libs: std.ArrayListUnmanaged([]const u8),
    referenced_functions: std.ArrayListUnmanaged([]const u8),
    llvm_ir: ?[]u8,
    object_code: ?[]u8,
    had_error: bool,
    error_msg: [1024]u8,
    error_len: usize,
};

// ============================================================================
// C ABI Exports - Called from FCX compiler
// ============================================================================

export fn fcx_c_import_create() ?*anyopaque {
    const allocator = gpa.allocator();
    const ctx = allocator.create(ImportContext) catch return null;
    ctx.* = .{
        .allocator = allocator,
        .headers = .{},
        .include_paths = .{},
        .link_libs = .{},
        .referenced_functions = .{},
        .llvm_ir = null,
        .object_code = null,
        .had_error = false,
        .error_msg = undefined,
        .error_len = 0,
    };
    return ctx;
}

export fn fcx_c_import_destroy(ctx_ptr: ?*anyopaque) void {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        ctx.headers.deinit(ctx.allocator);
        ctx.include_paths.deinit(ctx.allocator);
        ctx.link_libs.deinit(ctx.allocator);
        ctx.referenced_functions.deinit(ctx.allocator);
        if (ctx.llvm_ir) |ir| ctx.allocator.free(ir);
        if (ctx.object_code) |obj| ctx.allocator.free(obj);
        ctx.allocator.destroy(ctx);
    }
}

export fn fcx_c_import_add_include_path(ctx_ptr: ?*anyopaque, path: [*:0]const u8) bool {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        const path_copy = ctx.allocator.dupe(u8, std.mem.span(path)) catch return false;
        ctx.include_paths.append(ctx.allocator, path_copy) catch {
            ctx.allocator.free(path_copy);
            return false;
        };
        return true;
    }
    return false;
}

export fn fcx_c_import_add_link_lib(ctx_ptr: ?*anyopaque, lib: [*:0]const u8) bool {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        const lib_copy = ctx.allocator.dupe(u8, std.mem.span(lib)) catch return false;
        ctx.link_libs.append(ctx.allocator, lib_copy) catch {
            ctx.allocator.free(lib_copy);
            return false;
        };
        return true;
    }
    return false;
}

export fn fcx_c_import_header(ctx_ptr: ?*anyopaque, header: [*:0]const u8, is_system: bool) bool {
    _ = is_system;
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        const header_copy = ctx.allocator.dupe(u8, std.mem.span(header)) catch return false;
        ctx.headers.append(ctx.allocator, header_copy) catch {
            ctx.allocator.free(header_copy);
            return false;
        };
        return true;
    }
    return false;
}

export fn fcx_c_import_add_function(ctx_ptr: ?*anyopaque, func_name: [*:0]const u8) bool {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        const name_copy = ctx.allocator.dupe(u8, std.mem.span(func_name)) catch return false;
        ctx.referenced_functions.append(ctx.allocator, name_copy) catch {
            ctx.allocator.free(name_copy);
            return false;
        };
        return true;
    }
    return false;
}

fn setError(ctx: *ImportContext, msg: []const u8) void {
    ctx.had_error = true;
    const len = @min(msg.len, ctx.error_msg.len - 1);
    @memcpy(ctx.error_msg[0..len], msg[0..len]);
    ctx.error_msg[len] = 0;
    ctx.error_len = len;
}

export fn fcx_c_import_process(ctx_ptr: ?*anyopaque) bool {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        return processWithClang(ctx);
    }
    return false;
}

fn processWithClang(ctx: *ImportContext) bool {
    if (ctx.headers.items.len == 0) {
        return true;
    }

    const tmp_path = "/tmp/fcx_c_import.c";
    const tmp_file = std.fs.cwd().createFile(tmp_path, .{}) catch {
        setError(ctx, "Failed to create temp file");
        return false;
    };
    defer tmp_file.close();

    var buffer: [8192]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buffer);
    const writer = fbs.writer();

    // Write includes
    for (ctx.headers.items) |header| {
        writer.writeAll("#include <") catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
        writer.writeAll(header) catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
        writer.writeAll(">\n") catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
    }

    // Generate a dummy function that references all the C functions we need
    // This forces clang to emit their declarations in the LLVM IR
    if (ctx.referenced_functions.items.len > 0) {
        writer.writeAll("\nvoid __fcx_force_declarations(void) {\n") catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
        
        for (ctx.referenced_functions.items) |func_name| {
            // Generate a reference to the function (cast to void* to avoid type issues)
            writer.writeAll("    (void)&") catch {
                setError(ctx, "Failed to write temp file");
                return false;
            };
            writer.writeAll(func_name) catch {
                setError(ctx, "Failed to write temp file");
                return false;
            };
            writer.writeAll(";\n") catch {
                setError(ctx, "Failed to write temp file");
                return false;
            };
        }
        
        writer.writeAll("}\n") catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
    }

    tmp_file.writeAll(fbs.getWritten()) catch {
        setError(ctx, "Failed to write temp file");
        return false;
    };

    var args: std.ArrayList([]const u8) = .empty;
    defer args.deinit(ctx.allocator);

    args.append(ctx.allocator, "clang") catch return false;
    args.append(ctx.allocator, "-S") catch return false;
    args.append(ctx.allocator, "-emit-llvm") catch return false;
    args.append(ctx.allocator, "-o") catch return false;
    args.append(ctx.allocator, "/tmp/fcx_c_import.ll") catch return false;

    for (ctx.include_paths.items) |inc| {
        args.append(ctx.allocator, "-I") catch return false;
        args.append(ctx.allocator, inc) catch return false;
    }

    args.append(ctx.allocator, tmp_path) catch return false;

    var child = std.process.Child.init(args.items, ctx.allocator);
    child.stderr_behavior = .Pipe;
    
    const env_map = ctx.allocator.create(std.process.EnvMap) catch {
        setError(ctx, "Failed to allocate environment map");
        return false;
    };
    defer ctx.allocator.destroy(env_map);
    
    env_map.* = std.process.getEnvMap(ctx.allocator) catch {
        setError(ctx, "Failed to get environment");
        return false;
    };
    defer env_map.deinit();
    
    child.env_map = env_map;

    child.spawn() catch {
        setError(ctx, "Failed to spawn clang");
        return false;
    };

    const result = child.wait() catch {
        setError(ctx, "Failed to wait for clang");
        return false;
    };

    if (result != .Exited or result.Exited != 0) {
        setError(ctx, "clang failed to compile");
        return false;
    }

    const ll_file = std.fs.cwd().openFile("/tmp/fcx_c_import.ll", .{}) catch {
        setError(ctx, "Failed to open generated LLVM IR");
        return false;
    };
    defer ll_file.close();

    ctx.llvm_ir = ll_file.readToEndAlloc(ctx.allocator, 100 * 1024 * 1024) catch {
        setError(ctx, "Failed to read LLVM IR");
        return false;
    };

    return true;
}

export fn fcx_c_import_get_llvm_ir(ctx_ptr: ?*anyopaque) ?[*:0]const u8 {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        if (ctx.llvm_ir) |ir| {
            const result = ctx.allocator.allocSentinel(u8, ir.len, 0) catch return null;
            @memcpy(result[0..ir.len], ir);
            return result.ptr;
        }
    }
    return null;
}

export fn fcx_c_import_get_llvm_ir_size(ctx_ptr: ?*anyopaque) usize {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        if (ctx.llvm_ir) |ir| {
            return ir.len;
        }
    }
    return 0;
}

export fn fcx_c_import_get_error(ctx_ptr: ?*anyopaque) [*:0]const u8 {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        return @ptrCast(&ctx.error_msg);
    }
    return "No context";
}

export fn fcx_c_import_had_error(ctx_ptr: ?*anyopaque) bool {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        return ctx.had_error;
    }
    return true;
}

export fn fcx_c_import_compile_to_object(ctx_ptr: ?*anyopaque, output_path: [*:0]const u8) bool {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        return compileToObject(ctx, std.mem.span(output_path));
    }
    return false;
}

fn compileToObject(ctx: *ImportContext, output_path: []const u8) bool {
    if (ctx.headers.items.len == 0) {
        return true;
    }

    const tmp_path = "/tmp/fcx_c_import.c";
    const tmp_file = std.fs.cwd().createFile(tmp_path, .{}) catch {
        setError(ctx, "Failed to create temp file");
        return false;
    };
    defer tmp_file.close();

    var buffer: [4096]u8 = undefined;
    var fbs = std.io.fixedBufferStream(&buffer);
    const writer = fbs.writer();

    for (ctx.headers.items) |header| {
        writer.writeAll("#include <") catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
        writer.writeAll(header) catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
        writer.writeAll(">\n") catch {
            setError(ctx, "Failed to write temp file");
            return false;
        };
    }

    tmp_file.writeAll(fbs.getWritten()) catch {
        setError(ctx, "Failed to write temp file");
        return false;
    };

    var args: std.ArrayList([]const u8) = .empty;
    defer args.deinit(ctx.allocator);

    args.append(ctx.allocator, "clang") catch return false;
    args.append(ctx.allocator, "-c") catch return false;
    args.append(ctx.allocator, "-o") catch return false;
    
    const out_copy = ctx.allocator.dupeZ(u8, output_path) catch return false;
    defer ctx.allocator.free(out_copy);
    args.append(ctx.allocator, out_copy) catch return false;

    for (ctx.include_paths.items) |inc| {
        args.append(ctx.allocator, "-I") catch return false;
        args.append(ctx.allocator, inc) catch return false;
    }

    args.append(ctx.allocator, tmp_path) catch return false;

    var child = std.process.Child.init(args.items, ctx.allocator);
    
    const env_map = ctx.allocator.create(std.process.EnvMap) catch {
        setError(ctx, "Failed to allocate environment map");
        return false;
    };
    defer ctx.allocator.destroy(env_map);
    
    env_map.* = std.process.getEnvMap(ctx.allocator) catch {
        setError(ctx, "Failed to get environment");
        return false;
    };
    defer env_map.deinit();
    
    child.env_map = env_map;
    
    child.spawn() catch {
        setError(ctx, "Failed to spawn clang");
        return false;
    };

    const result = child.wait() catch {
        setError(ctx, "Failed to wait for clang");
        return false;
    };

    return result == .Exited and result.Exited == 0;
}

export fn fcx_c_import_get_link_lib_count(ctx_ptr: ?*anyopaque) usize {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        return ctx.link_libs.items.len;
    }
    return 0;
}

export fn fcx_c_import_get_link_lib(ctx_ptr: ?*anyopaque, index: usize) ?[*:0]const u8 {
    if (ctx_ptr) |ptr| {
        const ctx: *ImportContext = @ptrCast(@alignCast(ptr));
        if (index < ctx.link_libs.items.len) {
            const lib = ctx.link_libs.items[index];
            const result = ctx.allocator.dupeZ(u8, lib) catch return null;
            return result.ptr;
        }
    }
    return null;
}

// Dummy exports to maintain ABI compatibility
export fn fcx_c_import_get_function_count(_: ?*anyopaque) usize {
    return 0;
}

export fn fcx_c_import_get_function(_: ?*anyopaque, _: usize) ?*anyopaque {
    return null;
}

export fn fcx_c_import_find_function(_: ?*anyopaque, _: [*:0]const u8) ?*anyopaque {
    return null;
}

export fn fcx_c_import_find_struct(_: ?*anyopaque, _: [*:0]const u8) ?*anyopaque {
    return null;
}

export fn fcx_c_import_generate_llvm_decls(_: ?*anyopaque) ?[*:0]u8 {
    return null;
}

export fn fcx_c_import_free_string(_: ?[*]u8) void {}