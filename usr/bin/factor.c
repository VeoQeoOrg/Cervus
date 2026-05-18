#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cervus_util.h>

int is_number(const char *s) {
	if (*s == '\0') return 0;

	while (*s != '\0') {
		if (!isdigit(*s)) {
			return 0;
		}
		s++;
	}

	return 1;
}

int main(int argc, char **argv) {
/*	if (argc != 2) {
		fprintf(stderr, C_RED "factor: variable not specified\n" C_RESET);
        return 1;
	}*/

	if (argc != 2) {
		printf(C_CYAN "Cervus factor, write integer and press enter. exit or ctrl+c to quit\n" C_RESET);
		while (1) {
			char num[128];
			fgets(num, sizeof(num), stdin);
			num[strcspn(num, "\n")] = '\0';

			if (strcmp(num, "exit") == 0) {
				printf("exiting..");
				return 1;
			}
		
			if (!is_number(num)) {
				fprintf(stderr, C_RED "factor: %s isn't number\n" C_RESET, num);
				continue;
			}
			
			int n = atoi(num);
			int d = 2;
			
			printf("%d: ", n);
			do {
				if (n % d == 0) {
					printf("%d ", d);
					n = n / d;
				} else {
					d++;
				}
			} while (n > 1);
			printf("\n");
		}
	}

	if (!is_number(argv[1])) {
		fprintf(stderr, C_RED "factor: %s isn't number\n" C_RESET, argv[1]);
        return 1;
	}

	int n = atoi(argv[1]);
	int d = 2;
	
	printf("%d: ", n);
	do {
		if (n % d == 0) {
			printf("%d ", d);
			n = n / d;
		} else {
			d++;
		}
	} while (n > 1);
	printf("\n");

	return 0;
}
