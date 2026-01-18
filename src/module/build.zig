const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Build c_import as a static library with C ABI
    const lib = b.addStaticLibrary(.{
        .name = "fcx_c_import",
        .root_source_file = b.path("c_import.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Export C header
    lib.installHeader(b.path("c_import_zig.h"), "c_import_zig.h");

    b.installArtifact(lib);

    // Tests
    const unit_tests = b.addTest(.{
        .root_source_file = b.path("c_import.zig"),
        .target = target,
        .optimize = optimize,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}
