#undef TARGET_CERVUS
#define TARGET_CERVUS 1

#undef LIB_SPEC
#define LIB_SPEC "%{!shared:--start-group %{pie:-lcervus_pic;:-lcervus} -lgcc --end-group}"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared:crt0.o%s} %{shared|pie:crtbeginS.o%s;:crtbegin.o%s}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{shared|pie:crtendS.o%s;:crtend.o%s}"

#undef LINK_SPEC
#define LINK_SPEC "%{shared:-shared --hash-style=sysv;pie:--export-dynamic --hash-style=sysv -dynamic-linker /lib/ld-cervus.elf;:-static %{!Ttext-segment*:%{!Wl,-Ttext-segment*:-Ttext-segment=0x401000}}}"

#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END file_end_indicate_exec_stack

#undef DRIVER_SELF_SPECS
#define DRIVER_SELF_SPECS "%{!mred-zone:%{!mno-red-zone:-mno-red-zone}}"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()      \
  do {                                \
    builtin_define ("__cervus__");    \
    builtin_define ("__unix__");      \
    builtin_assert ("system=cervus"); \
    builtin_assert ("system=unix");   \
  } while (0)
