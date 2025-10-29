#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
/* You will have to add includes here */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <map>

// Enable if you want debugging to be printed.
// Alternative, pass CFLAGS=-DDEBUG to make
// #define DEBUG

// Included to get the support library
#include <calcLib.h>
#include "protocol.h"

#define JOB_TIMEOUT 10 // seconds
#define MAX_BUFFER 1500

using namespace std;

/*
Holds the state of a pending job for a client.
*/
struct ClientState {
    uint32_t id;            // The unique ID for this job
    time_t assignmentTime;  // Time the job was sent
    bool is_float;          // True if a float operation
    double f_result;        // The correct float result
    int32_t i_result;       // The correct int result
    
    // Store the client's address for replies
    struct sockaddr_storage addr;
    socklen_t addrlen;
};

/*
  Creates a unique string key (IP:Port) from a sockaddr.
 */
std::string get_client_key(struct sockaddr *addr, socklen_t len) {
    char hostbuf[NI_MAXHOST];
    char portbuf[NI_MAXSERV];
    if (getnameinfo(addr, len, hostbuf, sizeof(hostbuf),
                    portbuf, sizeof(portbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        return std::string(hostbuf) + ":" + std::string(portbuf);
    }
    return "unknown";
}

/*
Sends a simple calcMessage (OK or NOT OK) to a client.
*/
void send_simple_message(int sock, uint32_t msg_code, struct sockaddr* addr, socklen_t len) {
    struct calcMessage reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = htons(2);
    reply.message = htonl(msg_code);  // 1=OK, 2=NOT OK
    reply.protocol = htons(17);
    reply.major_version = htons(1);
    reply.minor_version = htons(0);
    
    sendto(sock, &reply, sizeof(reply), 0, addr, len);
}

/*
  Iterates through the job map and removes any expired jobs.
*/
void check_timeouts(std::map<std::string, ClientState>& jobs) {
    time_t now = time(NULL);
    for (auto it = jobs.begin(); it != jobs.end(); /* no increment */) {
        if (now - it->second.assignmentTime >= JOB_TIMEOUT) {
            #ifdef DEBUG
            printf("Job for client %s timed out.\n", it->first.c_str());
            #endif
            it = jobs.erase(it);
        } else {
            ++it;
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s host:port\n", argv[0]);
        return 1;
    }

    initCalcLib();

    char delim_address[] = ":";
    char *Desthost = strtok(argv[1], delim_address);
    char *Destport = strtok(NULL, delim_address);
    if (!Desthost || !Destport) {
        fprintf(stderr, "ERROR: bad address format, expected host:port\n");
        return 1;
    }

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(Desthost, Destport, &hints, &res) != 0) {
        perror("getaddrinfo");
        return 1;
    }

    int listen_fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if ((listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0)
        {    
          perror("server: socket");
          continue;
        }
        int yes = 1;
        if ((setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) < 0)
        {
            perror("setsockopt");
            exit(1);
        }
        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) < 0) 
        {
            close(listen_fd);
            perror("server: bind");
            continue;
        }
    }

    freeaddrinfo(res);

    #ifdef DEBUG
    printf("Server listening on %s:%s\n", Desthost, Destport);
    #endif

    // Map to store active jobs, keyed by "IP:Port" string
    std::map<std::string, ClientState> client_jobs;
    char buffer[MAX_BUFFER];

    while (1) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(listen_fd, &rset);

        // Set timeout for select() to 1 second to check for job timeouts
        struct timeval tv = {1, 0}; 

        int rv = select(listen_fd + 1, &rset, NULL, NULL, &tv);

        if (rv < 0) {
            perror("select");
            break;
        }

        if (rv == 0) {
            check_timeouts(client_jobs);
            continue;
        }

        // --- 4. Data is ready: Read packet ---
        if (FD_ISSET(listen_fd, &rset)) {
            struct sockaddr_storage client_addr;
            socklen_t client_len = sizeof(client_addr);
            ssize_t n = recvfrom(listen_fd, buffer, MAX_BUFFER, 0,
                               (struct sockaddr*)&client_addr, &client_len);

            if (n < 0) {
                perror("recvfrom");
                continue;
            }

            std::string client_key = get_client_key((struct sockaddr*)&client_addr, client_len);

            #ifdef DEBUG
            printf("Received %zd bytes from %s\n", n, client_key.c_str());
            #endif

            if (n == sizeof(struct calcMessage)) {
                struct calcMessage* msg = (struct calcMessage*)buffer;

                // Check for valid request: type 22, proto 17, v1.0
                if (ntohs(msg->type) == 22 && 
                    ntohs(msg->message) == 0 &&
                    ntohs(msg->protocol) == 17 &&
                    ntohs(msg->major_version) == 1 &&
                    ntohs(msg->minor_version) == 0) 
                {

                    ClientState state;
                    state.assignmentTime = time(NULL);
                    state.id = rand();
                    memcpy(&state.addr, &client_addr, client_len);
                    state.addrlen = client_len;

                    struct calcProtocol sent_task;
                    memset(&sent_task, 0, sizeof(sent_task));
                    sent_task.type = htons(1);
                    sent_task.major_version = htons(1);
                    sent_task.minor_version = htons(0);
                    sent_task.id = htonl(state.id);

                    char *op = randomType();
                    int iv1 = 0, iv2 = 0;
                    double fv1 = 0.0, fv2 = 0.0;
                    uint32_t arith_code = 0;

                    if (op[0] == 'f') {
                        state.is_float = true;
                        fv1 = randomFloat();
                        fv2 = randomFloat();
                        if (strcmp(op, "fadd") == 0) {
                            arith_code = 5; state.f_result = fv1 + fv2;
                        } else if (strcmp(op, "fsub") == 0) {
                            arith_code = 6; state.f_result = fv1 - fv2;
                        } else if (strcmp(op, "fmul") == 0) {
                            arith_code = 7; state.f_result = fv1 * fv2;
                        } else if (strcmp(op, "fdiv") == 0) {
                            arith_code = 8;
                            if (fv2 == 0.0) fv2 = 1.0;
                            state.f_result = fv1 / fv2;
                        }
                        sent_task.flValue1 = fv1;
                        sent_task.flValue2 = fv2;
                    } else {
                        state.is_float = false;
                        iv1 = randomInt();
                        iv2 = randomInt();
                        if (strcmp(op, "add") == 0) {
                            arith_code = 1; state.i_result = iv1 + iv2;
                        } else if (strcmp(op, "sub") == 0) {
                            arith_code = 2; state.i_result = iv1 - iv2;
                        } else if (strcmp(op, "mul") == 0) {
                            arith_code = 3; state.i_result = iv1 * iv2;
                        } else if (strcmp(op, "div") == 0) {
                            arith_code = 4;
                            if (iv2 == 0) iv2 = 1;
                            state.i_result = iv1 / iv2;
                        }
                        sent_task.inValue1 = htonl(iv1);
                        sent_task.inValue2 = htonl(iv2);
                    }
                    
                    sent_task.arith = htonl(arith_code);

                    // Store the job state and send it
                    client_jobs[client_key] = state;
                    sendto(listen_fd, &sent_task, sizeof(sent_task), 0, (struct sockaddr*)&client_addr, client_len);
                    
                    #ifdef DEBUG
                    printf("Sent job (ID: %u) to %s\n", state.id, client_key.c_str());
                    #endif

                } else {
                    #ifdef DEBUG
                    printf("Invalid calcMessage from %s. Sending NOT OK.\n", client_key.c_str());
                    #endif
                    send_simple_message(listen_fd, 2, (struct sockaddr*)&client_addr, client_len);
                }
            }
            
            else if (n == sizeof(struct calcProtocol)) {
                struct calcProtocol* sol = (struct calcProtocol*)buffer;

                if (ntohs(sol->type) != 2) {
                    #ifdef DEBUG
                    printf("Received calcProtocol with wrong type from %s. Ignoring.\n", client_key.c_str());
                    #endif
                    continue; 
                }

                // 1. Check if client is known (not timed out)
                auto it = client_jobs.find(client_key);
                if (it == client_jobs.end()) {
                    #ifdef DEBUG
                    printf("Received solution from unknown/timed-out client %s. Sending NOT OK.\n", client_key.c_str());
                    #endif
                    send_simple_message(listen_fd, 2, (struct sockaddr*)&client_addr, client_len);
                    continue;
                }

                ClientState& state = it->second;

                // 2. Check if the ID matches
                if (ntohl(sol->id) != state.id) {
                    #ifdef DEBUG
                    printf("Client %s sent WRONG ID (got %u, expected %u). Sending NOT OK.\n", 
                           client_key.c_str(), ntohl(sol->id), state.id);
                    #endif
                    send_simple_message(listen_fd, 2, (struct sockaddr*)&client_addr, client_len);
                    continue;
                }

                bool correct = false;
                if (state.is_float) {
                    double client_res = sol->flResult;
                    double server_res = state.f_result;

                    float precision = 0.0001;
                    if (((server_res - precision) < client_res) && 
                        ((server_res + precision) > client_res))
                        correct = true;

                    #ifdef DEBUG
                    printf("Client %s: Float check (Ref: %g, Client: %g) -> %s\n",
                           client_key.c_str(), server_res, client_res, correct ? "OK" : "WRONG");
                    #endif

                } else {
                    int32_t client_res = ntohl(sol->inResult);
                    int32_t server_res = state.i_result;
                    if (client_res == server_res) {
                        correct = true;
                    }
                    #ifdef DEBUG
                    printf("Client %s: Int check (Ref: %d, Client: %d) -> %s\n",
                           client_key.c_str(), server_res, client_res, correct ? "OK" : "WRONG");
                    #endif
                }

                if (correct) {
                    send_simple_message(listen_fd, 1, (struct sockaddr*)&client_addr, client_len); // 1 = OK
                } else {
                    send_simple_message(listen_fd, 2, (struct sockaddr*)&client_addr, client_len); // 2 = NOT OK
                }

                client_jobs.erase(it);
            }
            
            else {
                #ifdef DEBUG
                printf("Received malformed packet (size %zd) from %s. Ignoring.\n", n, client_key.c_str());
                #endif
            }
        }
    }

    close(listen_fd);
    printf("Server shutting down.\n");
    return 0;
}