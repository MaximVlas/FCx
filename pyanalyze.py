#!/usr/bin/env python3
"""
FCx Project Code Analyzer
Analyzes C, H, and FCx source files in the project.
"""

import os
from collections import defaultdict

def count_lines_and_chars(file_path):
    """Count lines and characters in a file."""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
            content = file.read()
            lines = len(content.splitlines())
            chars = len(content)
        return lines, chars
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return 0, 0

def analyze_folder(folder_path, extension, exclude_dirs=None):
    """Analyze all files with given extension in folder."""
    if exclude_dirs is None:
        exclude_dirs = set()
    
    results = {}
    if not os.path.exists(folder_path):
        return results
    
    for root, dirs, files in os.walk(folder_path):
        # Skip excluded directories
        dirs[:] = [d for d in dirs if d not in exclude_dirs]
        
        for file in files:
            if file.endswith(extension):
                file_path = os.path.join(root, file)
                lines, chars = count_lines_and_chars(file_path)
                results[file_path] = {'lines': lines, 'characters': chars}
    return results

def sort_by_directory(results):
    """Sort results by directory structure, then by filename."""
    def sort_key(item):
        path = item[0]
        parts = path.split(os.sep)
        # Sort by: directory depth, directory name, filename
        return (len(parts), os.path.dirname(path), os.path.basename(path))
    
    return sorted(results.items(), key=sort_key)

def print_section(title, results, show_by_dir=True):
    """Print a section of results."""
    print(f"{title}:")
    
    total_lines = 0
    total_chars = 0
    
    if show_by_dir:
        # Group by directory
        by_dir = defaultdict(list)
        for file_path, stats in results.items():
            dir_path = os.path.dirname(file_path)
            by_dir[dir_path].append((file_path, stats))
        
        # Sort directories
        for dir_path in sorted(by_dir.keys()):
            files = sorted(by_dir[dir_path], key=lambda x: x[0])
            for file_path, stats in files:
                print(f"{file_path}: {stats['lines']} lines, {stats['characters']} characters")
                total_lines += stats['lines']
                total_chars += stats['characters']
    else:
        for file_path, stats in sort_by_directory(results):
            print(f"{file_path}: {stats['lines']} lines, {stats['characters']} characters")
            total_lines += stats['lines']
            total_chars += stats['characters']
    
    return total_lines, total_chars

def main():
    exclude = {'obj', 'bin', '.git', '__pycache__', 'node_modules', 'debugs'}
    
    # ========== C Source Files in src/ ==========
    src_c_results = analyze_folder('src', '.c', exclude)
    if src_c_results:
        total_lines, total_chars = print_section("C code in src folder", src_c_results)
        print(f"\nTotal C code: {total_lines} lines, {total_chars} characters")
    
    # ========== H Header Files in src/ ==========
    src_h_results = analyze_folder('src', '.h', exclude)
    if src_h_results:
        print()
        total_lines, total_chars = print_section("H code in src folder", src_h_results)
        print(f"\nTotal H code: {total_lines} lines, {total_chars} characters")
    
    # ========== Test C Files (root level) ==========
    test_c_results = {}
    for f in os.listdir('.'):
        if f.endswith('.c') and os.path.isfile(f):
            lines, chars = count_lines_and_chars(f)
            test_c_results[f] = {'lines': lines, 'characters': chars}
    
    if test_c_results:
        print()
        total_lines, total_chars = print_section("Test C files (root)", test_c_results)
        print(f"\nTotal test C code: {total_lines} lines, {total_chars} characters")
    
    # ========== FCx Standard Library ==========
    std_fcx_results = analyze_folder('fcx-code/std', '.fcx', exclude)
    if std_fcx_results:
        print()
        total_lines, total_chars = print_section("FCx standard library (fcx-code/std)", std_fcx_results)
        print(f"\nTotal std FCx code: {total_lines} lines, {total_chars} characters")
    
    # ========== FCx Tests ==========
    test_fcx_results = analyze_folder('fcx-code/tests', '.fcx', exclude)
    if test_fcx_results:
        print()
        total_lines, total_chars = print_section("FCx tests (fcx-code/tests)", test_fcx_results)
        print(f"\nTotal test FCx code: {total_lines} lines, {total_chars} characters")
    
    # ========== FCx Examples ==========
    example_fcx_results = analyze_folder('fcx-code/examples', '.fcx', exclude)
    if example_fcx_results:
        print()
        total_lines, total_chars = print_section("FCx examples (fcx-code/examples)", example_fcx_results)
        print(f"\nTotal example FCx code: {total_lines} lines, {total_chars} characters")
    
    # ========== FCx Programs ==========
    program_fcx_results = analyze_folder('fcx-code/programs', '.fcx', exclude)
    if program_fcx_results:
        print()
        total_lines, total_chars = print_section("FCx programs (fcx-code/programs)", program_fcx_results)
        print(f"\nTotal program FCx code: {total_lines} lines, {total_chars} characters")
    
    # ========== Root FCx Files ==========
    root_fcx_results = {}
    for f in os.listdir('.'):
        if f.endswith('.fcx') and os.path.isfile(f):
            lines, chars = count_lines_and_chars(f)
            root_fcx_results[f] = {'lines': lines, 'characters': chars}
    
    if root_fcx_results:
        print()
        total_lines, total_chars = print_section("FCx files (root)", root_fcx_results)
        print(f"\nTotal root FCx code: {total_lines} lines, {total_chars} characters")
    
    # ========== Grand Total ==========
    print("\n" + "=" * 60)
    print("TOTAL")
    print("=" * 60)
    
    all_c = {**src_c_results, **test_c_results}
    all_h = src_h_results
    all_fcx = {**std_fcx_results, **test_fcx_results, **example_fcx_results, 
               **program_fcx_results, **root_fcx_results}
    
    c_lines = sum(s['lines'] for s in all_c.values())
    c_chars = sum(s['characters'] for s in all_c.values())
    h_lines = sum(s['lines'] for s in all_h.values())
    h_chars = sum(s['characters'] for s in all_h.values())
    fcx_lines = sum(s['lines'] for s in all_fcx.values())
    fcx_chars = sum(s['characters'] for s in all_fcx.values())
    
    print(f"C files:   {len(all_c):4} files, {c_lines:6} lines, {c_chars:8} characters")
    print(f"H files:   {len(all_h):4} files, {h_lines:6} lines, {h_chars:8} characters")
    print(f"FCx files: {len(all_fcx):4} files, {fcx_lines:6} lines, {fcx_chars:8} characters")
    print("-" * 60)
    total_files = len(all_c) + len(all_h) + len(all_fcx)
    total_lines = c_lines + h_lines + fcx_lines
    total_chars = c_chars + h_chars + fcx_chars
    print(f"TOTAL:     {total_files:4} files, {total_lines:6} lines, {total_chars:8} characters")

if __name__ == "__main__":
    main()
