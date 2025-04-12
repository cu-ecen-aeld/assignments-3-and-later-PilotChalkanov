#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <netdb.h>

#define PORT "9000"
#define BACKLOG 10
#define FILEPATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int sockfd = -1;
int client_fd = -1;
FILE *data_file = NULL;

void cleanup() {
    if (client_fd != -1) close(client_fd);
    if (sockfd != -1) close(sockfd);
    if (data_file) fclose(data_file);
    remove(FILEPATH);
    syslog(LOG_INFO, "Caught signal, exiting");
    closelog();
}

void signal_handler(int signo) {
    cleanup();
    exit(EXIT_SUCCESS);
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        fprintf(data_file, "%s", buffer);
        fflush(data_file);

        fseek(data_file, 0, SEEK_SET);
        char file_buffer[BUFFER_SIZE];
        while (fgets(file_buffer, BUFFER_SIZE, data_file)) {
            send(client_fd, file_buffer, strlen(file_buffer), 0);
        }
    }
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage client_addr;
    socklen_t addr_size;
    char client_ip[INET_ADDRSTRLEN];
    int yes = 1;

    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            close(sockfd);
            return -1;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;

        close(sockfd);
    }

    freeaddrinfo(res);

    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind socket");
        return -1;
    }

    if (daemon_mode) {
        if (daemon(0, 0) == -1) {
            syslog(LOG_ERR, "Failed to daemonize");
            return -1;
        }
    }

    if (listen(sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket");
        return -1;
    }

    data_file = fopen(FILEPATH, "a+");
    if (!data_file) {
        syslog(LOG_ERR, "Failed to open data file");
        return -1;
    }

    while (1) {
        addr_size = sizeof client_addr;
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd == -1) continue;

        inet_ntop(client_addr.ss_family, &((struct sockaddr_in *)&client_addr)->sin_addr, client_ip, sizeof client_ip);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        client_fd = -1;
    }

    cleanup();
    return 0;
}