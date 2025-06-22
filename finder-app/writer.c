#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d provided, 2 expected.", argc - 1);
        printf("Please use it as following: %s <file_path> <string_to_write>\n", argv[0]);
        closelog();
        return 1;
    }

    char *writefile = argv[1];
    char *writestr = argv[2];

    syslog(LOG_DEBUG, "Writing \"%s\" to %s", writestr, writefile);

    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open file %s for writing: %s", writefile, strerror(errno));
        printf("Could not open file %s for writing.\n", writefile);
        closelog();
        return 1;
    }

    if (fprintf(file, "%s", writestr) < 0) {
        syslog(LOG_ERR, "Failed to write to file %s: %s", writefile, strerror(errno));
        printf("Could not write to file %s.\n", writefile);
        fclose(file);
        closelog();
        return 1;
    }

    fclose(file);
    closelog();

    return 0;
}
