#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200112L
#include "sys/socket.h"
#include "sys/types.h"
#include "netdb.h"
#include "syslog.h"
#include "signal.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "arpa/inet.h"
#include "errno.h"

#define BACKLOG 10
#define PORT "9000"
#define BUFFER_SIZE 1024

volatile sig_atomic_t exit_flag = 0;

void signal_handler(int signum) {
    exit_flag = 1;
}

int main(int argc, char *argv[]){

    int daemon_mode = 0;

    // Argument parsing for -d flag
    if(argc == 2 && strcmp(argv[1], "-d") == 0){
        daemon_mode = 1;
    }


    struct addrinfo *servinfo;
    struct addrinfo hints;
    int sockfd;
    int status;
    int yes = 1;
    struct sigaction sa;

    //Initialize syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Setup signal handling
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1){
        syslog(LOG_ERR, "Failed to set up SIGINT handler");
        closelog();
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGTERM, &sa, NULL) == -1){
        syslog(LOG_ERR, "Failed to set up SIGTERM handler");
        closelog();
        exit(EXIT_FAILURE);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Use IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream socket
    hints.ai_flags = AI_PASSIVE; //Fill in my IP for me, for bind()

    if((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) {
        fprintf(stderr, "get addrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    // Socket Creation
    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd == -1) {
        syslog(LOG_ERR, "socket creation failed");
        freeaddrinfo(servinfo);
        closelog();
        exit(EXIT_FAILURE);
    }

    // Set SO_REUSEADDR option to avoid "Address already in use" errors
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        syslog(LOG_ERR, "setsockopt failed");
        close(sockfd);
        freeaddrinfo(servinfo);
        closelog();
        exit(EXIT_FAILURE);
    }

    // Binding
    if(bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        syslog(LOG_ERR, "bind failed");
        close(sockfd);
        freeaddrinfo(servinfo);
        closelog();
        exit(EXIT_FAILURE);
    }

    // Free up memory after successful bind
    freeaddrinfo(servinfo);

    // Daemonize if -d flag is provided
    if(daemon_mode){
        syslog(LOG_INFO, "Starting in daemon mode");
        if(daemon(0,0) == -1){
            syslog(LOG_ERR, "daemon creation failed");
            exit(EXIT_FAILURE);
        }
    }

    // Listening
    if(listen(sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen failed");
        close(sockfd);
        closelog();
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Socket successfully created, bound to port %s, and listening", PORT);

    // Main server loop code
    while(!exit_flag) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        char client_ip[INET6_ADDRSTRLEN];
        int client_fd;
        char buffer[BUFFER_SIZE];
        ssize_t bytes_received;
        int data_fd;

        // Accept connection
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            // ADD DEBUG LOGGING:
            syslog(LOG_DEBUG, "accept failed. errno: %d, exit_flag: %d", errno, exit_flag);

            if (exit_flag) {
                syslog(LOG_DEBUG, "Breaking loop due to exit_flag"); // ADD THIS
                break;
            }
            if (errno == EINTR) {
                syslog(LOG_DEBUG, "accept was interrupted by a signal"); // ADD THIS
                if (exit_flag) {
                    break;
                }
                continue;
            }
            syslog(LOG_ERR, "accept failed: %m");
            continue;
        }

        // Get client IP address
        if(client_addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &s->sin_addr, client_ip, sizeof(client_ip));
        } else{
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, client_ip, sizeof(client_ip));
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Open data file (create if it doesn't exist)
        data_fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_APPEND | O_RDWR, 0644);
        if(data_fd == -1){
            syslog(LOG_ERR, "Failed to open data file");
            close(client_fd);
            continue;
        }

        // Receive data from client
        while((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0){
            buffer[bytes_received] = '\0'; // Null terminate

            // Write received data to file
            if(write(data_fd, buffer, bytes_received) == -1){
                syslog(LOG_ERR, "Failed to write to data file");
                break;
            }

            //Check if packet is complete (ends with newline)
            if(memchr(buffer, '\n', bytes_received) != NULL){
                // Send entire file content back to client
                lseek(data_fd, 0, SEEK_SET);

                char file_buffer[BUFFER_SIZE];
                ssize_t bytes_read;

                while((bytes_read = read(data_fd, file_buffer, BUFFER_SIZE)) > 0){
                    if(send(client_fd, file_buffer, bytes_read, 0) == -1){
                        syslog(LOG_ERR, "Failed to send data to client");
                        break;
                    }
                }
                break; // Packet complete, break loop
            }
        }


        if(bytes_received == -1){
            syslog(LOG_ERR, "recv failed");
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        close(data_fd);

    }
    // End Main server loop
    
    // Cleanup
    syslog(LOG_INFO, "Caught signal, exiting");


    // Delete the data file
    if(unlink("/var/tmp/aesdsocketdata") == -1){
        syslog(LOG_ERR, "Failed to delete data file");
    }


    close(sockfd);
    closelog();

    return 0;
};