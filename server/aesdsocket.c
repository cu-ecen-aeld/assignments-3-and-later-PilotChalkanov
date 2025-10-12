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
#include <pthread.h>
#include <sys/time.h>
#include "thread_node.h"
#include "../aesd-char-driver/aesd_ioctl.h"

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#define PORT "9000"
#define BACKLOG 10

#if USE_AESD_CHAR_DEVICE
#define FILEPATH "/dev/aesdchar"
#else
#define FILEPATH "/var/tmp/aesdsocketdata"
#endif

#define BUFFER_SIZE 1024

int sock_fd = -1;
int file_fd = -1;
FILE *data_file = NULL;
SLIST_HEAD(thread_list, thread_node);

static struct thread_list g_threads;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

#if !USE_AESD_CHAR_DEVICE
void alarm_handler(int signo, siginfo_t *info, void *context) {
    (void)signo;
    (void)info;
    (void)context;
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "timestamp:%a %b %e %H:%M:%S %Y\n", timeinfo);

    pthread_mutex_lock(&g_mutex);
    if (file_fd != -1) {
        write(file_fd, buffer, strlen(buffer));
    }
    pthread_mutex_unlock(&g_mutex);
}

void start_timestamp_timer() {
    struct itimerval delay;
    struct sigaction sa = {0};
    sa.sa_sigaction = alarm_handler;
    sa.sa_flags = SA_SIGINFO;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    delay.it_value.tv_sec = 10;
    delay.it_value.tv_usec = 0;
    delay.it_interval.tv_sec = 10;
    delay.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &delay, NULL)) {
        perror("setitimer");
        return;
    }
}
#endif

void cleanup() {
    thread_node *head;
    while (!SLIST_EMPTY(&g_threads)) {
        head = SLIST_FIRST(&g_threads);
        SLIST_REMOVE_HEAD(&g_threads, next);
        pthread_join(head->thread_id, NULL);
        free(head);
    }
    pthread_mutex_destroy(&g_mutex);
    if (sock_fd != -1) close(sock_fd);
    if (file_fd != -1) close(file_fd);
    if (data_file) fclose(data_file);
#if !USE_AESD_CHAR_DEVICE
    remove(FILEPATH);
#endif
    syslog(LOG_INFO, "Caught signal, exiting");
    closelog();
}

void signal_handler(int signo) {
    (void) signo;
    cleanup();
    exit(EXIT_SUCCESS);
}

static int init_server_addrinfo(const char *port, struct addrinfo **serv_info) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rc = getaddrinfo(NULL, port, &hints, serv_info);
    if (rc != 0) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(rc));
        return -1;
    }
    return 0;
}

int open_file_for_write() {
#if USE_AESD_CHAR_DEVICE
    int fd = open(FILEPATH, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
#else
    int fd = open(FILEPATH, O_RDWR | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
#endif
    if (fd == -1) {
        syslog(LOG_ERR, "Error opening file: %s", FILEPATH);
        syslog(LOG_ERR, "Err: %s", strerror(errno));
        return -1;
    }
    return fd;
}

void * handle_client(void *arg) {
    // cast here to avoid warnings when compiling
    thread_node *node = (thread_node *)arg;
    char wbuffer[BUFFER_SIZE];
    char rbuffer[BUFFER_SIZE];
    ssize_t bytes_read;

    int client_id = node->client_fd;
    int client_file_fd;
    while (1) {
        ssize_t bytes_received = recv(client_id, wbuffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            syslog(LOG_ERR, "recv error or connection closed");
            break;
        }
        wbuffer[bytes_received] = '\0';

#if USE_AESD_CHAR_DEVICE
        client_file_fd = open_file_for_write();
#else
        client_file_fd = node->file_fd;
#endif

        if (strncmp(wbuffer, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
            unsigned int x, y;
            if (sscanf(wbuffer + 19, "%u,%u", &x, &y) == 2) {
                struct aesd_seekto seekto;
                seekto.write_cmd = x;
                seekto.write_cmd_offset = y;

                if(ioctl(client_file_fd, AESDCHAR_IOCSEEKTO, &seekto) < 0) {
                    syslog(LOG_ERR, "ioctl AESDCHAR_IOCSEEKTO failed: %s", strerror(errno));
                    close(client_file_fd);
                    break;
                }

                pthread_mutex_lock(&g_mutex);
                while ((bytes_read = read(client_file_fd, rbuffer, BUFFER_SIZE)) > 0) {
                    if (send(client_id, rbuffer, bytes_read, 0) == -1) {
                        syslog(LOG_ERR, "send error");
                        pthread_mutex_unlock(&g_mutex);
                        close(client_file_fd);
                        break;
                    }
                }
                pthread_mutex_unlock(&g_mutex);
            }
            close(client_file_fd);
            continue;
        }

        pthread_mutex_lock(&g_mutex);
        ssize_t bytes_written = write(client_file_fd, wbuffer, (size_t)bytes_received);
        if (bytes_written != bytes_received) {
            syslog(LOG_ERR, "Failed to write to file");
            pthread_mutex_unlock(&g_mutex);
            break;
        }

        if (wbuffer[bytes_received - 1] == '\n') {
#if USE_AESD_CHAR_DEVICE
            close(client_file_fd);
            client_file_fd = open_file_for_write();
            if (client_file_fd == -1) {
                syslog(LOG_ERR, "Failed to reopen file for reading");
                pthread_mutex_unlock(&g_mutex);
                break;
            }
#else
            lseek(client_file_fd, 0, SEEK_SET);
#endif

            while ((bytes_read = read(client_file_fd, rbuffer, BUFFER_SIZE)) > 0) {
                if (send(client_id, rbuffer, bytes_read, 0) == -1) {
                    syslog(LOG_ERR, "send error");
                    pthread_mutex_unlock(&g_mutex);
                    return NULL;
                }
            }

            if (bytes_read < 0) {
                printf("Failed to read from file: %s", strerror(errno));
                syslog(LOG_ERR, "Failed to read from file: %s", strerror(errno));
            }
#if !USE_AESD_CHAR_DEVICE
            lseek(client_file_fd, 0, SEEK_END);
#endif
        }
        pthread_mutex_unlock(&g_mutex);

        if (wbuffer[bytes_received - 1] == '\n') {
            break;
        }
    }
    close(node->client_fd);
    node->is_completed = true;
    return NULL;
}


int main(int argc, char *argv[]) {
    int optval = 1;
    int daemon_mode = 0;

    SLIST_INIT(&g_threads);

    // prepare the addrinfo structure
    struct addrinfo *serv_info, *p;
    if (init_server_addrinfo(PORT, &serv_info) != 0) {
        return -1;
    }

    for (p = serv_info; p != NULL; p = p->ai_next) {
        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd == -1) continue;

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            close(sock_fd);
            return -1;
        }
        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == 0) break;

        close(sock_fd);
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
    }
    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind socket");
        return -1;
    }

    freeaddrinfo(serv_info);

    // set singla handlers
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to setup signal handlers");
        return -1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }
    if (daemon_mode) {
        if (daemon(0, 0) == -1) {
            syslog(LOG_ERR, "Failed to daemonize");
            return -1;
        }
    }

    if (listen(sock_fd, 1000) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket");
        return -1;
    }

#if !USE_AESD_CHAR_DEVICE
    file_fd = open_file_for_write();
    start_timestamp_timer();
#endif

    // main loop for accepting client conn
    thread_node *n, *tmp;
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof(client_addr);

        int client_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &addr_size);
        if (client_fd == -1) continue;

        thread_node *node = malloc(sizeof(struct thread_node));
        if (!node) {
            syslog(LOG_ERR, "Failed to allocate memory");
            close(client_fd);
            continue;
        }

        struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)&client_addr;
        inet_ntop(AF_INET, &ipv4_addr->sin_addr, node->client_ip, sizeof(node->client_ip));

        syslog(LOG_INFO, "Accepted connection from %s", node->client_ip);

        node->client_fd = client_fd;
        node->file_fd = file_fd;
        node->is_completed = false;

        if (pthread_create(&node->thread_id, NULL, &handle_client, node) != 0) {
            syslog(LOG_ERR, "Failed to create thread");
            close(client_fd);
            free(node);
            continue;
        }

        SLIST_INSERT_HEAD(&g_threads, node, next);

        SLIST_FOREACH_SAFE(n, &g_threads, next, tmp) {
            if (n->is_completed) {
                pthread_join(n->thread_id, NULL);
                SLIST_REMOVE(&g_threads, n, thread_node, next);
                free(n);
            }
        }
    }

    cleanup();
    return 0;
}