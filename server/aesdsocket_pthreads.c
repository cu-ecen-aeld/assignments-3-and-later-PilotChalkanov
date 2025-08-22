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

#define PORT "9000"
#define BACKLOG 10
#define FILEPATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024


int sock_fd = -1;
int file_fd = -1;
FILE *data_file = NULL;
SLIST_HEAD(thread_list, thread_node);

static struct thread_list g_threads;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void alarm_handler(int signo) {
    (void)signo;
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[80];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S\n", timeinfo);
    printf("Debug: %s\n", buffer);
    pthread_mutex_lock(&g_mutex);
    write(file_fd, buffer, strlen(buffer));
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

void cleanup() {
    thread_node *head;
    while (!SLIST_EMPTY(&g_threads)) {
        head = SLIST_FIRST(&g_threads);
        SLIST_REMOVE_HEAD(&g_threads, next);
        pthread_join(head->thread_id, NULL);
        free(head);
    }
    pthread_mutex_destroy(&g_mutex);

};

void signal_handler(int signo) {
    (void) signo;
    cleanup();
    exit(EXIT_SUCCESS);
}

static int init_server_addrinfo(const char *port, struct addrinfo **serv_info) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; //ip_v4 or v6
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
    int fd = open(FILEPATH, O_RDWR | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1) {
        syslog(LOG_ERR, "Error opening file: %s", FILEPATH);
        return -1;
    }
    return fd;
}

// thread func - accepts void arg - thread_node struct
void  * handle_client(void *arg) {
    thread_node *node = (thread_node *)arg;
    char wbuffer[BUFFER_SIZE];
    char rbuffer[BUFFER_SIZE];
    ssize_t bytes_read;
    int file_fd = node->file_fd;
    int client_id = node->client_fd;
    char *client_ip = node->client_ip;
    printf("Debug: Executing from thread %lu\n", pthread_self());
    printf("Debug: Client IP: %s\n", client_ip);
    while (1) {
        ssize_t bytes_received = recv(client_id, wbuffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            syslog(LOG_ERR, "recv error or connection closed");
            pthread_exit(NULL);
        }
        wbuffer[bytes_received] = '\0';

        pthread_mutex_lock(&g_mutex);
        if (write(file_fd,wbuffer, (size_t)bytes_received) != (size_t)bytes_received) {
            syslog(LOG_ERR, "Failed to write to file");
            pthread_mutex_unlock(&g_mutex);
            break;
        }
        pthread_mutex_unlock(&g_mutex);
        if (wbuffer[bytes_received - 1] == '\n') {
            lseek(file_fd, 0, SEEK_SET);

            bytes_read = read(file_fd, rbuffer, BUFFER_SIZE);
            if (bytes_read == -1) {
                char* err = strerror(errno);
                syslog(LOG_ERR, "Failed to read from file: %s", err);
                printf("Debug: %s", err);
                pthread_exit(NULL);
            }
            while ((bytes_read = read(file_fd, rbuffer, BUFFER_SIZE)) > 0) {
                if (send(client_id, rbuffer, bytes_read, 0) == -1) {
                    syslog(LOG_ERR, "send error");
                    pthread_exit(NULL);
                }
            }
            lseek(file_fd, 0, SEEK_END);
        }
        close(node->client_fd);
        memset(wbuffer, 0, sizeof(wbuffer));
        memset(rbuffer, 0, sizeof(rbuffer));
        node->is_completed = true;
        pthread_exit(NULL);
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    // addr-info init
    int optval = 1;
    int daemon_mode = 0;

    SLIST_INIT(&g_threads);

    struct addrinfo *serv_info, *p;
    if (init_server_addrinfo(PORT, &serv_info) != 0) {
        return -1;
    }

    // socket init - iterate over the addr_info LL and try to bind
    for (p = serv_info; p != NULL; p = p->ai_next) {
        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd == -1) continue;

        // socket options - SOL_SOCKET - protocol lvl for which opts apply
        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            close(sock_fd);
            return -1;
        }
        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == 0) break;
        // if bind fails - closing the sock
        close(sock_fd);
        syslog(LOG_ERR, "Failed to bind socket");
    }
    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind socket");
        return -1;
    }
    // done with serv_info
    freeaddrinfo(serv_info);

    /*** singal handling ***/
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to setup signal handlers");
        return -1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    // daemonize if needed
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
    printf("Listening on locahost: %s\n", PORT);
    // open file and start timer
    file_fd = open_file_for_write();
    start_timestamp_timer();

    thread_node *n;
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof(client_addr);

        int client_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &addr_size);
        if (client_fd == -1) continue;

        thread_node *node = malloc(sizeof(struct thread_node));
        if (!node) {
            syslog(LOG_ERR, "Failed to allocate memory");
            continue;
        }
        int len = sizeof(node->client_ip);
        struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)&client_addr;
        struct in_addr *ip_address_ptr = &ipv4_addr->sin_addr;
        inet_ntop(client_addr.ss_family, ip_address_ptr, node->client_ip, len);

        syslog(LOG_INFO, "Accepted connection from %s", node->client_ip);

        node->client_fd = client_fd;
        node->file_fd = file_fd;
        node->mutex=g_mutex;
        if (pthread_create(&node->thread_id, NULL, &handle_client, node) != 0) {
            syslog(LOG_ERR, "Failed to create thread");
            return -1;
        };
        SLIST_INSERT_HEAD(&g_threads, node, next);
        SLIST_FOREACH(n, &g_threads, next) {
            if (!n->is_completed) {
                break;
            }
            pthread_join(n->thread_id, NULL);
        }
    }
}