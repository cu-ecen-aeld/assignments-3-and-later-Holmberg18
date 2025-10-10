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
#include "stdbool.h"
#include "pthread.h"
#include "queue.h"
#include "time.h"
#include "sys/ioctl.h"
#include "aesd_ioctl.h"

#define USE_AESD_CHAR_DEVICE 1
#define BACKLOG 10
#define PORT "9000"
#define BUFFER_SIZE 1024
#define TIMESTAMP_INTERVAL 10

// Add the build switch for char device
#ifdef USE_AESD_CHAR_DEVICE
#define OUTPUT_FILE "/dev/aesdchar"
#else
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
#endif

// Global vars for thread managment
volatile sig_atomic_t exit_flag = 0;
#ifdef USE_AESD_CHAR_DEVICE
// No file mutex needed for char device - driver handles locking
#else
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

//Thread data structure for linked list
struct thread_data {
    int socket_fd;
    pthread_t thread_id;
    bool thread_complete;
    SLIST_ENTRY(thread_data) entries;
};

// Linked list head
SLIST_HEAD(thread_list, thread_data);
struct thread_list head = SLIST_HEAD_INITIALIZER(head);

// Func prototypes
void *client_thread_func(void *arg);
void *timestamp_thread_func(void *arg);

void signal_handler(int signum) {
    exit_flag = 1;
}

// Client thread function
void *client_thread_func(void *arg){
    struct thread_data *tdata = (struct thread_data *)arg;
    int client_fd = tdata->socket_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int data_fd = -1;
    bool use_seek_command = false;
    unsigned int write_cmd = 0, write_cmd_offset = 0;

#ifdef USE_AESD_CHAR_DEVICE
    // Open char device once per connection and keep it open
    data_fd = open(OUTPUT_FILE, O_RDWR);
    if(data_fd == -1){
        syslog(LOG_ERR, "Failed to open char device");
        tdata->thread_complete = true;
        close(client_fd);
        pthread_exit(NULL);
    }
    syslog(LOG_DEBUG, "Opened char device fd: %d", data_fd);
#else
    // Open file to enter data (original implementation)
    data_fd = open(OUTPUT_FILE, O_CREAT | O_APPEND | O_RDWR, 0644);
    if(data_fd == -1){
        syslog(LOG_ERR, "Failed to open data file");
        tdata->thread_complete = true;
        close(client_fd);
        pthread_exit(NULL);
    }
#endif

    // Receive data from client
    while((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0){
        buffer[bytes_received] = '\0';
        syslog(LOG_DEBUG, "Received %zd bytes: %s", bytes_received, buffer);

        // Check if this is a seek command
        use_seek_command = false;
        if(bytes_received >= 18 && strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 18) == 0){
            // Parse this seek command
            if(sscanf(buffer + 18, "%u,%u", &write_cmd, &write_cmd_offset) == 2){
                syslog(LOG_DEBUG, "Processing seek command: cmd=%u, offset=%u",
                    write_cmd, write_cmd_offset);
                use_seek_command = true;
            } else {
                syslog(LOG_ERR, "Failed to parse seek command: %s", buffer);
            }
        }

#ifdef USE_AESD_CHAR_DEVICE
        // CHAR DEVICE IMPLEMENTATION

        if(use_seek_command){
            syslog(LOG_DEBUG, "=== SEEK COMMAND DETECTED ===");
            
            // Prepare and send ioctl command
            struct aesd_seekto seekto;
            seekto.write_cmd = write_cmd;
            seekto.write_cmd_offset = write_cmd_offset;

            syslog(LOG_DEBUG, "Sending ioctl: write_cmd=%u, write_cmd_offset=%u", 
                   write_cmd, write_cmd_offset);
            
            if(ioctl(data_fd, AESDCHAR_IOCSEEKTO, &seekto) == -1){
                syslog(LOG_ERR, "ioctl seek failed: %m");
                // Don't break - continue processing
                continue;
            }

            syslog(LOG_DEBUG, "ioctl seek successful, reading from current position");

            // Read content back from char device and send to client
            // Use the same fd to ensure f_pos is maintained
            char file_buffer[BUFFER_SIZE];
            ssize_t bytes_read;
            ssize_t total_sent = 0;

            // Read from current position (set by ioctl)
            while((bytes_read = read(data_fd, file_buffer, BUFFER_SIZE)) > 0){
                ssize_t sent = send(client_fd, file_buffer, bytes_read, 0);
                if(sent == -1){
                    syslog(LOG_ERR, "Failed to send data to client");
                    break;
                }
                total_sent += sent;
                syslog(LOG_DEBUG, "Sent %zd bytes to client", sent);
            }
            syslog(LOG_DEBUG, "Finished sending seek response, total %zd bytes", total_sent);

        } else {
            // NORMAL CHAR DEVICE WRITE IMPLEMENTATION
            
            syslog(LOG_DEBUG, "Normal write operation");

            // Write data to char device
            if(write(data_fd, buffer, bytes_received) == -1){
                syslog(LOG_ERR, "Failed to write to char device");
                break;
            }
            syslog(LOG_DEBUG, "Wrote %zd bytes to char device", bytes_received);

            // Check if packet is complete and ends with newline
            if(memchr(buffer, '\n', bytes_received) != NULL){
                syslog(LOG_DEBUG, "Packet complete, reading back all content");
                
                // Save current position
                off_t current_pos = lseek(data_fd, 0, SEEK_CUR);
                
                // Read back ALL content from the beginning for normal writes
                lseek(data_fd, 0, SEEK_SET);
                
                char file_buffer[BUFFER_SIZE];
                ssize_t bytes_read;
                ssize_t total_sent = 0;

                while((bytes_read = read(data_fd, file_buffer, BUFFER_SIZE)) > 0){
                    ssize_t sent = send(client_fd, file_buffer, bytes_read, 0);
                    if(sent == -1){
                        syslog(LOG_ERR, "Failed to send data to client");
                        break;
                    }
                    total_sent += sent;
                }
                
                // Restore position
                lseek(data_fd, current_pos, SEEK_SET);
                syslog(LOG_DEBUG, "Sent %zd bytes back to client", total_sent);
            }
        }

#else
        // FILE-BASED IMPLEMENTATION

        if(use_seek_command){
            syslog(LOG_WARNING, "Seek command received but not supported in file mode");
        }
        
        // Lock mutex for file writing
        pthread_mutex_lock(&file_mutex);

        //Write data that's receive to the file
        if(write(data_fd, buffer, bytes_received) == -1){
            syslog(LOG_ERR, "Failed to write to data file");
            pthread_mutex_unlock(&file_mutex);
            break;
        }

        // Check if packet is complete (ends with newline)
        if(memchr(buffer, '\n', bytes_received) != NULL){
            // Send entire file content back to client
            lseek(data_fd, 0, SEEK_SET);

            char file_buffer[BUFFER_SIZE];
            ssize_t bytes_read;
            ssize_t total_sent = 0;

            while((bytes_read = read(data_fd, file_buffer, BUFFER_SIZE)) > 0){
                ssize_t sent = send(client_fd, file_buffer, bytes_read, 0);
                if(sent == -1){
                    syslog(LOG_ERR, "Failed to send data to client");
                    break;
                }
                total_sent += sent;
            }
        }

        pthread_mutex_unlock(&file_mutex);
#endif

        // If newline was found in a normal write, this packet is complete
        // Don't break for seek commands or multiple packets
        if(!use_seek_command && memchr(buffer, '\n', bytes_received) != NULL){
            // For normal writes ending with newline, we've already sent response
            // Continue to receive more commands on same connection
            syslog(LOG_DEBUG, "Normal write complete, waiting for next command");
        }
    }

    if(bytes_received == -1){
        syslog(LOG_ERR, "recv failed: %m");
    } else if(bytes_received == 0){
        syslog(LOG_DEBUG, "Client disconnected");
    }

    // Cleanup
    close(client_fd);
    if(data_fd != -1) {
        close(data_fd);
        syslog(LOG_DEBUG, "Closed char device fd: %d", data_fd);
    }
    
    tdata->thread_complete = true;
    syslog(LOG_DEBUG, "Client thread completed");

    pthread_exit(NULL);
}

// Timestamp thread function - Not needed for CHAR device
void *timestamp_thread_func(void *arg) {
#ifdef USE_AESD_CHAR_DEVICE
    // No timestamp functionality for char device implementation
    while(!exit_flag){
        sleep(1);
    }
#else
    int data_fd;

    while(!exit_flag){
        sleep(1);

        if(exit_flag) break;

        //Only do timestamp every 10 seconds
        static int counter = 0;
        if(++counter >= 10){
            counter = 0;
            //Generate timestamp
            time_t now = time(NULL);
            struct tm *timeinfo = localtime(&now);
            char timestamp[100];
            strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z\n", timeinfo);

            // Open data file
            data_fd = open(OUTPUT_FILE, O_CREAT | O_APPEND | O_RDWR, 0644);

            if(data_fd == -1){
                syslog(LOG_ERR, "Failed to open data file for timestamp");
                continue;
            }

            // Lock mutex and write timestamp
            pthread_mutex_lock(&file_mutex);
            write(data_fd, timestamp, strlen(timestamp));
            pthread_mutex_unlock(&file_mutex);

            close(data_fd);
        }
    }
#endif

    pthread_exit(NULL);
}

int main(int argc, char *argv[]){

    int daemon_mode = 0;
    pthread_t timestamp_tid;

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

    // Initialize linked list
    SLIST_INIT(&head);

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

    // Signal that server is ready for test scripts
    printf("SERVER_READY\n");
    fflush(stdout); // Flush the output buffer

    // Start timestamp thread (even for char device to maintain thread structure)
    if(pthread_create(&timestamp_tid, NULL, timestamp_thread_func, NULL) != 0){
        syslog(LOG_ERR, "Failed to create timestamp thread");
        close(sockfd);
        closelog();
        exit(EXIT_FAILURE);
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Main server loop code
    while(!exit_flag) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        char client_ip[INET6_ADDRSTRLEN];
        int client_fd;

        // Accept connection
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            // ADD DEBUG LOGGING:
            syslog(LOG_DEBUG, "accept failed. errno: %d, exit_flag: %d", errno, exit_flag);
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // Timeout, check exit_flag, and continue
                continue;
            }
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

        // Create new thread data structure
        struct thread_data *tdata = malloc(sizeof(struct thread_data));
        if(!tdata){
            syslog(LOG_ERR, "Failed to allocate thread data");
            close(client_fd);
            continue;
        }

        tdata->socket_fd = client_fd;
        tdata->thread_complete = false;

        // Insert into linked list
        SLIST_INSERT_HEAD(&head, tdata, entries);

        // Create client thread;
        if(pthread_create(&tdata->thread_id, NULL, client_thread_func, tdata) != 0){
            syslog(LOG_ERR, "Failed to create client thread");
            SLIST_REMOVE(&head, tdata, thread_data, entries);
            free(tdata);
            close(client_fd);
            continue;
        }


        // Clean up completed threads
        struct thread_data *tdata_temp, *tdata_iter;
        SLIST_FOREACH_SAFE(tdata_iter, &head, entries, tdata_temp) {
            if(tdata_iter->thread_complete){
                pthread_join(tdata_iter->thread_id, NULL);
                SLIST_REMOVE(&head, tdata_iter, thread_data, entries);
                free(tdata_iter);
            }
        }
    }
    // End Main server loop
    
    // Cleanup
    syslog(LOG_INFO, "Caught signal, exiting");

    // Join timestamp thread
    pthread_join(timestamp_tid, NULL);

    // Join all remaning client threads
    struct thread_data *tdata_temp, *tdata_iter;
    SLIST_FOREACH_SAFE(tdata_iter, &head, entries, tdata_temp){
        pthread_join(tdata_iter->thread_id, NULL);
        SLIST_REMOVE(&head, tdata_iter, thread_data, entries);
        free(tdata_iter);
    }

#ifndef USE_AESD_CHAR_DEVICE
    // Delete the data file only for file-based implementation
    if(unlink(OUTPUT_FILE) == -1){
        syslog(LOG_ERR, "Failed to delete data file");
    }
#endif

#ifndef USE_AESD_CHAR_DEVICE
    pthread_mutex_destroy(&file_mutex);
#endif
    close(sockfd);
    closelog();

    return 0;
}