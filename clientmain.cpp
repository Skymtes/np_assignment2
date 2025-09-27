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

// Enable if you want debugging to be printed, see examble below.
// Alternative, pass CFLAGS=-DDEBUG to make, make CFLAGS=-DDEBUG
// #define DEBUG

// Included to get the support library
#include <calcLib.h>
#include "protocol.h"

#define MAXBUF 1500   // fits UDP packet
#define TIMEOUT 2     // seconds
#define RETRIES 3

// simple wrapper for sending with retransmissions
ssize_t send_with_retry(int sock, void *msg, size_t msglen,
                        struct sockaddr *server, socklen_t slen,
                        void *reply, size_t replylen) {
    for (int attempt = 0; attempt < RETRIES; attempt++) {
        sendto(sock, msg, msglen, 0, server, slen);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        struct timeval tv = {TIMEOUT, 0};
        int rv = select(sock+1, &fds, NULL, NULL, &tv);

        if (rv > 0) {
            ssize_t n = recvfrom(sock, reply, replylen, 0, NULL, NULL);
            return n;
        }
        #ifdef DEBUG
        fprintf(stderr, "Timeout, retransmitting (%d)\n", attempt+1);
        #endif
    }
    return -1; // fail
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s host:port\n", argv[0]);
        return 1;
    }

    char delim_address[] = ":";
    char *Desthost = strtok(argv[1], delim_address);
    char *Destport = strtok(NULL, delim_address);
    
    if (!Desthost || !Destport) {
        fprintf(stderr, "ERROR: bad address format, expected host:port\n");
        return 1;
    }

    printf("Host %s, and port %s.\n", Desthost, Destport);

    struct addrinfo hints;
    struct addrinfo *server_addr;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    int rv = getaddrinfo(Desthost, Destport, &hints, &server_addr);
    if (rv != 0)
    {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      printf("ERROR: RESOLVE ISSUE\n");
      return 1;
    }

    int internal_socket = socket(server_addr->ai_family, server_addr->ai_socktype, server_addr->ai_protocol);
    if (internal_socket < 0)
    {
      perror("socket");
      freeaddrinfo(server_addr);
      return 2;
    }

    // build initial calcMessage
    struct calcMessage first_message;
    memset(&first_message, 0, sizeof(first_message));
    first_message.type = htons(22);
    first_message.message = htonl(0);
    first_message.protocol = htons(17);
    first_message.major_version = htons(1);
    first_message.minor_version = htons(0);

    char buffer[MAXBUF];
    ssize_t n = send_with_retry(internal_socket, &first_message, sizeof(first_message),
                                server_addr->ai_addr, server_addr->ai_addrlen,
                                buffer, sizeof(buffer));
    if (n < 0) {
        printf("No response from server.\n");
        return 1;
    }

    // check reply type
    if (n == sizeof(struct calcMessage)) {
        struct calcMessage *r = (struct calcMessage*)buffer;
        if (ntohs(r->type) == 2 && ntohl(r->message) == 2) {
            printf("Server says NOT OK, aborting.\n");
            return 1;
        } else {
            printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
            return 1;
        }
    }
    if (n != sizeof(struct calcProtocol)) {
        printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
        return 1;
    }

    struct calcProtocol *received_task = (struct calcProtocol*)buffer;
    uint32_t arith = ntohl(received_task->arith);
    int32_t i1 = ntohl(received_task->inValue1);
    int32_t i2 = ntohl(received_task->inValue2);
    double f1 = received_task->flValue1;
    double f2 = received_task->flValue2;

    char opname[8];
    char result_str[64];
    
    if (arith >=1 && arith <=4) { // integer
        int resval=0;
        if (arith==1) { resval=i1+i2; strcpy(opname,"add"); }
        else if (arith==2){ resval=i1-i2; strcpy(opname,"sub"); }
        else if (arith==3){ resval=i1*i2; strcpy(opname,"mul"); }
        else if (arith==4){ resval=i1/i2; strcpy(opname,"div"); }
        printf("ASSIGNMENT: %s %d %d\n", opname, i1, i2);
        snprintf(result_str,sizeof(result_str),"%d",resval);
        received_task->inResult = htonl(resval);
    } else { // float
        double fres=0;
        if (arith==5){ fres=f1+f2; strcpy(opname,"fadd"); }
        else if (arith==6){ fres=f1-f2; strcpy(opname,"fsub"); }
        else if (arith==7){ fres=f1*f2; strcpy(opname,"fmul"); }
        else if (arith==8){ fres=f1/f2; strcpy(opname,"fdiv"); }
        printf("ASSIGNMENT: %s %8.8g %8.8g\n", opname, f1, f2);
        snprintf(result_str,sizeof(result_str),"%g",fres);
        received_task->flResult = fres;
    }

    // send solution back
    n = send_with_retry(internal_socket, received_task, sizeof(*received_task),
                        server_addr->ai_addr, server_addr->ai_addrlen,
                        buffer, sizeof(buffer));
    if (n < 0) {
        printf("No response from server after sending result.\n");
        return 1;
    }

    if (n == sizeof(struct calcMessage)) {
        struct calcMessage *r = (struct calcMessage*)buffer;
        if (ntohl(r->message) == 1) {
            printf("OK (myresult=%s)\n", result_str);
        } else {
            printf("NOT OK (myresult=%s)\n", result_str);
        }
    } else {
        printf("ERROR WRONG SIZE OR INCORRECT PROTOCOL\n");
    }

    close(internal_socket);
    freeaddrinfo(server_addr);
    return 0;
}
