#include "../apps/cervus_user.h"

CERVUS_MAIN(echo_main)
{
	const char *cwd_str = get_cwd_flag(argc, argv);
	int out_fd = 1;
	int no_newline = 0;
	int arg_start = argc;

	for (int i = 1; i < argc; i++) {
		if (is_shell_flag(argv[i])) continue;

		if (strcmp(argv[i], "-n") == 0) {
			no_newline = 1;
			continue;
		}

		if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-a") == 0) && i + 1 < argc) {
			int append = (argv[i][1] == 'a');
			i++;
			while (i < argc && is_shell_flag(argv[i])) i++;
			if (i >= argc) break;

			char path[512];
			resolve_path(cwd_str, argv[i], path, sizeof(path));

			int flags = O_WRONLY | O_CREAT;
			flags |= append ? O_APPEND : O_TRUNC;

			int fd = open(path, flags, 0644);
			if (fd < 0) {
				wse("echo: cannot open '");
				wse(argv[i]);
				wse("' for writing\n");
				exit(1);
			}
			if (out_fd != 1) close(out_fd);
			out_fd = fd;
			continue;
		}

		arg_start = i;
		break;
	}

	int first = 1;
	for (int i = arg_start; i < argc; i++) {
		if (is_shell_flag(argv[i])) continue;
		if (!first) write(out_fd, " ", 1);
		first = 0;

		const char *a = argv[i];
		for (int j = 0; a[j]; j++) {
			if (a[j] != '\\' || !a[j+1]) {
				write(out_fd, &a[j], 1);
				continue;
			}
			j++;
			char c;
			switch (a[j]) {
				case 'n':  c = '\n'; break;
				case 't':  c = '\t'; break;
				case 'r':  c = '\r'; break;
				case '\\': c = '\\'; break;
				case '"':  c = '"';  break;
				case '\'': c = '\''; break;
				case '0':  c = '\0'; break;
				default:
					write(out_fd, "\\", 1);
					c = a[j];
					break;
			}
			write(out_fd, &c, 1);
		}
	}

	if (!no_newline) write(out_fd, "\n", 1);

	if (out_fd != 1) close(out_fd);
	exit(0);
}