/**
 * Notes:
 * * Does not need to create directories which do not exist. You can assume the directory
 * is created by the caller.
 * * Use syslog for logging "LOG_USER"
 * * Write in the format "Writing <string> to <file>" logs to LOG_DEBUG
 * * Write error logs to LOG_ERR
 */

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>


int main(int argc, char* argv[]) {
    openlog("writer", LOG_PID, LOG_USER);

    if (argc != 3 ) {
        syslog(LOG_ERR, "Missing arguments! usage: writer.sh {write_file} {write_str}\n");
        return 1;
    }

    char* file_path = argv[1];
    char* text = argv[2];
    size_t text_length = strnlen(text, 4096); // assignment doesn't require a max input, but it's been beaten into me to use bounded string functions over the unbounded ones.
    if (text_length >= 4096) { // null terminate if input is greater than bounds.
        text[4095] = '\0';
    }

    FILE* file = fopen(file_path, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open file!\n");
        return 1;
    }
    
    syslog(LOG_DEBUG, "Writing %s to %s", text, file_path);
    fwrite(text, sizeof(char), text_length, file);
    fclose(file);
}
