// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libmimusb contributors
/* The simulated board as a program, for the tests of the language bindings: it creates a
 * pseudo-terminal, starts the firmware core on its master side, prints the slave's name on the
 * first line of stdout and runs until stdin is closed. Open that name as the board's serial port. */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE 1 /* cfmakeraw */
#include "fake_board.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
    struct termios tio;
    char buffer[64];
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 || ptsname(master) == NULL) {
        fprintf(stderr, "cannot create a pseudo-terminal\n");
        return 1;
    }
    tcgetattr(master, &tio);
    cfmakeraw(&tio);
    tcsetattr(master, TCSANOW, &tio);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    if (fake_board_start(master) != 0)
        return 1;
    printf("%s\n", ptsname(master));
    fflush(stdout);
    while (read(STDIN_FILENO, buffer, sizeof(buffer)) > 0) {
    }
    fake_board_stop();
    return 0;
}
