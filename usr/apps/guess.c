#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void guess_game(){
    unsigned char secret_number,guess;
    unsigned char attempts = 0;
    char input_buffer[5];

    srand((unsigned int)time(NULL));

    secret_number = (unsigned char)(((rand() & 127) * 100) >> 7) + 1;
    puts("Made up a number 1-100");

    while(1) {
        puts("Your answers: ");
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
            continue;
        }
        guess = (unsigned char)atoi(input_buffer);
        attempts++;

        if (guess > secret_number) {
            puts("Less number!");
        } else if (guess < secret_number) {
            puts("More number!");
        } else {
            printf("Your guessed! Attepts: %d\n", (int)attempts);
            break;
        }
    }
}

int main(){
    guess_game();
    return 0;
}