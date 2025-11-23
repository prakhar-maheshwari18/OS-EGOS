#include "egos.h"
#include "servers.h"
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {

    // Checks for correct way of using wcl
    if (argc < 2) {
        printf("Syntax of wcl - wcl FILE_NAME \n");
        exit(1);
    }
    
    // This variable acts as a bucket to hold the sum of lines from all files combined.
    int grand_total = 0; 

    // Loop through every argument passed after the program name
    for (int i = 1; i < argc; i++) {
       
        // Ask the OS to find the "inode" (Machine ID) for the filename string.
        int file_ino = dir_lookup(0, argv[i]);

        //If dir_lookup returns a negative number, the file doesn't exist.
        if (file_ino < 0) {
            printf("wcl: cannot open file '%s'\n", argv[i]);
            continue;
        }

        // 'current_file_lines' counts lines ONLY for the file we are currently reading.
        int current_file_lines = 0;   
        int offset = 0;               // Keeps track of where we are reading in the file (0, 4096, 8192...)
        char buf[BLOCK_SIZE];         // A 4KB container to hold the data we read from disk

        // We keep asking the OS for blocks of data until we hit the end.
        while (1) {
            // Read one block (4KB) from the file at the current offset position
            int status = file_read(file_ino, offset, buf);

            // If the OS returns an error (negative status), stop reading this file.
            if (status < 0) {
                break;
            }

            // We look at every single character inside the 4KB buffer we just grabbed.
            int finished = 0;
            for (int j = 0; j < BLOCK_SIZE; j++) {          
                
                // If we find \0, it means the file text has ended
                if (buf[j] == '\0') {
                    finished = 1;
                    break;
                }

                // If we find a \n, we increase the counter for this file.
                if (buf[j] == '\n') {
                    current_file_lines++;
                }
            }
            
            // If we found the end of the file in the loop above, break out of the while loop.
            if (finished) {
                break;
            }

            // Otherwise, move our "bookmark" forward by 4KB to read the next chunk.
            offset += BLOCK_SIZE;
        }
       
        // Take the count from the file we just finished and add it to the main total.
        grand_total += current_file_lines;
    }

    // Print the final calculated number.
    printf("%d\n", grand_total);
    
    return 0;
}
