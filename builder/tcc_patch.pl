#!/usr/bin/env perl
# Applies Cervus's patches to a pristine tcc-0.9.27 source tree.
#
# Ported 1:1 from the old builder/build.c (dstr_replace_once,
# dstr_guard_line_with, dstr_guard_func_impls). All operations are literal
# byte substring matches, never regex, and every patch is idempotent -- safe
# to run repeatedly against an already-patched tree.
#
# Usage: perl builder/tcc_patch.pl <tcc-src-dir>

use strict;
use warnings;

my $C_RESET  = "\033[0m";
my $C_RED    = "\033[91m";
my $C_GREEN  = "\033[92m";
my $C_YELLOW = "\033[93m";
my $C_CYAN   = "\033[96m";

sub say_color { my ($c, $msg) = @_; print "$c\[tcc-patch\]$C_RESET " . $msg . "\n"; }

my $src_dir = shift @ARGV or die "usage: tcc_patch.pl <tcc-src-dir>\n";

# --- primitives, mirroring build.c's dstr_* helpers -------------------------

sub replace_once {
    my ($ref, $old, $new) = @_;
    my $idx = index($$ref, $old);
    return 0 if $idx < 0;
    substr($$ref, $idx, length($old)) = $new;
    return 1;
}

# Wrap the whole line containing $marker in #ifndef $guard / #endif.
# Idempotent: skips if the line directly above is already that #ifndef.
sub guard_line_with {
    my ($ref, $marker, $guard) = @_;
    my $idx = index($$ref, $marker);
    return 0 if $idx < 0;

    my $line_start = $idx;
    $line_start-- while $line_start > 0 && substr($$ref, $line_start - 1, 1) ne "\n";

    my $prev_line_start = $line_start;
    $prev_line_start-- if $prev_line_start > 0;
    $prev_line_start-- while $prev_line_start > 0 && substr($$ref, $prev_line_start - 1, 1) ne "\n";

    my $needle = "#ifndef $guard";
    my $pll = $line_start - $prev_line_start;
    return 0 if $pll >= length($needle)
        && substr($$ref, $prev_line_start, length($needle)) eq $needle;

    my $line_end = $idx;
    my $len = length($$ref);
    $line_end++ while $line_end < $len && substr($$ref, $line_end, 1) ne "\n";
    $line_end++ if $line_end < $len && substr($$ref, $line_end, 1) eq "\n";

    substr($$ref, $line_end, 0)   = "#endif\n";
    substr($$ref, $line_start, 0) = "#ifndef $guard\n";
    return 1;
}

# Wrap every *definition* (not declaration) of $name(...) { ... } in
# #ifndef $guard / #endif, brace-matched. Returns count wrapped.
sub guard_func_impls {
    my ($ref, $name, $guard) = @_;
    my $count = 0;
    my $cursor = 0;
    my $name_n = length($name);

    while ($cursor + $name_n < length($$ref)) {
        my $idx = index($$ref, $name, $cursor);
        last if $idx < 0;

        my $len = length($$ref);
        my $i = $idx + $name_n;
        $i++ while $i < $len && (substr($$ref, $i, 1) eq ' ' || substr($$ref, $i, 1) eq "\t");
        if ($i >= $len || substr($$ref, $i, 1) ne '(') {
            $cursor = $idx + 1;
            next;
        }

        my $j = $i;
        $j++ while $j < $len && substr($$ref, $j, 1) ne '{' && substr($$ref, $j, 1) ne ';';
        last if $j >= $len;
        if (substr($$ref, $j, 1) eq ';') {
            $cursor = $idx + 1;
            next;
        }

        my $depth = 0;
        my $end = $j;
        for (; $end < $len; $end++) {
            my $c = substr($$ref, $end, 1);
            if ($c eq '{') { $depth++; }
            elsif ($c eq '}') {
                $depth--;
                if ($depth == 0) { $end++; last; }
            }
        }
        last if $depth != 0;
        $end++ if $end < $len && substr($$ref, $end, 1) eq "\n";

        my $line_start = $idx;
        $line_start-- while $line_start > 0 && substr($$ref, $line_start - 1, 1) ne "\n";

        my $look = $line_start > 200 ? $line_start - 200 : 0;
        if (index(substr($$ref, $look, $line_start - $look), $guard) >= 0) {
            $cursor = $end;
            next;
        }

        my $prefix = "#ifndef $guard\n";
        my $suffix = "#endif\n";
        substr($$ref, $end, 0)         = $suffix;
        substr($$ref, $line_start, 0)  = $prefix;
        $count++;
        $cursor = $end + length($prefix) + length($suffix);
    }
    return $count;
}

# Insert $insert right before $marker, but only if $marker is present and
# $other is not already present anywhere in the file.
sub insert_before_if_absent {
    my ($ref, $marker, $other, $insert) = @_;
    my $idx = index($$ref, $marker);
    return 0 if $idx < 0;
    return 0 if index($$ref, $other) >= 0;
    substr($$ref, $idx, 0) = $insert;
    return 1;
}

# Insert $stub right after the last #include found within the first $window
# bytes, unless $done_marker is already present anywhere in the file.
sub insert_after_last_include {
    my ($ref, $stub, $window, $done_marker) = @_;
    return 0 if index($$ref, $done_marker) >= 0;

    my $len = length($$ref);
    my $scan = $len < $window ? $len : $window;
    my $last_inc_end = 0;
    my $i = 0;
    while ($i < $scan) {
        if (substr($$ref, $i, 8) eq '#include') {
            my $e = $i;
            $e++ while $e < $len && substr($$ref, $e, 1) ne "\n";
            $e++ if $e < $len;
            $last_inc_end = $e;
            $i = $e;
            next;
        }
        $i++;
    }
    return 0 if $last_inc_end == 0;
    substr($$ref, $last_inc_end, 0) = $stub;
    return 1;
}

# --- per-file edits, verbatim from build.c's edit_*() functions -------------

sub edit_tcc_h {
    my ($ref) = @_;
    my $changed = 0;
    $changed++ if guard_line_with($ref, "#include <dlfcn.h>", "TCC_NO_DLOPEN");
    $changed++ if insert_before_if_absent($ref,
        "#include <sys/utsname.h>", "#include <sys/mman.h>",
        "#include <sys/mman.h>\n");
    $changed++ if guard_line_with($ref, "enable bound checking code", "CONFIG_TCC_BCHECK");
    return $changed;
}

sub edit_libtcc_c {
    my ($ref) = @_;
    my $changed = 0;

    $changed++ if replace_once($ref,
        "return dlopen(filename, RTLD_GLOBAL | RTLD_LAZY);\n",
        "(void)filename;\n    return NULL;\n");
    $changed++ if replace_once($ref,
        "return dlopen(filename, RTLD_LOCAL | RTLD_LAZY);\n",
        "(void)filename;\n    return NULL;\n");
    $changed++ if replace_once($ref,
        "return dlsym(handle, sym);\n",
        "(void)handle; (void)sym;\n    return NULL;\n");
    $changed++ if replace_once($ref, "dlclose(handle);\n", "(void)handle;\n");

    if (index($$ref, "s->alacarte_link = 1;\n    s->static_link = 1;") < 0) {
        $changed++ if replace_once($ref,
            "    s->alacarte_link = 1;",
            "    s->alacarte_link = 1;\n    s->static_link = 1;");
    }

    $changed++ if replace_once($ref,
        "        if (output_type != TCC_OUTPUT_DLL)\n"
      . "            tcc_add_crt(s, \"crt1.o\");\n"
      . "        tcc_add_crt(s, \"crti.o\");",
        "        if (output_type != TCC_OUTPUT_DLL)\n"
      . "            tcc_add_crt(s, \"crt0.o\");");

    return $changed;
}

sub edit_tccrun_c {
    my ($ref) = @_;
    my $changed = 0;

    $changed++ if guard_line_with($ref, "<sys/ucontext.h>", "TCC_NO_BACKTRACE");

    my $stub = "\n#ifdef TCC_NO_BACKTRACE\n"
             . "typedef void *ucontext_t;\n"
             . "#endif\n";
    $changed++ if insert_after_last_include($ref, $stub, 4096, "typedef void *ucontext_t;");

    $changed++ if guard_line_with($ref, "set_exception_handler();", "TCC_NO_BACKTRACE");

    for my $name (qw(rt_error sig_error set_exception_handler rt_get_caller_pc)) {
        my $n = guard_func_impls($ref, $name, "TCC_NO_BACKTRACE");
        $changed++ if $n > 0;
    }
    return $changed;
}

sub edit_tccelf_c {
    my ($ref) = @_;
    my $changed = 0;

    if (index($$ref, "tcc_add_library_err(s1, \"cervus\")") < 0) {
        $changed++ if replace_once($ref,
            "    if (!s1->nostdlib) {\n        tcc_add_library_err(s1, \"c\");",
            "    if (!s1->nostdlib) {\n        tcc_add_library_err(s1, \"cervus\");");
    }

    $changed++ if replace_once($ref,
        "        if (s1->output_type != TCC_OUTPUT_MEMORY)\n"
      . "            tcc_add_crt(s1, \"crtn.o\");\n"
      . "    }\n",
        "    }\n");

    if (index($$ref, "CERVUS_PLT_FOLD") < 0) {
        $changed++ if replace_once($ref,
            "            if ((type == R_X86_64_PLT32 || type == R_X86_64_PC32) &&\n"
          . "                (ELFW(ST_VISIBILITY)(sym->st_other) != STV_DEFAULT ||\n"
          . "\t\t ELFW(ST_BIND)(sym->st_info) == STB_LOCAL)) {\n"
          . "                rel->r_info = ELFW(R_INFO)(sym_index, R_X86_64_PC32);\n"
          . "                continue;\n"
          . "            }",
            "#define CERVUS_PLT_FOLD\n"
          . "            if ((type == R_X86_64_PLT32 || type == R_X86_64_PC32) &&\n"
          . "                (ELFW(ST_VISIBILITY)(sym->st_other) != STV_DEFAULT ||\n"
          . "\t\t ELFW(ST_BIND)(sym->st_info) == STB_LOCAL ||\n"
          . "\t\t (s1->static_link && sym->st_shndx != SHN_UNDEF))) {\n"
          . "                rel->r_info = ELFW(R_INFO)(sym_index, R_X86_64_PC32);\n"
          . "                continue;\n"
          . "            }");
    }

    return $changed;
}

# --- driver -------------------------------------------------------------

sub patch_file {
    my ($rel, $editor) = @_;
    my $path = "$src_dir/$rel";
    open(my $fh, '<:raw', $path) or do {
        say_color($C_RED, "cannot read $path");
        exit 1;
    };
    local $/;
    my $content = <$fh>;
    close $fh;

    my $changed = $editor->(\$content);
    if ($changed > 0) {
        open(my $out, '>:raw', $path) or do {
            say_color($C_RED, "cannot write $path");
            exit 1;
        };
        print $out $content;
        close $out;
        say_color($C_GREEN, "$rel: $changed edit(s)");
    } else {
        say_color($C_YELLOW, "$rel: no changes (already patched?)");
    }
}

patch_file("tcc.h",     \&edit_tcc_h);
patch_file("libtcc.c",  \&edit_libtcc_c);
patch_file("tccrun.c",  \&edit_tccrun_c);
patch_file("tccelf.c",  \&edit_tccelf_c);
