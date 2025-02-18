//
// Created by chalki on 18.02.2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>


int write_to_file(char* file_name, char* file_content)
{
    const int fd = open(file_name, O_WRONLY | O_APPEND | O_CREAT,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1)
    {
        syslog(LOG_ERR, "Error opening file: %s", file_name);
        return 1;
    }


    const ssize_t bytes_written = write(fd, file_content, strlen(file_content));

    if (bytes_written == -1)
    {
        syslog(LOG_ERR, "Failed to write to file: %s", file_name);
        close(fd);
        return 1;
    }

    syslog(LOG_USER, "Successfully wrote to file: %s", file_name);
    if (close(fd) == -1)
    {
        syslog(LOG_ERR, "Error closing file: %s", file_name);
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    openlog(NULL, LOG_PID | LOG_NDELAY, LOG_USER);
    if (argc != 3)
    {
        syslog(LOG_ERR, "Usage: %s <file_name> <file_content>", argv[0]);
        return 1;
    }

    write_to_file(argv[1], argv[2]);
}
