/**
 * build:
 * make clean && make all && ./aesdsocket
 * make clean && make all && ./aesdsocket -d
 * valgrind ./aesdsocket
 *
 * test/debug:
 * echo "test" | nc 127.0.0.1 9000
 * echo "The quick brown fox jumps over the lazy dog" | nc 127.0.0.1 9000
 * ./assignment-autotest/test/assignment5/sockettest.sh
 * journalctl SYSLOG_IDENTIFIER=aesdsocket -p debug
 * journalctl SYSLOG_IDENTIFIER=aesdsocket -p debug -f
 *
 * kill daemon:
 * ps aux | grep aesdsocket
 * pkill aesdsocket
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>

#include "queue.h"

// MARK: Defines
#define RECEIVE_BUFFER_SIZE 4096
#define SEND_BUFFER_SIZE 4096

// MARK: Enums
typedef enum result_s {
    SUCCESS = 0,
    FAILURE = 1
} result_t;

// MARK: Structs
typedef struct addrinfo addrinfo_t;
typedef struct sockaddr_in sockaddr_in_t;

typedef struct connection_entry_s {
    int value;
    uint32_t id;
    int peer_fd;
    pthread_t thread_id;
    bool done;
    SLIST_ENTRY(connection_entry_s) entries;
} connection_entry_t;

typedef struct {
    uint32_t total_connections;
} aesdsocket_metrics_t;

typedef struct {
    int server_fd;
    struct addrinfo* address;
    pthread_mutex_t file_mutex;
    pthread_mutex_t connections_mutex;
    pthread_t timestamp_thread;
    aesdsocket_metrics_t metrics;
    int connections_count;
    SLIST_HEAD(slisthead, connection_entry_s) connections;
} aesdsocket_t;

typedef struct {
    uint32_t id;
    int peer_fd;
} connection_thread_args_t;



// globals - Only accessed in the main entry point and cleanup/shutdown code.
// could do something cleaner without globals if using an event loop/state machine,
// that feels beyond the scope of this class though. Limiting their access to just
// startup/shutdown blocks feels clean enough.
static aesdsocket_t g_aesdsocket;

// forward declarations
static void cleanup_and_exit(aesdsocket_t* aesdsocket);

// MARK: signal handling
#define MAX_SIGNAL_NAME_LENGTH 32
typedef struct signal_handler_s {
    int signal;
    char name[MAX_SIGNAL_NAME_LENGTH];
    int flags;
    void (* callback)(int);
} signal_handler_t;

void sigint_handler(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    syslog(LOG_DEBUG, "SIGINT (2)");
    cleanup_and_exit(&g_aesdsocket);
}

void sigterm_handler(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    syslog(LOG_DEBUG, "SIGTERM (15)");
    cleanup_and_exit(&g_aesdsocket);
}

static const int signal_handler_table_size = 2;
static const signal_handler_t signal_handler_table[] = {
    {SIGINT, "SIGINT", 0, sigint_handler},
    {SIGTERM, "SIGTERM", 0, sigterm_handler},
};

void register_signal_handlers() {
    for (int i = 0; i < signal_handler_table_size; i++) {
        struct sigaction old_sigint_action;
        struct sigaction new_sigint_action;

        sigemptyset(&new_sigint_action.sa_mask);
        new_sigint_action.sa_flags = signal_handler_table[i].flags;
        new_sigint_action.sa_handler = signal_handler_table[i].callback;

        syslog(LOG_DEBUG, "Registering custom signal handler for: %s (%d)", signal_handler_table[i].name, signal_handler_table[i].signal);
        sigaction(signal_handler_table[i].signal, &new_sigint_action, &old_sigint_action);
    }
}

// MARK: Business Logic Start

result_t start_listen_server(int* server_fd, struct addrinfo** address, socklen_t* address_length) {
    addrinfo_t address_hints;

    memset(&address_hints, 0, sizeof(address_hints));
    address_hints.ai_family = AF_UNSPEC;
    address_hints.ai_socktype = SOCK_STREAM;
    address_hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, "9000", &address_hints, address) != 0) {
        perror("getaddrinfo");
        return(FAILURE);
    }

    *server_fd = socket((*address)->ai_family, (*address)->ai_socktype, (*address)->ai_protocol);

    // Allow re-using the port, if you restart this program in quick succession without this
    // you'll hint errors around the port still being use. It takes linux a minute or so to
    // free up the port by default, this bypasses the waiting.
    int enabled = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == -1) {
        perror("setsockopt");
        return(FAILURE);
    }

    if (bind(*server_fd, (*address)->ai_addr, (*address)->ai_addrlen) < 0) {
        perror("bind failed");
        return(FAILURE);
    }

    if (listen(*server_fd, 32) < 0) {
        perror("listen failed");
        return(FAILURE);
    }

    return SUCCESS;
}

void init_aesdsocket(aesdsocket_t* aesdsocket) {
    aesdsocket->connections_count = 0;
    aesdsocket->metrics.total_connections = 0;
    SLIST_INIT(&aesdsocket->connections);
    pthread_mutex_init(&aesdsocket->connections_mutex, NULL);
    pthread_mutex_init(&aesdsocket->file_mutex, NULL);
}

void deinit_aesdsocket(aesdsocket_t* aesdsocket) {
    pthread_mutex_destroy(&aesdsocket->connections_mutex);
    pthread_mutex_destroy(&aesdsocket->file_mutex);
}


// MARK: Cleanup

static void cleanup_and_exit(aesdsocket_t* aesdsocket) {
    syslog(LOG_DEBUG, "cleanup_and_exit()");

    // Join up the timestamp thread
    pthread_cancel(aesdsocket->timestamp_thread);
    int join_result = pthread_join(aesdsocket->timestamp_thread, NULL);
    if (join_result != 0) {
        perror("pthread_join");
    }

    // Remove all connections
    pthread_mutex_lock(&aesdsocket->connections_mutex);
    struct connection_entry_s *entry = NULL;
    struct connection_entry_s *temp = NULL;
    SLIST_FOREACH_SAFE(entry, &aesdsocket->connections, entries, temp) {
        if (entry->done) {
            if (pthread_cancel(entry->thread_id) != 0) {
                perror("pthread_cancel");
            }

            int join_result = pthread_join(entry->thread_id, NULL);
            if (join_result != 0) {
                perror("pthread_join");
            }
            SLIST_REMOVE(&aesdsocket->connections, entry, connection_entry_s, entries);
            aesdsocket->connections_count -= 1;
            syslog(LOG_DEBUG, "removed connection. connections_count: %d", aesdsocket->connections_count);
        }
    }
    syslog(LOG_INFO, "post cleanup connections: %d", aesdsocket->connections_count);
    pthread_mutex_unlock(&aesdsocket->connections_mutex);

    // Clean up file data
    if (access("/var/tmp/aesdsocketdata", F_OK) == 0) {
        if (remove("/var/tmp/aesdsocketdata") != 0) {
            perror("remove failed");
            exit(-1);
        }
    }

    if (aesdsocket->server_fd != 0) {
        if (shutdown(aesdsocket->server_fd, SHUT_RDWR) != 0) {
            perror("shutdown server_fd failed");
        }
        if (close(aesdsocket->server_fd) != 0) {
            perror("close server_fd failed");
        }
    }

    if (aesdsocket->address != NULL) {
        syslog(LOG_DEBUG, "aesdsocket->address: %p\n", aesdsocket->address);
        //freeaddrinfo(aesdsocket->address);
        aesdsocket->address = NULL;
    }

    deinit_aesdsocket(&g_aesdsocket);
    closelog();
    exit(0);
}

void join_completed_threads(aesdsocket_t* aesdsocket) {
    syslog(LOG_DEBUG, "join_completed_threads()");
    pthread_mutex_lock(&aesdsocket->connections_mutex);
    struct connection_entry_s *entry = NULL;
    struct connection_entry_s *temp = NULL;
    SLIST_FOREACH_SAFE(entry, &aesdsocket->connections, entries, temp) {
        if (entry->done) {
            pthread_join(entry->thread_id, NULL);
            SLIST_REMOVE(&aesdsocket->connections, entry, connection_entry_s, entries);
            aesdsocket->connections_count -= 1;
            syslog(LOG_DEBUG, "removed connection. connections_count: %d", aesdsocket->connections_count);
        }
    }
    pthread_mutex_unlock(&aesdsocket->connections_mutex);
}

// MARK: Timestamp thread

static void timestamp_timer_handler(union sigval sv) {
    time_t now_raw;
    struct tm* now;
    char buffer[256];
    time(&now_raw);
    now = localtime(&now_raw);
    strftime(buffer, sizeof(buffer), "timestamp:%a, %d %b %Y %T %z\n", now);
    syslog(LOG_DEBUG, "%s", buffer);

    syslog(LOG_DEBUG, "connections: %d", g_aesdsocket.connections_count);
    FILE* fp = fopen("/var/tmp/aesdsocketdata", "a+");
    if (fp == NULL) {
        perror("fopen failed");
    }

    pthread_mutex_lock(&g_aesdsocket.file_mutex);
    int fputs_result = fputs(buffer, fp);
    pthread_mutex_unlock(&g_aesdsocket.file_mutex);

    if (fputs_result == EOF) {
        perror("fputs failed");
    }

    if (fclose(fp) != 0) {
        perror("fclose failed");
    }
}

static timer_t timerid;

void* manage_timestamp_thread(void* arg) {
    struct sigevent sev = { 0 };
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timestamp_timer_handler;
    sev.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
        perror("timer_create");
    }

    struct itimerspec its = { 0 };
    its.it_interval.tv_sec = 10;
    its.it_value.tv_sec = 10;

    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("timer_settime");
    }

    while(true) {
        sleep(1);
    }
}

// MARK: Connection threads

int handle_peer(uint32_t id, int peer_fd, pthread_mutex_t file_mutex) {
    sockaddr_in_t peer_address;
    socklen_t peer_address_length = 0;
    pthread_t thread_id = pthread_self();

    if (getpeername(peer_fd, (struct sockaddr *)&peer_address, &peer_address_length) != 0) {
        perror("getpeername failed");
        return(FAILURE);
    }

    char client_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &peer_address.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop failed");
        return(FAILURE);
    }

    syslog(LOG_INFO, "Accepted connection from %s, id: %d, peer_fd: %d, thread_id: %ld",
        client_ip, id, peer_fd, thread_id);
    FILE* fp = fopen("/var/tmp/aesdsocketdata", "a+");
    if (fp == NULL) {
        perror("fopen failed");
        return(FAILURE);
    }

    // Receive data until we get a newline, writing data to file in chunks.
    while (true) {
        char receive_buffer[RECEIVE_BUFFER_SIZE];
        // Leave room for a null byte so we can log/debug the results
        int bytes_received = recv(peer_fd, receive_buffer, sizeof(receive_buffer) - 1, 0);
        receive_buffer[bytes_received] = '\0';
        if (bytes_received < 0) {
            perror("recv failed");
            return(FAILURE);
        } else if (bytes_received == 0) {
            syslog(LOG_DEBUG, "end of receive data");
            return(SUCCESS);
        }
        syslog(LOG_INFO, "   (%d) (%d) (%ld): recv: (%d): '%s'",
            id, peer_fd, thread_id, bytes_received, receive_buffer);

        pthread_mutex_lock(&file_mutex);
        int fputs_result = fputs(receive_buffer, fp);
        fflush(fp);
        pthread_mutex_unlock(&file_mutex);

        if (fputs_result == EOF) {
            perror("fputs failed");
            return(FAILURE);
        }

        if (receive_buffer[bytes_received - 1] == '\n') {
            syslog(LOG_DEBUG, "got newline");
            break;
        }
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror("fseek failed");
        return(FAILURE);
    }

    // Read the file we stored received data in and send it back to the peer.
    while (true) {
        char read_buffer[SEND_BUFFER_SIZE];
        int buffer_size = SEND_BUFFER_SIZE;

        pthread_mutex_lock(&file_mutex);
        int read_amount = fread(read_buffer, 1, buffer_size, fp);
        if (read_amount == 0 && ferror(fp) != 0) {
            perror("fread failed");
            pthread_mutex_unlock(&file_mutex);
            return(FAILURE);
        }
        pthread_mutex_unlock(&file_mutex);
        syslog(LOG_DEBUG, "fread: %d", read_amount);

        int sent_amount = send(peer_fd, read_buffer, read_amount, 0);
        syslog(LOG_DEBUG, "send: %d", sent_amount);

        if (sent_amount == -1) {
            perror("send failed");
            return(FAILURE);
        }
        if (sent_amount != SEND_BUFFER_SIZE) {
            break;
        }
    }

    if (fclose(fp) != 0) {
        perror("fclose failed");
        return(FAILURE);
    }

    return(SUCCESS);
}

void* manage_connection_thread(void* arg) {
    syslog(LOG_DEBUG, "manage_connection_thread()");
    pthread_t thread_id = pthread_self();
    connection_thread_args_t* thread_args = (connection_thread_args_t*) arg;
    uint32_t id = thread_args->id;
    int peer_fd = thread_args->peer_fd;

    free(thread_args);

    if (handle_peer(id, peer_fd, g_aesdsocket.file_mutex) == FAILURE) {
        perror("handle_peer failed");
        close(peer_fd);
        exit(-1);
    }
    close(peer_fd);

    struct connection_entry_s *entry = NULL;
    pthread_mutex_lock(&g_aesdsocket.connections_mutex);
    SLIST_FOREACH(entry, &g_aesdsocket.connections, entries) {
        if (entry->id == id) {
            syslog(LOG_DEBUG, "thread %d - %ld done", id, thread_id);
            entry->done = true;
        }
    }
    pthread_mutex_unlock(&g_aesdsocket.connections_mutex);
    return NULL;
}

// MARK: main

int main(int argc, char* argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);
    register_signal_handlers();
    init_aesdsocket(&g_aesdsocket);

    bool daemon_mode = false;
    if (argc >= 2) {
        if (strncmp(argv[1], "-d", sizeof("-d")) == 0) {
            daemon_mode = true;
        }
    }

    syslog(LOG_INFO, " "); // some empty space to make the syslog easier to scan
    syslog(LOG_INFO, " ");
    syslog(LOG_INFO, "Starting aesdsocket");
    socklen_t address_length = 0;
    if (start_listen_server(&g_aesdsocket.server_fd, &g_aesdsocket.address, &address_length) == FAILURE) {
        perror("start_listen_server failed");
        exit(-1);
    }

    if (daemon_mode) {
        syslog(LOG_DEBUG, "starting aesdsocket in daemon mode.");
        pid_t pid = fork();
        switch(pid) {
            case -1:
                perror("fork");
                exit(-1);
            case 0:
                // child
                break;
            default:
                // parent
                syslog(LOG_DEBUG, "daemon pid: %jd", (intmax_t) pid);
                exit(0);
        }
    }

    // Spawn the timestamp thread
    if (pthread_create(&g_aesdsocket.timestamp_thread, NULL, manage_timestamp_thread, NULL) != 0) {
        perror("pthread_create");
        exit(-1);
    }

    while(true) {
        int peer_fd = accept(g_aesdsocket.server_fd, (struct sockaddr*)&g_aesdsocket.address, &address_length);
        if (peer_fd == -1) {
            perror("accept failed");
            syslog(LOG_ERR, "accept failed");
            exit(-1);
        }

        // Spawn a new thread to handle the accepted connection.
        connection_thread_args_t* thread_args = malloc(sizeof(connection_thread_args_t));
        thread_args->id = g_aesdsocket.metrics.total_connections;
        thread_args->peer_fd = peer_fd;
        pthread_t new_thread_id;
        if (pthread_create(&new_thread_id, NULL, manage_connection_thread, thread_args) != 0) {
            perror("pthread_create");
            exit(-1);
        }

        // Add an entry for this new thread into the global aesdsocket structure.
        connection_entry_t *new_connection = malloc(sizeof(connection_entry_t));
        new_connection->id = g_aesdsocket.metrics.total_connections;
        new_connection->peer_fd = peer_fd;
        new_connection->thread_id = new_thread_id;
        new_connection->done = false;

        pthread_mutex_lock(&g_aesdsocket.connections_mutex);
        SLIST_INSERT_HEAD(&g_aesdsocket.connections, new_connection, entries);
        g_aesdsocket.connections_count += 1;
        g_aesdsocket.metrics.total_connections += 1;
        int connections_count = g_aesdsocket.connections_count;
        pthread_mutex_unlock(&g_aesdsocket.connections_mutex);

        syslog(LOG_DEBUG, "new connection. connections_count: %d (all time: %d)",
            connections_count, g_aesdsocket.metrics.total_connections);

        join_completed_threads(&g_aesdsocket);
    }
}
