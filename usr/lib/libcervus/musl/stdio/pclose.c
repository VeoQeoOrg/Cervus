#include "stdio_impl.h"
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

int pclose(FILE *f)
{
	int status, r;
	pid_t pid = f->pipe_pid;
	fclose(f);
	while ((r=waitpid(pid, &status, 0)) < 0 && errno == EINTR);
	if (r<0) return -1;
	return status;
}
