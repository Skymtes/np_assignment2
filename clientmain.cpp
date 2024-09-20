#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
/* You will have to add includes here */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

// Enable if you want debugging to be printed, see examble below.
// Alternative, pass CFLAGS=-DDEBUG to make, make CFLAGS=-DDEBUG
#define DEBUG

// Included to get the support library
#include <calcLib.h>
#include "protocol.h"

#define BUFFER_SIZE 4096

int main(int argc, char *argv[])
{
  /*
    Read first input, assumes <ip>:<port> syntax, convert into one string (Desthost) and one integer (port).
     Atm, works only on dotted notation, i.e. IPv4 and DNS. IPv6 does not work if its using ':'.
  */
  char delim_address[] = ":";
  char *Desthost = strtok(argv[1], delim_address);
  char *Destport = strtok(NULL, delim_address);
  int destination_port = atoi(Destport);
  // *Desthost now points to a string holding whatever came before the delimiter, ':'.
  // *Dstport points to whatever string came after the delimiter.

  /* Do magic */
  // int port = atoi(Destport);

  printf("Host %s, and port %s.\n", Desthost, Destport);

  char buffer[BUFFER_SIZE];
  memset(buffer, 0, sizeof(buffer));

  struct sockaddr_in server_addr;

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(destination_port);

  int internal_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (internal_socket < 0)
  {
    printf("Socket could not be created.\n");
    return 1;
  }

  if (inet_pton(AF_INET, Desthost, &server_addr.sin_addr) < 0)
  {
    printf("Invalid address/ Address not supported \n");
    return 2;
  }

  int connection = connect(internal_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (connection < 0)
  {
    printf("Connection Failed \n");
    return 3;
  }

  struct calcMessage first_message;
  first_message.type = htons(22);
  first_message.message = htonl(0);
  first_message.protocol = htons(17);
  first_message.major_version = htons(1);
  first_message.minor_version = htons(0);

  for (int i = 0; i == 3; i++)
  {
  }

  int sending = send(internal_socket, &first_message, sizeof(first_message), 0);
  if (sending < 0)
  {
    printf("Could not send to server.\n");
    return 4;
  }

  while (1)
  {
    int bytes_received = recv(internal_socket, buffer, sizeof(buffer), 0);
    if (bytes_received < 0)
    {
      printf("No message received.\n");
      close(internal_socket);
      return 5;
    }

    if (bytes_received == 0)
    {
      printf("Server closed.\n");
      close(internal_socket);
      return 0;
    }

    if (bytes_received <= 10)
    {
      printf("NOT OK RECEIVED\n");
      close(internal_socket);
      return 0;
    }

    if (bytes_received > 10)
    {
      break;
    }
  }

  struct calcProtocol *received_task = (struct calcProtocol *)buffer;
  struct calcProtocol converted_task;
  converted_task.type = ntohs(received_task->type);
  converted_task.major_version = ntohs(received_task->major_version);
  converted_task.minor_version = ntohs(received_task->minor_version);
  converted_task.id = ntohl(received_task->id);
  converted_task.arith = ntohl(received_task->arith);
  converted_task.inValue1 = ntohl(received_task->inValue1);
  converted_task.inValue2 = ntohl(received_task->inValue2);
  converted_task.inResult = ntohl(received_task->inResult);
  converted_task.flValue1 = received_task->flValue1;
  converted_task.flValue2 = received_task->flValue2;
  converted_task.flResult = received_task->flResult;

#ifdef DEBUG
  printf("I get here\n");
  printf("Received int: %d %d %d\n", converted_task.arith, converted_task.inValue1, converted_task.inValue2);
  printf("Received float: %d %f %f\n", converted_task.arith, converted_task.flValue1, converted_task.flValue2);
#endif
  bool it_was_float = false;

  if (converted_task.arith > 4)
  {
    if (converted_task.arith == 5)
    {
      converted_task.flResult = converted_task.flValue1 + converted_task.flValue2;
    }

    else if (converted_task.arith == 6)
    {
      converted_task.flResult = converted_task.flValue1 - converted_task.flValue2;
    }

    else if (converted_task.arith == 7)
    {
      converted_task.flResult = converted_task.flValue1 * converted_task.flValue2;
    }

    else if (converted_task.arith == 8)
    {
      converted_task.flResult = converted_task.flValue1 / converted_task.flValue2;
    }

    it_was_float = true;
  }

  else
  {
    if (converted_task.arith == 1)
    {
      converted_task.inResult = converted_task.inValue1 + converted_task.inValue2;
    }

    else if (converted_task.arith == 2)
    {
      converted_task.inResult = converted_task.inValue1 - converted_task.inValue2;
    }

    else if (converted_task.arith == 3)
    {
      converted_task.inResult = converted_task.inValue1 * converted_task.inValue2;
    }

    else if (converted_task.arith == 4)
    {
      converted_task.inResult = converted_task.inValue1 / converted_task.inValue2;
    }
  }

  char result_string[64];

  if (it_was_float)
  {
    int rv = sprintf(result_string, "%.8g\n", converted_task.flResult);
    if (rv < 0)
    {
      fprintf(stderr, "sprintf float: %s\n", gai_strerror(rv));
      return 6;
    }
  }

  else
  {
    int rv = sprintf(result_string, "%d\n", converted_task.inResult);
    if (rv < 0)
    {
      fprintf(stderr, "sprintf int: %s\n", gai_strerror(rv));
      return 7;
    }
  }

  printf("I am sending: %s", result_string);
  int bytes_sent = send(internal_socket, result_string, strlen(result_string), 0);
  if (bytes_sent < 0)
  {
    printf("No message sent.\n");
    close(internal_socket);
    return 8;
  }

  memset(buffer, 0, sizeof(buffer));

  while (1)
  {
    int last_bytes_received = recv(internal_socket, buffer, sizeof(buffer), 0);
    if (last_bytes_received < 0)
    {
      printf("No message received.\n");
      close(internal_socket);
      return 9;
    }

    if (last_bytes_received == 0)
    {
      printf("Server closed.\n");
      break;
    }

    if (last_bytes_received > 0)
    {
      struct calcMessage *received_message = (struct calcMessage *)buffer;
      int message = ntohl(received_message->message);

      printf("Message: %d, Bytes: %d", message, last_bytes_received);
      printf("%s (myresult=%s)\n", buffer, result_string);
      break;
    }
  }

  close(internal_socket);
  return 0;
}
