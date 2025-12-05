#!/usr/bin/env python3

import os
import sys
import shutil
import subprocess
import datetime
from pathlib import Path
from typing import List, Optional, Tuple
import argparse

IMAGE_NAME = "Cervus"
VERSION = "v0.0.1"
QEMUFLAGS = "-m 2G"

BASE_DIR = Path.cwd()
KERNEL_DIR = BASE_DIR / "kernel"
SRC_DIR = KERNEL_DIR / "src"
LINKER_SCRIPTS_DIR = KERNEL_DIR / "linker-scripts"
LINKER_SCRIPT = LINKER_SCRIPTS_DIR / "x86_64.lds"
BIN_DIR = BASE_DIR / "bin"
OBJ_DIR = BASE_DIR / "obj"
ISO_ROOT = BASE_DIR / "iso_root"
DEMO_ISO_DIR = BASE_DIR / "demo_iso"
LIMINE_TOOLS_DIR = BASE_DIR / "limine-tools"

LIBC_DIR = BASE_DIR / "libc"
LIBC_INCLUDE_DIR = LIBC_DIR / "include"
LIBC_SRC_DIR = LIBC_DIR / "src"

DEPENDENCIES = {
    "freestnd-c-hdrs": {
        "url": "https://codeberg.org/OSDev/freestnd-c-hdrs-0bsd.git",
        "commit": "5df91dd7062ad0c54f5ffd86193bb9f008677631"
    },
    "cc-runtime": {
        "url": "https://codeberg.org/OSDev/cc-runtime.git",
        "commit": "dae79833b57a01b9fd3e359ee31def69f5ae899b"
    },
    "limine-protocol": {
        "url": "https://codeberg.org/Limine/limine-protocol.git",
        "commit": "c4616df2572d77c60020bdefa617dd9bdcc6566a"
    }
}

class Colors:
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BLUE = '\033[94m'
    END = '\033[0m'
    BOLD = '\033[1m'
    CYAN = '\033[96m'

def print_color(color: str, message: str):
    print(f"{color}{message}{Colors.END}")

def print_help():
    help_text = f"""
{Colors.BOLD}{Colors.GREEN}OS Build Script{Colors.END}
{Colors.BOLD}================{Colors.END}

{Colors.BOLD}Usage:{Colors.END}
  python build.py [command] [options]

{Colors.BOLD}Commands:{Colors.END}
  {Colors.GREEN}run{Colors.END}      - Build and run in QEMU (auto-rebuild if needed, cleans obj/bin after exit)
  {Colors.GREEN}clean{Colors.END}    - Full cleanup (removes all build files and dependencies)
  {Colors.GREEN}cleaniso{Colors.END} - Clean demo_iso directory only
  {Colors.GREEN}gitclean{Colors.END} - Remove all generated files (demo_iso, limine.conf, limine-tools, limine, linker-scripts)

{Colors.BOLD}Options:{Colors.END}
  {Colors.BLUE}--tree{Colors.END}    - Generate OS-TREE.txt with directory structure and file contents
  {Colors.BLUE}--help{Colors.END}    - Show this help message
  {Colors.BLUE}--no-clean{Colors.END} - Don't clean obj/bin after QEMU exits (use with run command)
  {Colors.BLUE}--files{Colors.END}   - Specific files to include in tree
  {Colors.BLUE}--structure-only{Colors.END} - Generate tree structure only (no file contents)

{Colors.BOLD}Examples:{Colors.END}
  python build.py run
  python build.py clean
  python build.py cleaniso
  python build.py gitclean
  python build.py --tree
  python build.py run --tree
  python build.py run --no-clean
  python build.py --tree --files kernel/src/main.c
  python build.py --tree --structure-only
    """
    print(help_text)

def generate_tree_structure(output_file: str = "OS-TREE.txt", 
                           specific_files: List[str] = None,
                           structure_only: bool = False):
    """
    Генерирует структуру директорий и содержимое файлов в текстовом файле
    
    Args:
        output_file: Имя выходного файла
        specific_files: Список конкретных файлов для включения (None для всех)
        structure_only: Только структура без содержимого файлов
    """
    
    print_color(Colors.GREEN, f"Generating OS tree structure to {output_file}...")
    
    ignore_dirs = {'.git', '__pycache__', 'obj', 'bin', 'iso_root', 'demo_iso'}
    ignore_extensions = {'.o', '.elf', '.iso', '.bin', '.sys', '.efi'}
    
    specific_paths = None
    if specific_files:
        specific_paths = []
        for file_path in specific_files:
            path = Path(file_path)
            if not path.is_absolute():
                path = BASE_DIR / path
            specific_paths.append(path)
    
    def should_include(path: Path) -> bool:
        """Определяет, нужно ли включать файл/директорию в вывод"""
        if path.is_dir():
            if path.name in ignore_dirs:
                return False
            return True
        else:
            if path.suffix in ignore_extensions:
                return False
            
            if specific_paths:
                for spec_path in specific_paths:
                    if path == spec_path:
                        return True
                return False
            
            return True
    
    def get_file_contents(path: Path, indent: str = "") -> str:
        """Получает содержимое файла с форматированием"""
        try:
            content = path.read_text(encoding='utf-8', errors='ignore')
            lines = content.split('\n')
            
            max_lines = 100
            if len(lines) > max_lines:
                lines = lines[:max_lines]
                lines.append(f"... [truncated, total {len(content.splitlines())} lines]")
            
            result = f"\n{indent}Contents:\n"
            for i, line in enumerate(lines, 1):
                result += f"{indent}{i:3d}: {line}\n"
            return result
        except Exception as e:
            return f"\n{indent}[Error reading file: {e}]\n"
    
    def traverse_directory(root: Path, indent: str = "") -> str:
        """Рекурсивно обходит директорию и генерирует структуру"""
        result = ""
        
        try:
            items = sorted(root.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower()))
        except PermissionError:
            return f"{indent}[Permission denied: {root.name}]\n"
        
        for item in items:
            if not should_include(item):
                continue
            
            if item.is_dir():
                result += f"{indent}├── {item.name}/\n"
                result += traverse_directory(item, indent + "│   ")
            else:
                result += f"{indent}├── {item.name}"
                
                try:
                    size = item.stat().st_size
                    if size < 1024:
                        size_str = f"{size} bytes"
                    elif size < 1024 * 1024:
                        size_str = f"{size/1024:.1f} KB"
                    else:
                        size_str = f"{size/(1024*1024):.1f} MB"
                    result += f" ({size_str})"
                except:
                    result += " (size unknown)"
                
                result += "\n"
                
                if not structure_only and item.is_file():
                    text_extensions = {'.c', '.h', '.asm', '.S', '.lds', '.conf', 
                                     '.txt', '.md', '.py', '.sh', '.json', '.yml', '.yaml'}
                    if item.suffix in text_extensions:
                        result += get_file_contents(item, indent + "│   ")
        
        return result
    
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(f"OS Tree Structure - {IMAGE_NAME} {VERSION}\n")
        f.write(f"Generated: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("=" * 80 + "\n\n")
        
        f.write("Project Information:\n")
        f.write(f"- Name: {IMAGE_NAME}\n")
        f.write(f"- Version: {VERSION}\n")
        f.write(f"- Base Directory: {BASE_DIR}\n")
        f.write(f"- Files included: {'Specific files' if specific_files else 'All files'}\n")
        f.write(f"- Show contents: {'No' if structure_only else 'Yes'}\n\n")
        
        structure = traverse_directory(BASE_DIR)
        f.write("Directory Structure:\n")
        f.write(".\n")
        f.write(structure)
        
        f.write("\n" + "=" * 80 + "\n")
        f.write("Statistics:\n")
        
        file_types = {}
        total_size = 0
        
        def count_files(root: Path):
            nonlocal file_types, total_size
            for item in root.iterdir():
                if should_include(item):
                    if item.is_dir():
                        count_files(item)
                    else:
                        ext = item.suffix
                        if ext not in file_types:
                            file_types[ext] = 0
                        file_types[ext] += 1
                        
                        try:
                            total_size += item.stat().st_size
                        except:
                            pass
        
        count_files(BASE_DIR)
        
        f.write(f"- Total files: {sum(file_types.values())}\n")
        f.write(f"- Total size: {total_size/1024:.1f} KB\n")
        f.write("- Files by type:\n")
        for ext, count in sorted(file_types.items()):
            if ext:
                f.write(f"  {ext}: {count}\n")
            else:
                f.write(f"  [no extension]: {count}\n")
    
    print_color(Colors.GREEN, f"Tree structure saved to {output_file}")
    return True

def run_command(cmd: List[str], cwd: Path = None, check: bool = True, capture: bool = False) -> Tuple[bool, str]:
    """Запускает команду и возвращает результат"""
    if capture:
        print_color(Colors.BLUE, f"Running: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, cwd=cwd, check=False, 
                               capture_output=True, text=True, encoding='utf-8')
        
        if not capture and result.stdout:
            print_color(Colors.CYAN, result.stdout.strip())
        
        if result.stderr:
            if result.returncode != 0:
                print_color(Colors.RED, result.stderr.strip())
            elif not capture:  
                print_color(Colors.YELLOW, result.stderr.strip())
        
        if check and result.returncode != 0:
            return False, result.stderr
        
        return True, result.stdout if capture else ""
    except Exception as e:
        print_color(Colors.RED, f"Command failed: {e}")
        return False, str(e)

def run_qemu_and_clean(clean_after: bool = True):
    """Запускает QEMU и очищает obj/bin после завершения"""
    print_color(Colors.GREEN, "Starting QEMU...")
    
    latest_iso = DEMO_ISO_DIR / f"{IMAGE_NAME}.latest.iso"
    if not latest_iso.exists():
        iso_files = list(DEMO_ISO_DIR.glob("*.iso"))
        if not iso_files:
            print_color(Colors.RED, "No ISO found. Building first...")
            if not build_all():
                return False
            latest_iso = DEMO_ISO_DIR / f"{IMAGE_NAME}.latest.iso"
        else:
            latest_iso = iso_files[-1]
    
    qemu_cmd = [
        "qemu-system-x86_64", "-M", "q35",
        "-cdrom", str(latest_iso),
        "-boot", "d",
        "-serial", "stdio"
    ]
    
    if QEMUFLAGS:
        qemu_cmd += QEMUFLAGS.split()
    
    print_color(Colors.BLUE, f"Running: {' '.join(qemu_cmd)}")
    print_color(Colors.YELLOW, "Press Ctrl+C to exit QEMU and clean up")
    
    try:
        qemu_process = subprocess.Popen(qemu_cmd)
        qemu_process.wait()
        print_color(Colors.GREEN, "QEMU has exited")
        
    except KeyboardInterrupt:
        print_color(Colors.YELLOW, "\nQEMU interrupted by user")
        if qemu_process:
            qemu_process.terminate()
            try:
                qemu_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu_process.kill()
    
    finally:
        if clean_after:
            print_color(Colors.GREEN, "Cleaning obj and bin directories...")
            cleanup_build_dirs()
    
    return True

def cleanup_build_dirs():
    """Очищает obj и bin директории"""
    dirs_to_clean = [OBJ_DIR, BIN_DIR]
    
    for dir_path in dirs_to_clean:
        if dir_path.exists():
            print_color(Colors.BLUE, f"Cleaning {dir_path}")
            try:
                shutil.rmtree(dir_path)
                print_color(Colors.GREEN, f"  {dir_path} cleaned")
            except Exception as e:
                print_color(Colors.RED, f"  Failed to clean {dir_path}: {e}")
        else:
            print_color(Colors.YELLOW, f"  {dir_path} doesn't exist")
    
    return True

def ensure_linker_scripts():
    print_color(Colors.GREEN, "Checking linker scripts...")
    
    LINKER_SCRIPTS_DIR.mkdir(parents=True, exist_ok=True)
    
    if not LINKER_SCRIPT.exists():
        print_color(Colors.YELLOW, "Creating x86_64.lds...")
        
        linker_content = """OUTPUT_FORMAT(elf64-x86-64)

ENTRY(kernel_main)

PHDRS
{
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
"""
        LINKER_SCRIPT.write_text(linker_content)
        print_color(Colors.GREEN, "x86_64.lds created")
    
    return True

def check_limine_tools():
    if not LIMINE_TOOLS_DIR.exists():
        print_color(Colors.YELLOW, "limine-tools directory not found")
        return False
    
    for dep_name in DEPENDENCIES:
        dep_dir = LIMINE_TOOLS_DIR / dep_name
        if not dep_dir.exists():
            print_color(Colors.YELLOW, f"Dependency {dep_name} not found in limine-tools")
            return False
        
        git_dir = dep_dir / ".git"
        if not git_dir.exists():
            print_color(Colors.YELLOW, f"Dependency {dep_name} is not a git repository")
            return False
    
    return True

def setup_missing_dependencies():
    print_color(Colors.GREEN, "Checking dependencies...")
    
    LIMINE_TOOLS_DIR.mkdir(exist_ok=True)
    
    setup_needed = False
    for dep_name, dep_info in DEPENDENCIES.items():
        dep_dir = LIMINE_TOOLS_DIR / dep_name
        if not dep_dir.exists():
            print_color(Colors.YELLOW, f"Missing {dep_name}, setting up...")
            setup_needed = True
            
            success, _ = run_command(["git", "clone", dep_info["url"], str(dep_dir)], capture=True)
            if not success:
                return False
            
            success, _ = run_command(["git", "-c", "advice.detachedHead=false", 
                              "checkout", dep_info["commit"]], cwd=dep_dir, capture=True)
            if not success:
                return False
    
    if setup_needed:
        print_color(Colors.GREEN, "Missing dependencies installed")
    else:
        print_color(Colors.GREEN, "All dependencies already exist")
    
    return True

def check_limine():
    limine_dir = BASE_DIR / "limine"
    if not limine_dir.exists():
        print_color(Colors.YELLOW, "Limine directory not found")
        return False
    
    limine_binary = limine_dir / "limine"
    if not limine_binary.exists():
        print_color(Colors.YELLOW, "Limine binary not found")
        return False
    
    return True

def build_limine():
    limine_dir = BASE_DIR / "limine"
    
    if limine_dir.exists():
        limine_binary = limine_dir / "limine"
        if limine_binary.exists():
            print_color(Colors.GREEN, "Limine already built")
            return True
        else:
            print_color(Colors.YELLOW, "Limine directory exists but binary not found, rebuilding...")
            shutil.rmtree(limine_dir)
    
    print_color(Colors.GREEN, "Building Limine...")
    
    success, _ = run_command(["git", "clone", "https://codeberg.org/Limine/Limine.git", 
                       "limine", "--branch=v10.x-binary", "--depth=1"], capture=True)
    if not success:
        return False
    
    success, _ = run_command(["make"], cwd=limine_dir, capture=True)
    return success

def ensure_limine_conf():
    limine_conf = BASE_DIR / "limine.conf"
    if not limine_conf.exists():
        print_color(Colors.YELLOW, "Creating limine.conf...")
        content = f"""# Timeout in seconds
timeout: 3

# Entry name
/{IMAGE_NAME} {VERSION}
    protocol: limine
    path: boot():/boot/kernel
"""
        limine_conf.write_text(content)
        print_color(Colors.GREEN, "limine.conf created")
    
    return True

def find_all_source_files() -> List[Path]:
    """Находит все исходные файлы, включая ядро и libc"""
    source_files = []
    
    if KERNEL_DIR.exists():
        kernel_src_dir = KERNEL_DIR / "src"
        if kernel_src_dir.exists():
            for root, _, files in os.walk(kernel_src_dir):
                if '.git' in root:
                    continue
                
                for file in files:
                    if file.endswith(('.c', '.S', '.asm', '.psf')):
                        source_files.append(Path(root) / file)
    
    if LIBC_SRC_DIR.exists():
        for root, _, files in os.walk(LIBC_SRC_DIR):
            for file in files:
                if file.endswith('.c'):
                    source_files.append(Path(root) / file)
    
    return source_files

def get_source_file_info(src_file: Path) -> Tuple[str, str, Path]:
    """Возвращает информацию об исходном файле для генерации имени объектного файла"""
    if src_file.is_relative_to(LIBC_SRC_DIR):
        rel_path = src_file.relative_to(LIBC_SRC_DIR)
        category = "libc"
    elif src_file.is_relative_to(KERNEL_DIR):
        rel_path = src_file.relative_to(KERNEL_DIR)
        category = "kernel"
    else:
        rel_path = src_file.relative_to(BASE_DIR)
        category = "other"
    
    if src_file.suffix == '.psf':
        obj_name = f"{category}_{src_file.stem}"
    else:
        obj_name = f"{category}_{str(rel_path).replace(os.sep, '_').replace('/', '_').replace('.', '_')}"
    
    if category == "libc":
        obj_dir = OBJ_DIR / "libc"
    elif category == "kernel":
        obj_dir = OBJ_DIR / "kernel"
    else:
        obj_dir = OBJ_DIR / "other"
    
    obj_file = obj_dir / f"{obj_name}.o"
    
    return obj_name, category, obj_file

def compile_binary_file(src_file: Path, obj_file: Path) -> bool:
    """Компилирует бинарный файл (например, .psf) в объектный файл"""
    print_color(Colors.BLUE, f"Converting binary: {src_file.name}")
    
    obj_file.parent.mkdir(parents=True, exist_ok=True)
    
    temp_name = f"temp_{src_file.name}"
    temp_path = BASE_DIR / temp_name
    
    try:
        shutil.copy2(src_file, temp_path)
        
        cmd = [
            "objcopy",
            "-I", "binary",
            "-O", "elf64-x86-64",
            "-B", "i386:x86-64",
            "--rename-section", ".data=.rodata,alloc,load,readonly,data,contents",
            str(temp_path),
            str(obj_file)
        ]
        
        success, output = run_command(cmd, capture=True)
        
        if success:
            print_color(Colors.GREEN, f"  Binary converted to {obj_file.name}")
            
            base_name = src_file.name  
            
            check_cmd = ["nm", "--defined-only", str(obj_file)]
            success2, symbols = run_command(check_cmd, capture=True)
            
            if success2 and symbols:
                print_color(Colors.BLUE, f"  Original symbols:")
                for line in symbols.strip().split('\n'):
                    if line:
                        print_color(Colors.BLUE, f"    {line}")

                symbol_base = src_file.stem  
                
                rename_commands = [
                    ("_binary_temp_font_psf_start", f"_binary_{symbol_base}_psf_start"),
                    ("_binary_temp_font_psf_end", f"_binary_{symbol_base}_psf_end"),
                    ("_binary_temp_font_psf_size", f"_binary_{symbol_base}_psf_size"),
                    ("_binary_font_start", f"_binary_{symbol_base}_psf_start"),
                    ("_binary_font_end", f"_binary_{symbol_base}_psf_end"),
                    ("_binary_font_size", f"_binary_{symbol_base}_psf_size"),
                ]
                
                for line in symbols.strip().split('\n'):
                    if line and 'binary' in line:
                        parts = line.split()
                        if len(parts) >= 3:
                            actual_symbol = parts[2]
                            if actual_symbol.endswith('_start'):
                                rename_cmd = [
                                    "objcopy",
                                    "--redefine-sym",
                                    f"{actual_symbol}=_binary_{symbol_base}_psf_start",
                                    str(obj_file)
                                ]
                            elif actual_symbol.endswith('_end'):
                                rename_cmd = [
                                    "objcopy",
                                    "--redefine-sym",
                                    f"{actual_symbol}=_binary_{symbol_base}_psf_end",
                                    str(obj_file)
                                ]
                            elif actual_symbol.endswith('_size'):
                                rename_cmd = [
                                    "objcopy",
                                    "--redefine-sym",
                                    f"{actual_symbol}=_binary_{symbol_base}_psf_size",
                                    str(obj_file)
                                ]
                            else:
                                continue
                            
                            run_command(rename_cmd, capture=True)
                            print_color(Colors.CYAN, f"    Renamed: {actual_symbol} -> {rename_cmd[2].split('=')[1]}")
                
                success3, final_symbols = run_command(check_cmd, capture=True)
                if success3 and final_symbols:
                    print_color(Colors.GREEN, f"  Final symbols:")
                    for line in final_symbols.strip().split('\n'):
                        if line:
                            print_color(Colors.GREEN, f"    {line}")
                else:
                    print_color(Colors.YELLOW, f"  Warning: Could not verify final symbols")
            else:
                print_color(Colors.YELLOW, f"  Warning: Could not read symbols from {obj_file.name}")
        
        return success
        
    except Exception as e:
        print_color(Colors.RED, f"  Error compiling binary: {e}")
        return False
        
    finally:
        if temp_path.exists():
            temp_path.unlink()

def compile_source_file(src_file: Path, obj_file: Path, cflags: str, cppflags: str) -> bool:
    """Компилирует исходный файл (C или ассемблер)"""
    obj_file.parent.mkdir(parents=True, exist_ok=True)
    
    if src_file.suffix == '.c':
        cmd = ["gcc"] + cflags.split() + cppflags.split() + \
              ["-c", str(src_file), "-o", str(obj_file)]
    elif src_file.suffix == '.S':
        cmd = ["gcc"] + cflags.split() + cppflags.split() + \
              ["-c", str(src_file), "-o", str(obj_file)]
    elif src_file.suffix == '.asm':
        cmd = ["nasm", "-g", "-F", "dwarf", "-f", "elf64", str(src_file), 
              "-o", str(obj_file)]
    else:
        return False
    
    print_color(Colors.BLUE, f"Compiling: {src_file.name}")
    
    success, error_output = run_command(cmd, capture=False)
    
    if success:
        print_color(Colors.GREEN, f"  ✓ Successfully compiled: {src_file.name}")
    else:
        print_color(Colors.RED, f"  ✗ Failed to compile: {src_file.name}")
    
    return success

def compile_kernel():
    print_color(Colors.GREEN, "Compiling kernel...")
    
    if not ensure_linker_scripts():
        return False
    
    if not check_limine_tools():
        print_color(Colors.RED, "limine-tools not found or incomplete")
        return False
    
    if not LIBC_INCLUDE_DIR.exists():
        print_color(Colors.YELLOW, f"libc include directory not found: {LIBC_INCLUDE_DIR}")
        print_color(Colors.YELLOW, "Creating minimal libc structure...")
        LIBC_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)
    
    if not LIBC_SRC_DIR.exists():
        print_color(Colors.YELLOW, f"libc source directory not found: {LIBC_SRC_DIR}")
        LIBC_SRC_DIR.mkdir(parents=True, exist_ok=True)
    
    cflags = "-g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding " \
             "-fno-stack-protector -fno-stack-check -fno-lto -fno-PIC " \
             "-ffunction-sections -fdata-sections -m64 -march=x86-64 " \
             "-mabi=sysv -mno-80387 -mno-mmx -mno-sse -mno-sse2 " \
             "-mno-red-zone -mcmodel=kernel"
    
    cppflags = f"-I {KERNEL_DIR / 'src'} " \
               f"-I {LIBC_INCLUDE_DIR} " \
               f"-I {LIMINE_TOOLS_DIR / 'limine-protocol/include'} " \
               f"-isystem {LIMINE_TOOLS_DIR / 'freestnd-c-hdrs/include'} " \
               "-MMD -MP"
    
    BIN_DIR.mkdir(parents=True, exist_ok=True)
    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    
    source_files = find_all_source_files()
    object_files = []
    
    if not source_files:
        print_color(Colors.RED, "No source files found!")
        return False
    
    print_color(Colors.BLUE, f"Found {len(source_files)} source files")
    
    compilation_success = True
    
    for src_file in source_files:
        obj_name, category, obj_file = get_source_file_info(src_file)
        
        needs_compile = True
        if obj_file.exists():
            src_mtime = src_file.stat().st_mtime
            obj_mtime = obj_file.stat().st_mtime
            if src_mtime <= obj_mtime:
                needs_compile = False
                print_color(Colors.BLUE, f"Using cached: {category}/{src_file.name}")
                object_files.append(obj_file)
        
        if needs_compile:
            print_color(Colors.CYAN, f"\n[{category}] {src_file.name}")
            
            if src_file.suffix == '.psf':
                if obj_file.exists():
                    obj_file.unlink()  
                
                if not compile_binary_file(src_file, obj_file):
                    compilation_success = False
                else:
                    object_files.append(obj_file)
            elif src_file.suffix in ('.c', '.S', '.asm'):
                if not compile_source_file(src_file, obj_file, cflags, cppflags):
                    compilation_success = False
                else:
                    object_files.append(obj_file)
            else:
                print_color(Colors.YELLOW, f"Skipping unknown file type: {src_file}")
                continue
    
    if not compilation_success:
        print_color(Colors.RED, "\nCompilation failed! Check errors above.")
        return False
    
    kernel_output = BIN_DIR / "kernel"
    
    link_needed = True
    if kernel_output.exists() and object_files:
        kernel_mtime = kernel_output.stat().st_mtime
        all_up_to_date = True
        for obj_file in object_files:
            if obj_file.stat().st_mtime > kernel_mtime:
                all_up_to_date = False
                break
        
        if all_up_to_date:
            link_needed = False
    
    if link_needed:
        print_color(Colors.BLUE, "\nLinking kernel...")
        
        ldflags = f"-m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 " \
                  f"--gc-sections -T {LINKER_SCRIPT}"
        
        cmd = ["ld"] + ldflags.split() + [str(obj) for obj in object_files] + \
              ["-o", str(kernel_output)]
        
        success, output = run_command(cmd, capture=True)
        if not success:
            print_color(Colors.RED, f"Linking failed: {output}")
            return False
        
        print_color(Colors.GREEN, f"Kernel linked: {kernel_output}")
    else:
        print_color(Colors.GREEN, "Kernel is up to date")
    
    return True

def create_iso():
    print_color(Colors.GREEN, "Creating ISO...")
    
    if not check_limine():
        print_color(Colors.RED, "Limine not built")
        return False
    
    kernel_binary = BIN_DIR / "kernel"
    if not kernel_binary.exists():
        print_color(Colors.RED, "Kernel not built")
        return False
    
    if ISO_ROOT.exists():
        shutil.rmtree(ISO_ROOT)
    
    (ISO_ROOT / "boot" / "limine").mkdir(parents=True, exist_ok=True)
    (ISO_ROOT / "EFI" / "BOOT").mkdir(parents=True, exist_ok=True)
    
    print_color(Colors.BLUE, "Copying files...")
    shutil.copy(kernel_binary, ISO_ROOT / "boot")
    ensure_limine_conf()
    shutil.copy(BASE_DIR / "limine.conf", ISO_ROOT / "boot" / "limine")
    
    limine_dir = BASE_DIR / "limine"
    shutil.copy(limine_dir / "limine-bios.sys", ISO_ROOT / "boot" / "limine")
    shutil.copy(limine_dir / "limine-bios-cd.bin", ISO_ROOT / "boot" / "limine")
    shutil.copy(limine_dir / "limine-uefi-cd.bin", ISO_ROOT / "boot" / "limine")
    shutil.copy(limine_dir / "BOOTX64.EFI", ISO_ROOT / "EFI" / "BOOT")
    shutil.copy(limine_dir / "BOOTIA32.EFI", ISO_ROOT / "EFI" / "BOOT")
    
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    iso_filename = f"{IMAGE_NAME}.{VERSION}.{timestamp}.iso"
    
    DEMO_ISO_DIR.mkdir(exist_ok=True)
    iso_path = DEMO_ISO_DIR / iso_filename
    
    print_color(Colors.BLUE, f"Creating ISO: {iso_path}")
    
    cmd = [
        "xorriso", "-as", "mkisofs", "-R", "-r", "-J",
        "-b", "boot/limine/limine-bios-cd.bin",
        "-no-emul-boot", "-boot-load-size", "4", "-boot-info-table", "-hfsplus",
        "-apm-block-size", "2048", "--efi-boot", "boot/limine/limine-uefi-cd.bin",
        "-efi-boot-part", "--efi-boot-image", "--protective-msdos-label",
        str(ISO_ROOT), "-o", str(iso_path)
    ]
    
    success, output = run_command(cmd, capture=True)
    if not success:
        print_color(Colors.RED, f"ISO creation failed: {output}")
        return False
    
    print_color(Colors.BLUE, "Installing Limine BIOS...")
    success, output = run_command([str(limine_dir / "limine"), "bios-install", str(iso_path)], capture=True)
    if not success:
        print_color(Colors.RED, f"Limine BIOS install failed: {output}")
        return False
    
    shutil.rmtree(ISO_ROOT)
    
    latest_iso = DEMO_ISO_DIR / f"{IMAGE_NAME}.latest.iso"
    if latest_iso.exists():
        latest_iso.unlink()
    latest_iso.symlink_to(iso_path)
    
    print_color(Colors.GREEN, f"ISO created: {iso_path}")
    print_color(Colors.GREEN, f"Latest ISO: {latest_iso}")
    return True

def build_all():
    print_color(Colors.GREEN, "Building everything...")
    
    DEMO_ISO_DIR.mkdir(exist_ok=True)
    
    if not ensure_linker_scripts():
        return False
    
    if not setup_missing_dependencies():
        return False
    
    if not build_limine():
        return False
    
    if not compile_kernel():
        return False
    
    if not create_iso():
        return False
    
    return True

def incremental_build():
    print_color(Colors.GREEN, "Incremental build...")
    
    if not ensure_linker_scripts():
        return False
    
    if not check_limine_tools():
        print_color(Colors.YELLOW, "Some dependencies missing, setting up...")
        if not setup_missing_dependencies():
            return False
    else:
        print_color(Colors.GREEN, "All dependencies available")
    
    if not check_limine():
        print_color(Colors.YELLOW, "Limine not built, building...")
        if not build_limine():
            return False
    else:
        print_color(Colors.GREEN, "Limine already built")
    
    if not compile_kernel():
        return False
    
    if not create_iso():
        return False
    
    return True

def clean():
    print_color(Colors.GREEN, "Performing full cleanup...")
    
    dirs_to_remove = [
        BIN_DIR,
        OBJ_DIR,
        ISO_ROOT,
        BASE_DIR / "limine",
        LIMINE_TOOLS_DIR,
        BASE_DIR / "edk2-ovmf"
    ]
    
    for dir_path in dirs_to_remove:
        if dir_path.exists():
            print_color(Colors.BLUE, f"Removing {dir_path}")
            shutil.rmtree(dir_path)
    
    files_to_remove = [
        BASE_DIR / f"{IMAGE_NAME}.iso",
        BASE_DIR / f"{IMAGE_NAME}.hdd",
        KERNEL_DIR / ".deps-obtained",
        BASE_DIR / "OS-TREE.txt"
    ]
    
    for file_path in files_to_remove:
        if file_path.exists():
            print_color(Colors.BLUE, f"Removing {file_path}")
            file_path.unlink()
    
    for temp_file in BASE_DIR.glob("temp_*"):
        temp_file.unlink()
    
    print_color(Colors.GREEN, "Cleanup complete!")
    return True

def cleaniso():
    print_color(Colors.GREEN, "Cleaning demo_iso directory...")
    
    if DEMO_ISO_DIR.exists():
        print_color(Colors.BLUE, f"Removing {DEMO_ISO_DIR}")
        shutil.rmtree(DEMO_ISO_DIR)
        DEMO_ISO_DIR.mkdir(exist_ok=True)
        print_color(Colors.GREEN, "demo_iso directory cleaned!")
    else:
        print_color(Colors.YELLOW, "demo_iso directory doesn't exist")
    
    return True

def gitclean():
    print_color(Colors.GREEN, "Removing all generated files...")
    
    items_to_remove = [
        DEMO_ISO_DIR,
        BASE_DIR / "limine.conf",
        LIMINE_TOOLS_DIR,
        BASE_DIR / "limine",
        BASE_DIR / ".vscode",
        LINKER_SCRIPTS_DIR,
        BIN_DIR,
        OBJ_DIR,
        ISO_ROOT,
    ]
    
    for item_path in items_to_remove:
        if item_path.exists():
            print_color(Colors.BLUE, f"Removing: {item_path}")
            if item_path.is_dir():
                shutil.rmtree(item_path)
            else:
                item_path.unlink()
        else:
            print_color(Colors.YELLOW, f"Doesn't exist: {item_path}")
    
    for temp_file in BASE_DIR.glob("temp_*"):
        temp_file.unlink()
    
    print_color(Colors.GREEN, "gitclean complete!")
    return True

def should_rebuild() -> bool:
    if not check_limine_tools():
        print_color(Colors.YELLOW, "Missing dependencies")
        return True
    
    if not check_limine():
        print_color(Colors.YELLOW, "Limine not built")
        return True
    
    if not LINKER_SCRIPT.exists():
        print_color(Colors.YELLOW, "Linker script not found")
        return True
    
    kernel_binary = BIN_DIR / "kernel"
    if not kernel_binary.exists():
        print_color(Colors.YELLOW, "Kernel not built")
        return True
    
    kernel_mtime = kernel_binary.stat().st_mtime
    source_files = find_all_source_files()
    
    for src_file in source_files:
        if src_file.stat().st_mtime > kernel_mtime:
            print_color(Colors.YELLOW, f"Source file {src_file.name} is newer than kernel")
            return True
    
    latest_iso = DEMO_ISO_DIR / f"{IMAGE_NAME}.latest.iso"
    if not latest_iso.exists():
        print_color(Colors.YELLOW, "No latest ISO found")
        return True
    
    print_color(Colors.GREEN, "Everything is up to date")
    return False

def main():
    parser = argparse.ArgumentParser(description="OS Build Script", add_help=False)
    parser.add_argument("command", nargs="?", help="Command to execute")
    parser.add_argument("--tree", action="store_true", help="Generate OS-TREE.txt")
    parser.add_argument("--help", "-h", action="store_true", help="Show help message")
    parser.add_argument("--files", nargs="+", help="Specific files to include in tree")
    parser.add_argument("--structure-only", action="store_true", 
                       help="Generate tree structure only (no file contents)")
    parser.add_argument("--no-clean", action="store_true", 
                       help="Don't clean obj/bin after QEMU exits (use with run command)")
    
    args = parser.parse_args()
    
    if args.help or (not args.command and not args.tree and not args.files):
        print_help()
        return 0
    
    if args.tree and not args.command:
        generate_tree_structure(specific_files=args.files, structure_only=args.structure_only)
        return 0
    
    command = args.command.lower() if args.command else ""
    
    if command == "run":
        if should_rebuild():
            print_color(Colors.YELLOW, "Incremental rebuild...")
            if not incremental_build():
                return 1
        else:
            print_color(Colors.GREEN, "Everything is up to date")
        
        if args.tree:
            generate_tree_structure(specific_files=args.files, structure_only=args.structure_only)
        
        clean_after = not args.no_clean
        return 0 if run_qemu_and_clean(clean_after=clean_after) else 1
    
    elif command == "clean":
        return 0 if clean() else 1
    
    elif command == "cleaniso":
        return 0 if cleaniso() else 1
    
    elif command == "gitclean":
        return 0 if gitclean() else 1
    
    else:
        print_color(Colors.RED, f"Unknown command: {command}")
        print_help()
        return 1

if __name__ == "__main__":
    sys.exit(main())