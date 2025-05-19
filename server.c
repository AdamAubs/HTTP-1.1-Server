#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define RMAX 4096
#define HMAX 1024
#define BMAX 1024
#define DMAX 1024
#define MAX_EVENTS 10


typedef struct {
    int filefd; // file descriptor for requested file
    char *fileContents; // file data
    size_t fileSize; 
    size_t bytesSent;
    int complete; // indicates if transfer is complete
} client_state_t;

// Array of client states indexed by file descriptor numbers 
// (0 to 1023). Each element tracks a clients file tranfer progress
// allowing the server to handle multiple concurrent transfers by maintaing 
// separate state for each connected client.
client_state_t client_states[__FD_SETSIZE];

static char request[RMAX+1];

static int HSIZE = 0;
static char header[HMAX];

static int BSIZE = 0;
static char body[BMAX];

static int DSIZE = 0;
static char data[DMAX];

static int FSIZE = 0;

static int 
open_listenfd(int port) 
{
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server;
    server.sin_family = AF_INET; // Protocol family
    server.sin_port = htons(port); // Port number in network byte order
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr); // IP address of the server in network byte order

    bind(listenfd, (struct sockaddr*)&server, sizeof(server)); // ask kernel to associate the socket file with socket address.
    listen(listenfd, 10); // Enable socket to accept connections

    return listenfd;
}

static int
accept_connection(int listenfd)
{
    static struct sockaddr_in client;
    static socklen_t csize;

    memset(&client, 0x00, sizeof(client));
    memset(&csize, 0x00, sizeof(csize));
    csize = sizeof(client);
    
    int clientfd = accept(listenfd, (struct sockaddr*)&client, &csize);

    return clientfd;
}


static int
handle_request(int clientfd) 
{
    // Read client socket request into buffer
    ssize_t request_size = recv(clientfd, request, RMAX, 0);
    request[request_size] = '\0';

    // Invalid client socket read
    if (request_size <= 0) {
        if (request_size < 0) {
            perror("reading client socket");
        }
        return -1;
    }

    char method[256];
    char path[256];
    char version[256];
    sscanf(request, "%s /%s %s", method, path, version);

    if (strncmp(path, "hello", 4) == 0) {
        strcpy(body, "greetings");
        BSIZE = strlen(body);

        sprintf(header, "HTTP/1.1 200 OK\r\n"
                       "Content-Length: %d\r\n"
                       "\r\n", BSIZE);
        HSIZE = strlen(header);

        return 1;
    }

    if (strncmp(path, "headers", 7) == 0) {

       int requestLen = strlen(request);
       if (request[requestLen - 1] != '\n' && request[requestLen - 2] != '\r') {
           strcpy(header, "HTTP/1.1 400 Bad Request\r\n"
                          "\r\n");
           HSIZE = strlen(header);
       } else {
           char* headerStart = strstr(request, "\r");
           headerStart += 2;

           char* headerEnd = strstr(headerStart, "\r\n\r\n");
           int headerLength = headerEnd - headerStart;

           memcpy(body, headerStart, headerLength);

           BSIZE = strlen(body);
   
           if (BSIZE > 1024) {
               body[0] = '\0';
               BSIZE = 0; 
               strcpy(header, "HTTP/1.1 413 Request Entity Too Large\r\n"
                              "\r\n");
               HSIZE = strlen(header);
           } else {
               sprintf(header, "HTTP/1.1 200 OK\r\n" 
                               "Content-Length: %d\r\n"
                               "\r\n", BSIZE);
               HSIZE = strlen(header);
           }
       }

       return 1;
    }

    if (strncmp(path, "data", 4) == 0) {
        // Start at the content after the request header
        char* dataStart = strstr(request, "\r\n\r\n");

        // Get the length of the content in the request
        char* contentLengthStart = strstr(request, "Content-Length: ");
        int contentLength = 0;
        if (contentLengthStart) {
            contentLengthStart += strlen("Content-Length: ");
            contentLength = atoi(contentLengthStart);
        }  

        // Check the request body size to
        // make sure its not greater than the max 1024
        if (contentLength > 1024) {
            // handle data limit exceeded error
            printf("data limit exceeded alloted amount!\n");

            // Set header to 413 error response
            strcpy(header, "HTTP/1.1 413 Request Entity Too Large\r\n"
                "\r\n");
            HSIZE = strlen(header);
            // Make sure the body is empty
            memset(body, 0, sizeof(body));
        } else {
            // Correct size so handle post request
            // by storing its body in the global
            // data array.

            // Clear the data buffer if its not empty
            if (strcmp(data, "\0") != 0) {
                memset(data, 0, sizeof(data));
                DSIZE = 0;
            }

            // Copy the content length amount of data from the body of the request 
            // into the global data array
            if (dataStart) {
                dataStart += 4;
                memcpy(data, dataStart, contentLength);
                DSIZE = contentLength;
            } 

            // Set up the response to send back to the client.
            // The responses body is the body of the request body (data) 
            // sent from the client.
            memcpy(body, data, contentLength);
            BSIZE = contentLength;
            sprintf(header, "HTTP/1.1 200 OK\r\n" 
                "Content-Length: %d\r\n"
                "\r\n", BSIZE);
            HSIZE = strlen(header);
        }

        return 1;
    }

    if (strncmp(path, "stored", 6) == 0) {
        // Nothing is stored in data buffer
        if (strcmp(data, "\0") == 0) {
            memcpy(body, "(NULL)", strlen("(NULL)") + 1);

            BSIZE = strlen(body);

        // Create response
        sprintf(header, "HTTP/1.1 200 OK\r\n" 
            "Content-Length: %d\r\n"
            "\r\n", BSIZE);
            HSIZE = strlen(header); 
        
        } else {
            memcpy(body, data, DSIZE);

            BSIZE = DSIZE;
            // Create response
            sprintf(header, "HTTP/1.1 200 OK\r\n" 
                "Content-Length: %d\r\n"
                "\r\n", BSIZE);
            HSIZE = strlen(header); 
        }

        return 1;
    } else {
        if (strlen(method) > 3) {
            strcpy(header, "HTTP/1.1 400 Bad Request\r\n\r\n");
            HSIZE = strlen(header);
            BSIZE = 0;
            return 1;
        }

        int fd = open(path, O_RDONLY);

        if (fd == -1) {
            printf("cannot open file\n");
        }

        struct stat buf;
        fstat(fd, &buf);

        if (S_ISREG(buf.st_mode)) {
            client_state_t *state = &client_states[clientfd];

            // Set up the clients states requested file data 
            // that will be used in the response
            FSIZE = buf.st_size; 
            state->fileContents = malloc(FSIZE + 1);
            state->fileSize = buf.st_size;
            state->bytesSent = 0;
            state->complete = 0;
            ssize_t file_contents_size = read(fd, state->fileContents, FSIZE);
            state->fileContents[file_contents_size] = '\0';
    
            close(fd);
    
            FSIZE = file_contents_size;
            sprintf(header, "HTTP/1.1 200 OK\r\n" 
                "Content-Length: %d\r\n"
                "\r\n", FSIZE);
            HSIZE = strlen(header);

            return 1;
        } else {
            strcpy(header, "HTTP/1.1 404 Not Found\r\n\r\n");
            HSIZE = strlen(header);
            BSIZE = 0;
        }

        return 1;
    }

    memset(method, 0, sizeof(method));
    memset(path, 0, sizeof(path));
    memset(version, 0, sizeof(version));
}

static void 
send_data(int clientfd, char buf[], int size)
{
    ssize_t amt, total = 0;
    do {
        amt = send(clientfd, buf + total, size - total, 0);
        total += amt;
    } while (total < size);
}

static void 
send_response(int clientfd)
{
    // First send the header
    send_data(clientfd, header, HSIZE);

    // Get the state using its index in the client_states array
    client_state_t *state = &client_states[clientfd];

    // If we have a file to send, do it in chuncks
    if (state->fileContents != NULL) {
        size_t remainingBytes = state->fileSize - state->bytesSent;
        size_t chunkSize = (remainingBytes > 1024) ? 1024 : remainingBytes;

        // Send a chunk
        send_data(clientfd, state->fileContents + state->bytesSent, chunkSize);
        state->bytesSent += chunkSize;

        // If there's more to send, keep the connection EPOLLOUT state
        if (state->bytesSent < state->fileSize) {
            return; // More to send, don't close yet
        }
    } else if (body[0] != '\0') {
        // send body if it exists and were not sending a file
        send_data(clientfd, body, BSIZE);
    }

    // Reset after response sent
    memset(header, 0, sizeof(header));
    HSIZE = 0;
    memset(body, 0, sizeof(body));
    BSIZE = 0;
    memset(request, 0, sizeof(request));

    // ensure client state is properly freed if not in use
    if (state->fileContents != NULL) {
        free(state->fileContents);
        state->fileContents = NULL;
        state->fileSize = 0;
        state->bytesSent = 0;
        state->complete = 1;
    }
}


int 
main(int argc, char * argv[]) {

    assert(argc == 2);
    int port = atoi(argv[1]);
    // create the servers lisitening socket
    int listenfd = open_listenfd(port);

    // Set up epoll instance that returns file descriptor 
    // which will be used to manage and monitor multiple file descriptors
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // epoll_events describes events to monitor and associated data
    struct epoll_event ev;
    struct epoll_event event_list[MAX_EVENTS];

    // monitor listenfd for readable events
    // like when there is data to read or a new 
    // connection to accept
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;

    // epoll_ctl is used to manage the file descriptors monitored by the epoll instance
    // This adds the listenfd to the epoll instance epfd
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
        perror("epoll_ctl: listenfd");
        exit(EXIT_FAILURE);
    }

    // Initialize client states for 1024 potential clients
    for (int i = 0; i < __FD_SETSIZE; i++) {
        client_states[i].fileContents = NULL;
        client_states[i].fileSize = 0;
        client_states[i].bytesSent = 0;
        client_states[i].complete = 0;
    }

    while (1) {
        // blocks unitill at least one file descriptor in the epfd instance is ready
        // for the events being monitored.
        // Returns the number of file descriptors with events that are ready
        int nfds = epoll_wait(epfd, event_list, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            int fd = event_list[i].data.fd;
            // Check the event type 
            if (event_list[i].events & EPOLLIN) {
                if (fd == listenfd) {
                    // listenfd ready for reading
                    int clientfd = accept_connection(listenfd);
                    if (clientfd == -1) {
                        perror("accept");
                        exit(EXIT_FAILURE);
                    }

                    // add the new clientfd to the epoll instance for monitoring
                    ev.events = EPOLLIN;
                    ev.data.fd = clientfd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev) == -1) {
                        perror("epoll_ctl: clientfd");
                        exit(EXIT_FAILURE);
                    }
                } else {
                    // Event is on a client socket
                    // meanining the client is ready for reading
                    //int status = handle_request(fd);
                    int status = handle_request(fd);
                    //printf("HANDLING RESPONSE!!!!!!!!\n");
                    
                    if (status < 0) {
                        // Invalid read so remove the descriptor from epoll
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                    } else {
                        //printf("SETTING EPOLLOUT!!!!!!!!\n");
                        // Valid request received so change the event to monitor for write readiness
                        ev.events = EPOLLOUT;
                        ev.data.fd = fd;
                        if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                            perror("epoll_ctl: mod to EPOLLOUT");
                            close(fd);
                        }
                    }
                } 
            } else if (event_list[i].events & EPOLLOUT) {
                // Event is not EPOLLIN, socket is ready for writing
                // EPOLLOUT -> client ready for writing
                //send_response(fd);
                //printf("SENDING RESPONSE!!!!!!!!\n");

                // Create a pointer to the clients state data in the client_states
                // array using the clients fd as an index. Using a pointer ensures 
                // any modifications persist throughout the client's entire connection.
                client_state_t *state = &client_states[fd];


                if (state->bytesSent == 0) {
                    // first chunk, send headers and start sending file
                    send_response(fd);
                } else if (state->bytesSent < state->fileSize) {
                    // continue sending next chunk
                    size_t remainingBytes = state->fileSize - state->bytesSent;
                    size_t chunkSize = (remainingBytes > 1024) ? 1024 : remainingBytes;
                    
                    // state->fileContents starts where state->bytesSent left off
                    send_data(fd, state->fileContents + state->bytesSent, chunkSize);
                    state->bytesSent += chunkSize;
                }

                // Check if we've sent everything
                if (state->fileContents == NULL || state->bytesSent >= state->fileSize) {
                    // All data sent so remove the descriptor from epoll
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);

                    // Cleanup any remaining resources
                    if (state->fileContents != NULL) {
                        free(state->fileContents);
                        state->fileContents = NULL;
                    }
                    
                    state->fileSize = 0;
                    state->bytesSent = 0;
                    state->complete = 0;
                    
                    close(fd);
                }
            }
        }
    }

    close(epfd);
    close(listenfd);

    return EXIT_SUCCESS;
}
