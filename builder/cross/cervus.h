#undef TARGET_CERVUS
#define TARGET_CERVUS 1

#undef LIB_SPEC
#define LIB_SPEC "%{shared:-lc;static|no-pie:--start-group -lcervus -lgcc --end-group;:--start-group -lc -lgcc --end-group}"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared:crt0.o%s} %{static|no-pie:crtbegin.o%s;:crtbeginS.o%s}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{static|no-pie:crtend.o%s;:crtendS.o%s}"

#undef LINK_SPEC
#define LINK_SPEC "%{rdynamic:--export-dynamic} %{shared:-shared --hash-style=sysv;static|no-pie:-static %{!Ttext-segment*:%{!Wl,-Ttext-segment*:-Ttext-segment=0x401000}};:--export-dynamic --hash-style=sysv -dynamic-linker /lib/ld-cervus.elf}"

#undef TARGET_THREAD_SSP_OFFSET
#define TARGET_THREAD_SSP_OFFSET (TARGET_64BIT ? 0x28 : 0x14)

#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END file_end_indicate_exec_stack

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()      \
  do {                                \
    builtin_define ("__cervus__");    \
    builtin_define ("__unix__");      \
    builtin_assert ("system=cervus"); \
    builtin_assert ("system=unix");   \
  } while (0)
