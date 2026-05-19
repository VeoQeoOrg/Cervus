#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_SYMBOLS 1024
#define MAX_RELOCS 1024
#define SEC_SIZE 65536
#define MAX_LINE_LEN 256
#define ELF_MAGIC "\x7F""ELF"

typedef enum { 
    ASM_TEXT, 
    ASM_DATA 
} SectionType;

typedef enum { 
    OP_NONE, 
    OP_REG, 
    OP_IMM, 
    OP_MEM, 
    OP_SYM 
} OpType;

typedef struct {
    char name[64];
    uint64_t addr;
    SectionType sec;
    bool is_global;
    bool defined;
} Symbol;

typedef struct {
    uint64_t offset;
    char sym_name[64];
    SectionType sec;
    int size;
    bool is_relative;
} Relocation;

typedef struct {
    uint8_t data[SEC_SIZE];
    size_t size;
} Section;

typedef struct {
    Section text;
    Section data;
    Section *current;
    Symbol symbols[MAX_SYMBOLS];
    int symbol_count;
    Relocation relocations[MAX_RELOCS];
    int relocation_count;
} Assembler;

typedef struct {
    const char *name;
    uint8_t id;
    bool is_ext;
} Reg;

typedef struct {
    OpType type;
    Reg *reg;
    uint64_t imm;
    Reg *base_reg;
    int32_t disp;
    char sym[64];
} Operand;

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

static Reg g_regs[] = {
    {"rax", 0, false}, {"rcx", 1, false}, {"rdx", 2, false}, {"rbx", 3, false},
    {"rsp", 4, false}, {"rbp", 5, false}, {"rsi", 6, false}, {"rdi", 7, false},
    {"r8", 0, true},   {"r9", 1, true},   {"r10", 2, true},  {"r11", 3, true},
    {"r12", 4, true},  {"r13", 5, true},  {"r14", 6, true},  {"r15", 7, true},
    {NULL, 0, false}
};

static void str_trim(char *s) {
    char *p = s;
    int l = strlen(p);
    while (l > 0 && isspace((unsigned char)p[l - 1])) p[--l] = '\0';
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, l + 1);
}

static Reg *reg_find(const char *name) {
    for (int i = 0; g_regs[i].name != NULL; i++) {
        if (strcasecmp(g_regs[i].name, name) == 0) return &g_regs[i];
    }
    return NULL;
}

static void assembler_init(Assembler *as) {
    memset(as, 0, sizeof(Assembler));
    as->current = &as->text;
}

static Symbol *assembler_find_symbol(Assembler *as, const char *name) {
    for (int i = 0; i < as->symbol_count; i++) {
        if (strcmp(as->symbols[i].name, name) == 0) return &as->symbols[i];
    }
    return NULL;
}

static void assembler_add_symbol(Assembler *as, const char *name, uint64_t addr, SectionType sec, bool is_global, bool defined) {
    Symbol *s = assembler_find_symbol(as, name);
    if (s) {
        if (defined) {
            s->addr = addr;
            s->sec = sec;
            s->defined = true;
        }
        if (is_global) s->is_global = true;
    } else {
        if (as->symbol_count >= MAX_SYMBOLS) return;
        Symbol *ns = &as->symbols[as->symbol_count++];
        strncpy(ns->name, name, 63);
        ns->addr = addr;
        ns->sec = sec;
        ns->is_global = is_global;
        ns->defined = defined;
    }
}

static void assembler_add_reloc(Assembler *as, uint64_t offset, const char *sym, SectionType sec, int size, bool rel) {
    if (as->relocation_count >= MAX_RELOCS) return;
    Relocation *r = &as->relocations[as->relocation_count++];
    r->offset = offset;
    strncpy(r->sym_name, sym, 63);
    r->sec = sec;
    r->size = size;
    r->is_relative = rel;
}

static void section_emit_byte(Assembler *as, uint8_t b) {
    if (as->current->size < SEC_SIZE) {
        as->current->data[as->current->size++] = b;
    } else {
        fprintf(stderr, "Section overflow\n");
        exit(1);
    }
}

static void section_emit_word(Assembler *as, uint16_t w) {
    section_emit_byte(as, w & 0xFF);
    section_emit_byte(as, (w >> 8) & 0xFF);
}

static void section_emit_dword(Assembler *as, uint32_t d) {
    section_emit_word(as, d & 0xFFFF);
    section_emit_word(as, (d >> 16) & 0xFFFF);
}

static void section_emit_qword(Assembler *as, uint64_t q) {
    section_emit_dword(as, q & 0xFFFFFFFF);
    section_emit_dword(as, (q >> 32) & 0xFFFFFFFF);
}

static bool operand_parse(char *s, Operand *op) {
    memset(op, 0, sizeof(Operand));
    if (!s || strlen(s) == 0) return false;

    if (s[0] == '[') {
        op->type = OP_MEM;
        char *end = strchr(s, ']');
        if (end) *end = '\0';
        char *inner = s + 1;
        char *plus = strchr(inner, '+');
        if (plus) {
            *plus = '\0';
            op->disp = (int32_t)strtol(plus + 1, NULL, 0);
        }
        char *minus = strchr(inner, '-');
        if (minus) {
            *minus = '\0';
            op->disp = -(int32_t)strtol(minus + 1, NULL, 0);
        }
        char reg_name[32];
        strncpy(reg_name, inner, 31);
        char *trim_reg = strtok(reg_name, " \t");
        op->base_reg = reg_find(trim_reg);
        return op->base_reg != NULL;
    }

    Reg *r = reg_find(s);
    if (r) {
        op->type = OP_REG;
        op->reg = r;
        return true;
    }

    char *endptr;
    uint64_t val = strtoull(s, &endptr, 0);
    if (*endptr == '\0') {
        op->type = OP_IMM;
        op->imm = val;
        return true;
    }

    op->type = OP_SYM;
    strncpy(op->sym, s, 63);
    return true;
}

static uint8_t rex_prefix(bool w, bool r, bool x, bool b) {
    return 0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0);
}

static void encode_alu(Assembler *as, uint8_t opcode, Operand *dst, Operand *src) {
    if (dst->type == OP_REG && src->type == OP_REG) {
        bool r = src->reg->is_ext;
        bool b = dst->reg->is_ext;
        section_emit_byte(as, rex_prefix(true, r, false, b));
        section_emit_byte(as, opcode);
        section_emit_byte(as, 0xC0 + (src->reg->id * 8) + dst->reg->id);
    } else if (dst->type == OP_REG && src->type == OP_IMM) {
        bool b = dst->reg->is_ext;
        section_emit_byte(as, rex_prefix(true, false, false, b));
        if (src->imm <= 127) {
            section_emit_byte(as, 0x83);
            section_emit_byte(as, 0xC0 + dst->reg->id);
            section_emit_byte(as, src->imm & 0xFF);
        } else {
            section_emit_byte(as, 0x81);
            section_emit_byte(as, 0xC0 + dst->reg->id);
            section_emit_dword(as, src->imm & 0xFFFFFFFF);
        }
    }
}

static void assembler_line(Assembler *as, char *line, int pass) {
    while (isspace((unsigned char)*line)) line++;
    char *end = line + strlen(line) - 1;
    while (end > line && isspace((unsigned char)*end)) *end-- = '\0';
    if (*line == '\0' || *line == ';') return;

    char *colon = strchr(line, ':');
    if (colon) {
        *colon = '\0';
        char *label = line;
        while (isspace((unsigned char)*label)) label++;
        char *label_end = label + strlen(label) - 1;
        while (label_end > label && isspace((unsigned char)*label_end)) *label_end-- = '\0';
        
        if (pass == 1) {
            assembler_add_symbol(as, label, as->current == &as->text ? as->text.size : as->data.size, as->current == &as->text ? ASM_TEXT : ASM_DATA, false, true);
        }
        line = colon + 1;
        while (isspace((unsigned char)*line)) line++;
        if (*line == '\0') return;
    }

    char *inst = strtok(line, " \t");
    if (!inst) return;

    if (strcasecmp(inst, "section") == 0) {
        char *sec_name = strtok(NULL, " \t");
        if (strcasecmp(sec_name, ".text") == 0) as->current = &as->text;
        else if (strcasecmp(sec_name, ".data") == 0) as->current = &as->data;
        return;
    }
    if (strcasecmp(inst, "global") == 0) {
        char *sym_name = strtok(NULL, " \t");
        if (pass == 1) assembler_add_symbol(as, sym_name, 0, ASM_TEXT, true, false);
        return;
    }

    if (strcasecmp(inst, "db") == 0 || strcasecmp(inst, "dw") == 0 ||
        strcasecmp(inst, "dd") == 0 || strcasecmp(inst, "dq") == 0) {
        char *val_str = strtok(NULL, "");
        if (!val_str) return;
        str_trim(val_str);
        if (pass == 2) {
            if (val_str[0] == '"') {
                for (int i = 1; val_str[i] && val_str[i] != '"'; i++) section_emit_byte(as, val_str[i]);
            } else {
                uint64_t val = strtoull(val_str, NULL, 0);
                if (strcasecmp(inst, "db") == 0) section_emit_byte(as, val & 0xFF);
                else if (strcasecmp(inst, "dw") == 0) section_emit_word(as, val & 0xFFFF);
                else if (strcasecmp(inst, "dd") == 0) section_emit_dword(as, val & 0xFFFFFFFF);
                else section_emit_qword(as, val);
            }
        } else {
            size_t size = 0;
            if (val_str[0] == '"') {
                for (int i = 1; val_str[i] && val_str[i] != '"'; i++) size++;
            } else {
                if (strcasecmp(inst, "db") == 0) size = 1;
                else if (strcasecmp(inst, "dw") == 0) size = 2;
                else if (strcasecmp(inst, "dd") == 0) size = 4;
                else size = 8;
            }
            as->current->size += size;
        }
        return;
    }

    char *op1_str = strtok(NULL, ",");
    char *op2_str = strtok(NULL, "");
    if (op1_str) str_trim(op1_str);
    if (op2_str) str_trim(op2_str);

    Operand op1 = {0}, op2 = {0};
    if (op1_str) operand_parse(op1_str, &op1);
    if (op2_str) operand_parse(op2_str, &op2);

    if (strcasecmp(inst, "syscall") == 0) {
        if (pass == 2) { section_emit_byte(as, 0x0F); section_emit_byte(as, 0x05); }
        else { as->text.size += 2; }
        return;
    }
    if (strcasecmp(inst, "ret") == 0) {
        if (pass == 2) section_emit_byte(as, 0xC3); else as->text.size += 1;
        return;
    }
    if (strcasecmp(inst, "nop") == 0) {
        if (pass == 2) section_emit_byte(as, 0x90); else as->text.size += 1;
        return;
    }

    if (strcasecmp(inst, "push") == 0) {
        if (pass == 2) {
            bool b = op1.reg->is_ext;
            if (b) section_emit_byte(as, rex_prefix(false, false, false, true));
            section_emit_byte(as, 0x50 + op1.reg->id);
        } else {
            as->text.size += (op1.reg->is_ext ? 2 : 1);
        }
        return;
    }
    if (strcasecmp(inst, "pop") == 0) {
        if (pass == 2) {
            bool b = op1.reg->is_ext;
            if (b) section_emit_byte(as, rex_prefix(false, false, false, true));
            section_emit_byte(as, 0x58 + op1.reg->id);
        } else {
            as->text.size += (op1.reg->is_ext ? 2 : 1);
        }
        return;
    }

    if (strcasecmp(inst, "mov") == 0) {
        if (pass == 2) {
            if (op1.type == OP_REG && op2.type == OP_REG) {
                bool r = op2.reg->is_ext;
                bool b = op1.reg->is_ext;
                section_emit_byte(as, rex_prefix(true, r, false, b));
                section_emit_byte(as, 0x89);
                section_emit_byte(as, 0xC0 + (op2.reg->id * 8) + op1.reg->id);
            } else if (op1.type == OP_REG && op2.type == OP_IMM) {
                bool b = op1.reg->is_ext;
                section_emit_byte(as, rex_prefix(true, false, false, b));
                section_emit_byte(as, 0xB8 + op1.reg->id);
                section_emit_qword(as, op2.imm);
            } else if (op1.type == OP_REG && op2.type == OP_SYM) {
                bool b = op1.reg->is_ext;
                section_emit_byte(as, rex_prefix(true, false, false, b));
                section_emit_byte(as, 0xB8 + op1.reg->id);
                assembler_add_reloc(as, as->text.size, op2.sym, ASM_TEXT, 8, false);
                section_emit_qword(as, 0);
            } else if (op1.type == OP_REG && op2.type == OP_MEM) {
                bool r = op1.reg->is_ext;
                bool b = op2.base_reg->is_ext;
                section_emit_byte(as, rex_prefix(true, r, false, b));
                section_emit_byte(as, 0x8B);
                if (op2.disp == 0) {
                    section_emit_byte(as, 0x00 + (op1.reg->id * 8) + op2.base_reg->id);
                } else if (op2.disp >= -128 && op2.disp <= 127) {
                    section_emit_byte(as, 0x40 + (op1.reg->id * 8) + op2.base_reg->id);
                    section_emit_byte(as, op2.disp & 0xFF);
                } else {
                    section_emit_byte(as, 0x80 + (op1.reg->id * 8) + op2.base_reg->id);
                    section_emit_dword(as, op2.disp);
                }
            } else if (op1.type == OP_MEM && op2.type == OP_REG) {
                bool r = op2.reg->is_ext;
                bool b = op1.base_reg->is_ext;
                section_emit_byte(as, rex_prefix(true, r, false, b));
                section_emit_byte(as, 0x89);
                if (op1.disp == 0) {
                    section_emit_byte(as, 0x00 + (op2.reg->id * 8) + op1.base_reg->id);
                } else if (op1.disp >= -128 && op1.disp <= 127) {
                    section_emit_byte(as, 0x40 + (op2.reg->id * 8) + op1.base_reg->id);
                    section_emit_byte(as, op1.disp & 0xFF);
                } else {
                    section_emit_byte(as, 0x80 + (op2.reg->id * 8) + op1.base_reg->id);
                    section_emit_dword(as, op1.disp);
                }
            }
        } else {
            if (op1.type == OP_REG && op2.type == OP_REG) as->text.size += 3;
            else if (op1.type == OP_REG && op2.type == OP_IMM) as->text.size += 10;
            else if (op1.type == OP_REG && op2.type == OP_SYM) as->text.size += 10;
            else if (op1.type == OP_REG && op2.type == OP_MEM) {
                as->text.size += 3 + (op2.disp == 0 ? 0 : (op2.disp >= -128 && op2.disp <= 127 ? 1 : 4));
            } else if (op1.type == OP_MEM && op2.type == OP_REG) {
                as->text.size += 3 + (op1.disp == 0 ? 0 : (op1.disp >= -128 && op1.disp <= 127 ? 1 : 4));
            }
        }
        return;
    }

    if (strcasecmp(inst, "xor") == 0) {
        if (pass == 2) encode_alu(as, 0x31, &op1, &op2);
        else as->text.size += (op2.type == OP_IMM) ? (op2.imm <= 127 ? 4 : 7) : 3;
        return;
    }
    if (strcasecmp(inst, "add") == 0) {
        if (pass == 2) encode_alu(as, 0x01, &op1, &op2);
        else as->text.size += (op2.type == OP_IMM) ? (op2.imm <= 127 ? 4 : 7) : 3;
        return;
    }
    if (strcasecmp(inst, "sub") == 0) {
        if (pass == 2) encode_alu(as, 0x29, &op1, &op2);
        else as->text.size += (op2.type == OP_IMM) ? (op2.imm <= 127 ? 4 : 7) : 3;
        return;
    }

    bool is_jmp = (strcasecmp(inst, "jmp") == 0);
    bool is_call = (strcasecmp(inst, "call") == 0);
    bool is_jcc = (strcasecmp(inst, "je") == 0 || strcasecmp(inst, "jne") == 0 ||
                   strcasecmp(inst, "jg") == 0 || strcasecmp(inst, "jl") == 0);

    if (is_jmp || is_call || is_jcc) {
        if (pass == 2) {
            if (is_jmp) {
                section_emit_byte(as, 0xE9);
                assembler_add_reloc(as, as->text.size, op1.sym, ASM_TEXT, 4, true);
                section_emit_dword(as, 0);
            } else if (is_call) {
                section_emit_byte(as, 0xE8);
                assembler_add_reloc(as, as->text.size, op1.sym, ASM_TEXT, 4, true);
                section_emit_dword(as, 0);
            } else {
                section_emit_byte(as, 0x0F);
                uint8_t code = 0x84;
                if (strcasecmp(inst, "jne") == 0) code = 0x85;
                else if (strcasecmp(inst, "jg") == 0) code = 0x8F;
                else if (strcasecmp(inst, "jl") == 0) code = 0x8C;
                section_emit_byte(as, code);
                assembler_add_reloc(as, as->text.size, op1.sym, ASM_TEXT, 4, true);
                section_emit_dword(as, 0);
            }
        } else {
            as->text.size += is_jcc ? 6 : 5;
        }
        return;
    }

    fprintf(stderr, "Instruction failure: %s\n", inst);
}

static void assembler_apply_relocs(Assembler *as, uint64_t text_vaddr, uint64_t data_vaddr) {
    for (int i = 0; i < as->relocation_count; i++) {
        Relocation *r = &as->relocations[i];
        Symbol *s = assembler_find_symbol(as, r->sym_name);
        if (!s || !s->defined) {
            fprintf(stderr, "Undefined reference to '%s'\n", r->sym_name);
            exit(1);
        }
        uint64_t target_addr = s->addr + (s->sec == ASM_TEXT ? text_vaddr : data_vaddr);
        uint64_t patch_addr = r->offset + (r->sec == ASM_TEXT ? text_vaddr : data_vaddr);

        if (r->is_relative) {
            int64_t disp = (int64_t)(target_addr - (patch_addr + r->size));
            if (r->size == 4) {
                uint32_t d32 = (uint32_t)disp;
                memcpy(&as->text.data[r->offset], &d32, 4);
            }
        } else {
            if (r->size == 8) {
                memcpy(&as->text.data[r->offset], &target_addr, 8);
            }
        }
    }
}

static void assembler_write_elf64(Assembler *as, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) { perror("Output open error"); exit(1); }

    uint64_t text_vaddr = 0x400000;
    uint64_t data_vaddr = 0x600000;

    assembler_apply_relocs(as, text_vaddr + 0x120, data_vaddr);

    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    memcpy(ehdr.e_ident, ELF_MAGIC, 4);
    ehdr.e_ident[4] = 2;
    ehdr.e_ident[5] = 1;
    ehdr.e_ident[6] = 1;
    ehdr.e_type = 2;
    ehdr.e_machine = 62;
    ehdr.e_version = 1;
    
    Symbol *start = assembler_find_symbol(as, "_start");
    if (!start) start = assembler_find_symbol(as, "main");
    ehdr.e_entry = start ? (start->addr + text_vaddr + 0x120) : (text_vaddr + 0x120);
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 2;

    Elf64_Phdr phdrs[2];
    memset(phdrs, 0, sizeof(phdrs));

    phdrs[0].p_type = 1;
    phdrs[0].p_flags = 5;
    phdrs[0].p_offset = 0x120;
    phdrs[0].p_vaddr = text_vaddr + 0x120;
    phdrs[0].p_paddr = phdrs[0].p_vaddr;
    phdrs[0].p_filesz = as->text.size;
    phdrs[0].p_memsz = as->text.size;
    phdrs[0].p_align = 0x1000;

    phdrs[1].p_type = 1;
    phdrs[1].p_flags = 6;
    phdrs[1].p_offset = 0x120 + as->text.size;
    phdrs[1].p_vaddr = data_vaddr;
    phdrs[1].p_paddr = phdrs[1].p_vaddr;
    phdrs[1].p_filesz = as->data.size;
    phdrs[1].p_memsz = as->data.size;
    phdrs[1].p_align = 0x1000;

    fwrite(&ehdr, 1, sizeof(ehdr), f);
    fwrite(phdrs, 1, sizeof(phdrs), f);

    uint8_t padding[112];
    memset(padding, 0, sizeof(padding));
    fwrite(padding, 1, sizeof(padding), f);

    fwrite(as->text.data, 1, as->text.size, f);
    fwrite(as->data.data, 1, as->data.size, f);

    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("MiniASM - Intel x86_64 Assembler\n");
        printf("Usage: %s <source.asm> <output_elf>\n", argv[0]);
        return 1;
    }

    static Assembler as;
    assembler_init(&as);

    FILE *src = fopen(argv[1], "r");
    if (!src) { perror("Source open error"); return 1; }
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), src)) {
        char temp[MAX_LINE_LEN];
        strcpy(temp, line);
        assembler_line(&as, temp, 1);
    }
    fclose(src);

    size_t final_text_sz = as.text.size;
    size_t final_data_sz = as.data.size;
    as.text.size = 0;
    as.data.size = 0;
    as.current = &as.text;

    src = fopen(argv[1], "r");
    while (fgets(line, sizeof(line), src)) {
        char temp[MAX_LINE_LEN];
        strcpy(temp, line);
        assembler_line(&as, temp, 2);
    }
    fclose(src);

    assembler_write_elf64(&as, argv[2]);

    printf("Assembly complete: %s\n", argv[2]);
    printf("  .text: %zu bytes\n", final_text_sz);
    printf("  .data: %zu bytes\n", final_data_sz);
    return 0;
}
