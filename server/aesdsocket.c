#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static int server_fd = -1;
static volatile int running = 1;

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    running = 0;
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE);
    closelog();
    exit(0);
}

int main(int argc, char* argv[]) {
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    int client_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char client_ip[INET_ADDRSTRLEN];

    openlog("aesdsocket", LOG_PID, LOG_USER);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) exit(1);
        if (pid > 0) exit(0);
    }

    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }

    while (running) {
        client_addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (running) syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        FILE* file = fopen(DATA_FILE, "a+");
        if (!file) {
            syslog(LOG_ERR, "fopen failed");
            close(client_fd);
            continue;
        }

        while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, bytes_received, file);
            fflush(file);
            if (memchr(buffer, '\n', bytes_received)) break;
        }
        fclose(file);

        file = fopen(DATA_FILE, "r");
        if (file) {
            while ((bytes_received = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
                send(client_fd, buffer, bytes_received, 0);
            }
            fclose(file);
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
    }

    close(server_fd);
    unlink(DATA_FILE);
    closelog();
    return 0;
}