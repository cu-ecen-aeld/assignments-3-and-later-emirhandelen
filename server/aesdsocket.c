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
#include <pthread.h>
#include <time.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static int server_fd = -1;
static volatile int running = 1;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timer_thread;

typedef struct thread_node {
    pthread_t thread_id;
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
    int completed;
    struct thread_node* next;
} thread_node_t;

static thread_node_t* thread_list_head = NULL;

void* timer_thread_func(void* arg) {
    char timestamp_buffer[256];
    time_t raw_time;
    struct tm* time_info;
    
    while (running) {
        time(&raw_time);
        time_info = localtime(&raw_time);
        
        strftime(timestamp_buffer, sizeof(timestamp_buffer), 
                "timestamp:%a, %d %b %Y %H:%M:%S %z\n", time_info);
        
        // Write timestamp to file (synchronized)
        pthread_mutex_lock(&file_mutex);
        FILE* file = fopen(DATA_FILE, "a");
        if (file) {
            fwrite(timestamp_buffer, 1, strlen(timestamp_buffer), file);
            fflush(file);
            fclose(file);
        } else {
            syslog(LOG_ERR, "Timer thread: fopen failed: %s", strerror(errno));
        }
        pthread_mutex_unlock(&file_mutex);
        
        for (int i = 0; i < 10 && running; i++) {
            sleep(1);
        }
    }
    
    return NULL;
}

typedef struct {
    int client_fd;
    char client_ip[INET_ADDRSTRLEN];
    thread_node_t* node;
} thread_data_t;

void add_thread_to_list(thread_node_t* node) {
    pthread_mutex_lock(&list_mutex);
    node->next = thread_list_head;
    thread_list_head = node;
    pthread_mutex_unlock(&list_mutex);
}

void cleanup_completed_threads() {
    pthread_mutex_lock(&list_mutex);
    thread_node_t* current = thread_list_head;
    thread_node_t* prev = NULL;
    
    while (current != NULL) {
        if (current->completed) {
            pthread_join(current->thread_id, NULL);
            
            if (prev == NULL) {
                thread_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            
            thread_node_t* to_delete = current;
            current = current->next;
            free(to_delete);
        } else {
            prev = current;
            current = current->next;
        }
    }
    pthread_mutex_unlock(&list_mutex);
}

void join_all_threads() {
    pthread_mutex_lock(&list_mutex);
    thread_node_t* current = thread_list_head;
    
    while (current != NULL) {
        pthread_mutex_unlock(&list_mutex);
        pthread_join(current->thread_id, NULL);
        pthread_mutex_lock(&list_mutex);
        
        thread_node_t* to_delete = current;
        current = current->next;
        free(to_delete);
    }
    thread_list_head = NULL;
    pthread_mutex_unlock(&list_mutex);
}

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    running = 0;
    
    if (server_fd != -1) {
        close(server_fd);
    }
    
    pthread_join(timer_thread, NULL);
    
    join_all_threads();
    
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&list_mutex);
    
    unlink(DATA_FILE);
    closelog();
    exit(0);
}

void* handle_client(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int client_fd = data->client_fd;
    char* client_ip = data->client_ip;
    thread_node_t* node = data->node;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int error_occurred = 0;
    
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    
    while (running && !error_occurred) {
        bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received < 0) {
                syslog(LOG_ERR, "recv failed for %s: %s", client_ip, strerror(errno));
            }
            break;
        }
        
        pthread_mutex_lock(&file_mutex);
        FILE* file = fopen(DATA_FILE, "a");
        if (!file) {
            syslog(LOG_ERR, "fopen failed for append");
            pthread_mutex_unlock(&file_mutex);
            error_occurred = 1;
            break;
        }
        
        fwrite(buffer, 1, bytes_received, file);
        fflush(file);
        fclose(file);
        pthread_mutex_unlock(&file_mutex);
        
        if (memchr(buffer, '\n', bytes_received)) {
            break;
        }
    }
    
    if (!error_occurred && running) {
        pthread_mutex_lock(&file_mutex);
        FILE* file = fopen(DATA_FILE, "r");
        if (file) {
            while ((bytes_received = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
                if (send(client_fd, buffer, bytes_received, 0) == -1) {
                    syslog(LOG_ERR, "send failed for %s: %s", client_ip, strerror(errno));
                    break;
                }
            }
            fclose(file);
        } else {
            syslog(LOG_ERR, "fopen failed for read");
        }
        pthread_mutex_unlock(&file_mutex);
    }
    
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(client_fd);
    
    node->completed = 1;
    free(data);
    
    return NULL;
}

int main(int argc, char* argv[]) {
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    int client_fd;
    
    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    unlink(DATA_FILE);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
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
        if (pid < 0) {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            exit(1);
        }
        if (pid > 0) {
            exit(0);
        }
        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            exit(1);
        }
    }
    
    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    if (pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "Timer thread creation failed: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    syslog(LOG_INFO, "Server listening on port %d", PORT);
    
    while (running) {
        cleanup_completed_threads();
        
        client_addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (client_fd == -1) {
            if (running) {
                syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            }
            continue;
        }
        
        thread_data_t* thread_data = malloc(sizeof(thread_data_t));
        if (!thread_data) {
            syslog(LOG_ERR, "malloc failed for thread_data");
            close(client_fd);
            continue;
        }
        
        thread_data->client_fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, thread_data->client_ip, sizeof(thread_data->client_ip));
        
        thread_node_t* node = malloc(sizeof(thread_node_t));
        if (!node) {
            syslog(LOG_ERR, "malloc failed for thread_node");
            free(thread_data);
            close(client_fd);
            continue;
        }
        
        node->client_fd = client_fd;
        strcpy(node->client_ip, thread_data->client_ip);
        node->completed = 0;
        node->next = NULL;
        thread_data->node = node;
        
        if (pthread_create(&node->thread_id, NULL, handle_client, thread_data) != 0) {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(errno));
            free(thread_data);
            free(node);
            close(client_fd);
            continue;
        }
        
        add_thread_to_list(node);
    }
    
    close(server_fd);
    pthread_join(timer_thread, NULL);
    join_all_threads();
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&list_mutex);
    unlink(DATA_FILE);
    closelog();
    return 0;
}