#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]){
    // Inialize syslog with LOG_USER facility
    openlog("writer", LOG_PID, LOG_USER);

    // Check argument count
    if(argc < 3){
        if(argc == 1){
            syslog(LOG_ERR, "Both writefile and writestr are missing");
            fprintf(stderr, "Error: Both writefile and writestr are missing\n");
        } else if (argc == 2){
            syslog(LOG_ERR, "Writestr is missing");
            fprintf(stderr, "Error: Writestr is missing\n");
        }

        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        exit(EXIT_FAILURE);
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    //Log debug message about writing
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    //Open file for writing (creates if doesn't exist)
    FILE *file = fopen(writefile, "w");
    if(file == NULL){
        syslog(LOG_ERR, "Failed to open file %s", writefile);
        perror("Error");
        closelog();
        exit(EXIT_FAILURE);
    }

    //Write string to file
    if(fputs(writestr, file) == EOF) {
        syslog(LOG_ERR, "Failed to write to file %s", writefile);
        perror("Error");
        fclose(file);
        closelog();
        exit(EXIT_FAILURE);
    }

    //Close file
    if(fclose(file) != 0){
        syslog(LOG_ERR, "Failed to properly close file %s", writefile);
        perror("Error");
        closelog();
        exit(EXIT_FAILURE);
    }

    printf("A file called '%s' containing new content: '%s' has been created/updated\n", writefile, writestr);

    closelog();
    return EXIT_SUCCESS;
}