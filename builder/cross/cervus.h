/* Cervus OS target for GCC (x86_64-cervus).
   Static, non-PIE, no dynamic loader; links Cervus's crt0 + libcervus + libgcc. */

#undef TARGET_CERVUS
#define TARGET_CERVUS 1

#undef LIB_SPEC
#define LIB_SPEC "--start-group -lcervus -lgcc --end-group"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""

#undef LINK_SPEC
#define LINK_SPEC "-static %{!Ttext-segment*:%{!Wl,-Ttext-segment*:-Ttext-segment=0x401000}} %{shared:-shared}"

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
