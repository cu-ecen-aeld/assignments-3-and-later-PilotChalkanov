#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BACKLOG 10
#define BUFFER_SIZE 1024
#define FILEPATH "/var/tmp/aesdsocketdata"

int sockfd = -1;

/**
 *Prints the Protocol Family, IP and PORT
 */
void print_servinfo(struct addrinfo *servinfo) {
    /**
     *Prints the Protocol Family, IP and PORT
     */
    struct addrinfo *p;
    char ipstr[INET_ADDRSTRLEN];

    for (p = servinfo; p != NULL; p = p->ai_next) {
        void *addr;
        char *ipver;
        int port;

        struct sockaddr_in *ipv4 = (struct sockaddr_in *) p->ai_addr;
        addr = &(ipv4->sin_addr);
        ipver = "IPv4";
        port = ntohs(ipv4->sin_port);

        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        printf("  %s: %s, Port: %d\n", ipver, ipstr, port);
    }
}

/*
 * handler for SIGINT and SIGTERM
 *
 */
void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("Caught signal %d\n", signo);
        syslog(LOG_INFO, "Caught signal %d\n", signo);
        if (sockfd != -1) {
            close(sockfd);
            printf("Socket closed successfully.\n");
        }
        exit(EXIT_SUCCESS);
    }
    if (signo == SIGCHLD) {
        printf("Caught signal %d\n", signo);
    }
    if (signo == SIGSEGV) {
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            printf("Handled SIGCHLD: Cleaned up a child process.\n");
        }
    }
    fprintf(stderr, "Unexpected signal!\n");
    exit(EXIT_FAILURE);
}

void *get_in_addr(struct sockaddr *sa) {
    return &((struct sockaddr_in *) sa)->sin_addr;
}

int write_to_file(char *file_name, char *file_content) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Current working directory: %s\n", cwd);
    } else {
        perror("getcwd() error");
    }
    const int fd = open(file_name, O_WRONLY | O_APPEND | O_CREAT,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1) {
        syslog(LOG_ERR, "Error opening file: %s", file_name);
        printf("Error opening file: %s\n", file_name);
        return 1;
    }


    const ssize_t bytes_written = write(fd, file_content, strlen(file_content));

    if (bytes_written == -1) {
        syslog(LOG_ERR, "Failed to write to file: %s", file_name);
        close(fd);
        return 1;
    }

    syslog(LOG_USER, "Successfully wrote to file: %s", file_name);
    printf("Successfully wrote to file: %s", file_name);
    if (close(fd) == -1) {
        syslog(LOG_ERR, "Error closing file: %s", file_name);
        return 1;
    }
    return 0;
}

char *read_from_file(char *file_name) {
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Error getting file size");
        close(fd);
        return NULL;
    }

    size_t file_size = st.st_size;
    char *buf = (char *) malloc(file_size + 1);
    if (buf == NULL) {
        perror("Memory allocation failed");
        close(fd);
        return NULL;
    }

    ssize_t bytes_read = read(fd, buf, file_size);
    if (bytes_read == -1) {
        perror("Error reading file");
        free(buf);
        close(fd);
        return NULL;
    }

    buf[bytes_read] = '\0';
    close(fd);
    return buf;
}


int main(int argc, char *argv[]) {
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo, *p;
    // int sockfd;
    int yes = 1;
    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];

    int daemon_mode = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
        printf("Going in daemon started.\n");
    }


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE; // Fill in my IP for me

    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    print_servinfo(servinfo);

    // SIGNAL handling
    sa.sa_handler = &signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            close(sockfd);
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }
    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        freeaddrinfo(servinfo);
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        freeaddrinfo(servinfo);
        exit(1);
    }
    socklen_t sin_size;
    int new_fd;
    struct sockaddr_storage their_addr;

    printf("server: waiting for connections...\n");
    syslog(LOG_INFO, "server: waiting for connections...\n");

    while (1) {
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *) &their_addr),
                  s, sizeof s);
        printf("server: Accepted connection from %s\n", s);
        syslog(LOG_INFO, "server: Accepted connection from %s\n", s);

        //makes the process daemonic
        if (daemon_mode == 1) {
         if (daemon(0, 0) == -1) {
             perror("daemon");
             close(new_fd);
             continue;
         }
            }

        ssize_t bytes_received = recv(new_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            printf("server: Received data: %s\n", buffer);

            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                close(sockfd); // Close the listening socket in the child process
                printf("server: writing data: %s\n", buffer);
                write_to_file(FILEPATH, buffer);
                // Send the data back to the client
                char *ret = read_from_file(FILEPATH);
                printf("server: reading data: %s\n", ret);
                if (send(new_fd, buffer, strlen(buffer), 0) == -1) {
                    perror("send");
                }
                free(ret);
                exit(0);
            } else if (pid < 0) {
                perror("fork");
            }
        } else if (bytes_received == -1) {
            perror("recv");
        }

        close(new_fd);
        return 0;
    }
}
