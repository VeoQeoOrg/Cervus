/*

 Created by alex on 6/24/26.
 Displays OS, hostname, kernel, uptime, CPU, RAM, disk usage, shell, terminal,
 desktop env, and a random quote.

 * General Features:
 *
 *  ** Custom ASCII logo (Cervus deer)
 *
 *  ** Colorful output with ANSI escape codes
 *
 *  ** Automatic aligment of values
 *
 *  ** Random tech quote on each run
 *
 *
 *  ** Uses native Cervus system calls (cervus_uptime_ns, cervus_meminfo,
 cervus_list_mounts, cervus_disk_info) and standart POSIX (uname, getenv).


 * Author: alexvoste
*/

#define _GNU_SOURCE
#include <cervus_util.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cervus.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static const char *logo[] = {"    L          ",   "   'k.i ,      ",
                             "    \";\"+U.,    ", "       \\_' -.  ",
                             "      .f  ,_.;.",   "      I ,f     ",
                             "       '       ",   NULL};

static const char *quotes[] = {
    "“The only way to go fast is to go well.” – Robert C. Martin",
    "“Simplicity is the ultimate sophistication.” – da Vinci",
    "“Talk is cheap. Show me the code.” – Linus Torvalds",
    "“Real programmers don't comment their code. If it was hard to write, it "
    "should be hard to understand.”",
    "“Cervus OS: because why not?”",
    "“Kernel panic? Nah, just a gentle nap.”",
    "“Keep it simple, stupid!” – KISS principle",
    "“Unix is basically a simple operating system, but you have to be a genius "
    "to understand the simplicity.” – Dennis Ritchie",
    "“The best way to predict the future is to implement it.”",
    "“Programs must be written for people to read, and only incidentally for "
    "machines to execute.” – Abelson & Sussman",
    "“AlexVoste: software should be predictable”",
    NULL};

static void cpuid_leaf(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c,
                       uint32_t *d) {
  asm volatile("cpuid"
               : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
               : "0"(leaf), "2"(0));
}

static const char *get_shell(void) {
  const char *s = getenv("SHELL");
  if (s && s[0])
    return s;

  static char buf[256];
  int fd = open("/etc/shell", O_RDONLY);
  if (fd < 0)
    fd = open("/mnt/etc/shell", O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
      buf[n] = '\0';
      size_t i = 0;
      while (i < sizeof(buf) && buf[i] && buf[i] != '\n' && buf[i] != '\r')
        i++;
      if (i < sizeof(buf))
        buf[i] = '\0';
      if (buf[0])
        return buf;
    }
  }
  return "/bin/csh";
}

static void print_size_human(char *buf, size_t bufsz, uint64_t bytes) {
  uint64_t mb = bytes / (1024 * 1024);
  if (mb >= 1024) {
    snprintf(buf, bufsz, "%lu.%luG", (unsigned long)(mb / 1024),
             (unsigned long)((mb % 1024) * 10 / 1024));
  } else if (mb > 0) {
    snprintf(buf, bufsz, "%luM", (unsigned long)mb);
  } else {
    snprintf(buf, bufsz, "%luK", (unsigned long)(bytes / 1024));
  }
}

static const char *random_quote(void) {
  int count = 0;

  while (quotes[count])
    count++;
  if (count == 0)
    return NULL;

  static int seeded = 0;

  if (!seeded) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    seeded = 1;
  }

  return quotes[rand() % count];
}

int main(void) {
  struct utsname un;
  if (uname(&un) != 0) {
    snprintf(un.nodename, sizeof(un.nodename), "unknown");
    snprintf(un.release, sizeof(un.release), "unknown");
    snprintf(un.machine, sizeof(un.machine), "unknown");
  }

  const char *colors[] = {"\033[1;36m", "\033[1;32m", "\033[1;33m",
                          "\033[1;35m", "\033[1;31m", "\033[1;34m"};
  const char *reset = "\033[0m";

  const char *os = "Cervus OS";
  const char *host = un.nodename;
  const char *kernel = un.release;
  const char *arch = un.machine;

  uint64_t ns = cervus_uptime_ns();
  uint64_t total_s = ns / 1000000000ULL;
  uint64_t days = total_s / 86400;
  uint64_t hours = (total_s / 3600) % 24;
  uint64_t mins = (total_s / 60) % 60;
  uint64_t secs = total_s % 60;
  char uptime_buf[64];
  if (days > 0)
    snprintf(uptime_buf, sizeof(uptime_buf), "%lud %02lu:%02lu:%02lu", days,
             hours, mins, secs);
  else
    snprintf(uptime_buf, sizeof(uptime_buf), "%02lu:%02lu:%02lu", hours, mins,
             secs);

  uint32_t a, b, c, d;
  cpuid_leaf(0x80000000, &a, &b, &c, &d);
  char cpu_brand[49] = "Unknown";
  if (a >= 0x80000004) {
    uint32_t *p = (uint32_t *)cpu_brand;
    cpuid_leaf(0x80000002, &p[0], &p[1], &p[2], &p[3]);
    cpuid_leaf(0x80000003, &p[4], &p[5], &p[6], &p[7]);
    cpuid_leaf(0x80000004, &p[8], &p[9], &p[10], &p[11]);
    cpu_brand[48] = '\0';
    const char *br = cpu_brand;
    while (*br == ' ')
      br++;
    char *out = cpu_brand;
    while (*br) {
      if (*br != ' ' || (out > cpu_brand && *(out - 1) != ' '))
        *out++ = *br;
      br++;
    }
    *out = '\0';
  }

  cervus_meminfo_t mi;
  char mem_buf[64] = "N/A";
  if (cervus_meminfo(&mi) == 0 && mi.total_bytes > 0) {
    uint64_t used = mi.used_bytes;
    uint64_t total = mi.total_bytes;
    const uint64_t MiB = 1024ULL * 1024;
    const uint64_t GiB = 1024ULL * 1024 * 1024;
    if (total >= GiB)
      snprintf(mem_buf, sizeof(mem_buf), "%lu.%02lu / %lu.%02lu GiB",
               used / GiB, (used % GiB) * 100 / GiB, total / GiB,
               (total % GiB) * 100 / GiB);
    else
      snprintf(mem_buf, sizeof(mem_buf), "%lu / %lu MiB", used / MiB,
               total / MiB);
  }

  char disk_buf[64] = "N/A";
  cervus_mount_info_t mounts[16];
  long nm = cervus_list_mounts(mounts, 16);
  if (nm > 0) {
    const char *root_dev = NULL;
    for (long i = 0; i < nm; i++) {
      if (strcmp(mounts[i].path, "/") == 0) {
        root_dev = mounts[i].device;
        break;
      }
    }
    if (root_dev) {
      for (int i = 0; i < 64; i++) {
        cervus_disk_info_t info;
        if (cervus_disk_info(i, &info) < 0)
          break;
        if (!info.present)
          continue;
        if (strcmp(info.name, root_dev) == 0) {
          print_size_human(disk_buf, sizeof(disk_buf), info.size_bytes);
          break;
        }
      }
    }
  }

  const char *shell = get_shell();
  const char *term = getenv("TERM");
  if (!term || !term[0])
    term = "unknown";
  const char *de = getenv("XDG_CURRENT_DESKTOP");
  if (!de || !de[0])
    de = getenv("DESKTOP_SESSION");
  if (!de || !de[0])
    de = NULL;

  const char *quote = random_quote();

  printf("\n");
  const int col = 14;

  for (int i = 0; logo[i]; i++) {
    printf("%s%s%s", colors[i % 6], logo[i], reset);
    int len = strlen(logo[i]);
    int spaces = col - len;
    if (spaces < 1)
      spaces = 1;
    for (int j = 0; j < spaces; j++)
      putchar(' ');
    switch (i) {
    case 0:
      printf("%sOS:%s %s%s%s", colors[1], reset, colors[3], os, reset);
      break;
    case 1:
      printf("%sHost:%s %s%s%s", colors[1], reset, colors[3], host, reset);
      break;
    case 2:
      printf("%sKernel:%s %s%s%s", colors[1], reset, colors[3], kernel, reset);
      break;
    case 3:
      printf("%sUptime:%s %s%s%s", colors[1], reset, colors[3], uptime_buf,
             reset);
      break;
    case 4:
      printf("%sShell:%s %s%s%s", colors[1], reset, colors[3], shell, reset);
      break;
    case 5:
      printf("%sCPU:%s %s%s%s", colors[1], reset, colors[3], cpu_brand, reset);
      break;
    case 6:
      printf("%sRAM:%s %s%s%s", colors[1], reset, colors[3], mem_buf, reset);
      break;
    }
    printf("\n");
  }

  printf("%s       ...%s", colors[4], reset);
  int len_dots = strlen("       ...");
  int spaces = col - len_dots;
  if (spaces < 1)
    spaces = 1;
  for (int j = 0; j < spaces; j++)
    putchar(' ');
  printf("\t%sDisk:%s %s%s%s\n", colors[1], reset, colors[3], disk_buf, reset);

  printf("%s       ...%s", colors[5], reset);
  for (int j = 0; j < spaces; j++)
    putchar(' ');
  printf("\t%sTerm:%s %s%s%s\n", colors[1], reset, colors[3], term, reset);

  if (de) {
    int extra = 1;
    printf("  ");
    for (int j = 0; j < col - 2 + extra; j++)
      putchar(' ');
    printf("%sDE:%s %s%s%s\n", colors[1], reset, colors[3], de, reset);
  }
  if (arch) {
    printf("  ");
    for (int j = 0; j < col - 2; j++)
      putchar(' ');
    printf("\t%sArch:%s %s%s%s\n", colors[1], reset, colors[3], arch, reset);
  }

  printf("%s", colors[0]);
  for (int i = 0; i < 50; i++)
    putchar('─');
  printf("%s\n", reset);

  if (quote) {
    printf("%s%s%s\n", colors[2], quote, reset);
  }

  printf("\n");
  return 0;
}
