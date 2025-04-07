#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "draw.h"
#include "process.h"
#include "term.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,"Usage: editor <filename>\n");
        exit(1);
    }
    term_setup();
    buffer_find_file(argv[1]);
    while(1) {
        editor_refresh();
        editor_process(term_read(STDIN_FILENO));
    }
    return 0;
}
