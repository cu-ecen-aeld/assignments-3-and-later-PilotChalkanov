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


void write_to_file(char* file_name, char* file_content){
  int fd;
  fd = open(file_name, O_WRONLY | O_APPEND , S_IRUSR | S_IWUSR);
  if (fd == -1){
    syslog(LOG_ERR, "Error opening file: %s", file_name);
    exit(1);
  }


  ssize_t bytes_written = write(fd, file_content, strlen(file_content));
  if (bytes_written == -1){
    syslog(LOG_ERR, "Failed to write to file: %s", file_name);
    close(fd);
    return;
  }

  syslog(LOG_INFO, "Successfully wrote to file: %s", file_name);
  close(fd);
  }

int main(int argc, char *argv[]) {

if (argc != 3) {
        syslog(LOG_ERR, "Usage: %s <file_name> <file_content>", argv[0]);
        return 1;
    }

    write_to_file(argv[1], argv[2]);
}