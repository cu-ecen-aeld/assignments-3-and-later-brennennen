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
#include <unistd.h>

// structs, defines, and enums

#define RECEIVE_BUFFER_SIZE 4096
#define SEND_BUFFER_SIZE 4096

typedef enum result_s {
    SUCCESS = 0,
    FAILURE = 1
} result_t;

typedef struct addrinfo addrinfo_t;
typedef struct sockaddr_in sockaddr_in_t;

// globals - Only accessed in the main entry point and cleanup/shutdown code.
// could do something cleaner without globals if using an event loop/state machine,
// that feels beyond the scope of this class though. Limiting their access to just
// startup/shutdown blocks feels clean enough.
static int g_server_fd = 0;
static addrinfo_t* g_address = NULL;
static int g_peer_fd = 0;

// forward declarations
static void cleanup_and_exit();

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
    cleanup_and_exit();
}

void sigterm_handler(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    syslog(LOG_DEBUG, "SIGTERM (15)");
    cleanup_and_exit();
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

// MARK: business logic

result_t start_listen_server(int* server_fd, addrinfo_t** address, socklen_t* address_length) {
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

    if (listen(*server_fd, 3) < 0) {
        perror("listen failed");
        return(FAILURE);
    }

    return SUCCESS;
}

int handle_peer(int peer_fd) {
    sockaddr_in_t peer_address;
    socklen_t peer_address_length = 0;

    if (getpeername(peer_fd, (struct sockaddr *)&peer_address, &peer_address_length) != 0) {
        perror("getpeername failed");
        return(FAILURE);
    }

    char client_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &peer_address.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop failed");
        return(FAILURE);
    }

    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
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
        }
        syslog(LOG_DEBUG, "recv: (%d): '%s'", bytes_received, receive_buffer);

        if (fputs(receive_buffer, fp) == EOF) {
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
        int read_amount = fread(read_buffer, 1, buffer_size, fp);
        syslog(LOG_DEBUG, "fread: %d", read_amount);
        if (read_amount == 0 && ferror(fp) != 0) {
            perror("fread failed");
            return(FAILURE);
        }

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

static void cleanup_and_exit() {
    if (access("/var/tmp/aesdsocketdata", F_OK) == 0) {
        if (remove("/var/tmp/aesdsocketdata") != 0) {
            perror("remove failed");
            exit(-1);
        }
    }

    if (g_peer_fd != 0) {
        if (shutdown(g_peer_fd, SHUT_RDWR) != 0) {
            perror("shutdown g_peer_fd failed");
        }
        if (close(g_peer_fd) != 0) {
            perror("close g_peer_fd failed");
        }
    }

    if (g_server_fd != 0) {
        if (shutdown(g_server_fd, SHUT_RDWR) != 0) {
            perror("shutdown g_server_fd failed");
        }
        if (close(g_server_fd) != 0) {
            perror("close g_peer_fd failed");
        }
    }

    if (g_address != NULL) {
        freeaddrinfo(g_address);
    }

    exit(0);
}

int main(int argc, char* argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);
    register_signal_handlers();

    bool daemon_mode = false;
    if (argc >= 2) {
        if (strncmp(argv[1], "-d", sizeof("-d")) == 0) {
            daemon_mode = true;
        }
    }

    syslog(LOG_DEBUG, "starting server...\n");
    socklen_t address_length = 0;
    if (start_listen_server(&g_server_fd, &g_address, &address_length) == FAILURE) {
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

    while(true) {
        g_peer_fd = 0;
        g_peer_fd = accept(g_server_fd, (struct sockaddr*)g_address, &address_length);
        if (g_peer_fd == -1) {
            perror("accept failed");
            exit(-1);
        }

        if (handle_peer(g_peer_fd) == FAILURE) {
            perror("handle_peer failed");
            exit(-1);
        }
    }
}
