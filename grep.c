#include "egos.h"
#include "servers.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Syntax of grep - grep PATTERN FILE_NAME \n");
        exit(1);
    }

    char* pattern = argv[1];

    for (int i = 2; i < argc; i++) {
        int file_ino = dir_lookup(0, argv[i]);
        if (file_ino < 0) {
            printf("grep: cannot open file '%s'\n", argv[i]);
            continue;
        }

        /* Dynamic "Rubber Band" Buffer Setup */
        char buf[BLOCK_SIZE];
        int capacity = 128;
        char* line_buf = malloc(capacity);
        int line_pos = 0;
        int offset = 0;
        int finished = 0;
       
        /* NEW: Flag to track matches */
        int found_any = 0;

        while (1) {
            int status = file_read(file_ino, offset, buf);
            if (status < 0) break;

            for (int j = 0; j < BLOCK_SIZE; j++) {
                char c = buf[j];

                if (c == '\0') {
                    finished = 1;
                    break;
                }

                if (c == '\n') {
                    line_buf[line_pos] = 0;
                    if (strstr(line_buf, pattern)) {
                        term_write(line_buf, strlen(line_buf));
                        term_write("\n", 1);
                        found_any = 1; /* NEW: Mark success */
                    }
                    line_pos = 0;
                } else {
                    if (line_pos >= capacity - 1) {
                        int new_capacity = capacity * 2;
                        char* new_buf = malloc(new_capacity);
                        memcpy(new_buf, line_buf, capacity);
                        free(line_buf);
                        line_buf = new_buf;
                        capacity = new_capacity;
                    }
                    line_buf[line_pos++] = c;
                }
            }

            if (finished) break;
            offset += BLOCK_SIZE;
        }
       
        /* NEW: Check flag and report if missing */
        if (!found_any) {
            printf("grep: pattern '%s' not found in '%s'\n", pattern, argv[i]);
        }

        free(line_buf);
    }
    return 0;
}
