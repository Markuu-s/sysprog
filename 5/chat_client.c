#include "chat.h"
#include "chat_client.h"

#include <stdlib.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <memory.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <ctype.h>
#include <assert.h>



struct chat_client {
    /** Socket connected to the server. */
    int socket;

    /** Array of received messages. */
    char* received_buffer;
    int received_buffer_size;
    int received_buffer_capacity;

    /** Output buffer. */
    char* buffer_to_send;
    int buffer_to_send_size;
    int buffer_to_send_capacity;

    struct pollfd pollfd;

    char *name;
};

struct chat_client *
chat_client_new(const char *name)
{
    struct chat_client *client = calloc(1, sizeof(*client));
    client->socket = -1;
    client->name = strdup(name);

    client->buffer_to_send_size = 0;
    client->received_buffer_size = 0;
    client->received_buffer_capacity = 0;
    client->buffer_to_send_capacity = 0;

    return client;
}

void
chat_client_delete(struct chat_client *client)
{
    if (client->socket >= 0)
        close(client->socket);

    if(client->buffer_to_send_capacity > 0){
        free(client->buffer_to_send);
        client->buffer_to_send_size = 0;
        client->buffer_to_send_capacity = 0;
    }

    if(client->received_buffer_capacity > 0){
        free(client->received_buffer);
        client->received_buffer_size = 0;
        client->received_buffer_capacity = 0;
    }

    free(client->name);
    free(client);
}

int
chat_client_connect(struct chat_client *client, const char *addr)
{
    if (client->socket != -1) {
        return CHAT_ERR_ALREADY_STARTED;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    struct addrinfo *result, *rp;

    char **argv = calloc(2, sizeof(char*));

    char *temp_addr = strdup(addr);
    int cur_argv = 0;

    char *istr = strtok(temp_addr, ":");
    while(istr != NULL) {
        argv[cur_argv++] = strdup(istr);
        istr = strtok(NULL, ":");
    }
    free(temp_addr);

    int s = getaddrinfo(argv[0], argv[1], &hints, &result);
    free(argv[0]);
    free(argv[1]);
    free(argv);

    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        freeaddrinfo(result);
        return CHAT_ERR_NO_ADDR;
    }

    for(rp = result; rp != NULL; rp = rp->ai_next) {
        if ((client->socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == -1) {
            continue;
        }

        if (connect(client->socket, result->ai_addr, result->ai_addrlen) != -1) {
            break;
        }

        close(client->socket);
    }

    if (rp == NULL) {
        perror("Could not connect");
        freeaddrinfo(result);
        return CHAT_ERR_SYS;
    }

    if(fcntl(client->socket, F_SETFL, O_NONBLOCK | fcntl(client->socket, F_GETFL, 0)) < 0){
        close(client->socket);
        freeaddrinfo(result);
        return  CHAT_ERR_SYS;
    }

    client->pollfd.fd = client->socket;
    client->pollfd.events = POLLIN;

    freeaddrinfo(result);
    return 0;
}

struct chat_message *
chat_client_pop_next(struct chat_client *client)
{
    struct chat_message *chat_message = NULL;
    int cursor = 0;
    for(; cursor < client->received_buffer_size && client->received_buffer[cursor] != '\n'; ++cursor);
    ++cursor;
    if (cursor <= client->received_buffer_size) {
        chat_message = calloc(1, sizeof(struct chat_message));
        chat_message->data = calloc(cursor + 1, sizeof(char));
        strncpy(chat_message->data, client->received_buffer, cursor);
        chat_message->data[cursor] = '\0';

        for(int i = cursor; i < client->received_buffer_size; ++i) {
            client->received_buffer[i - cursor] = client->received_buffer[i];
        }
        client->received_buffer_size -= cursor;
    }

    return chat_message;
}


int
chat_client_update(struct chat_client *client, double timeout)
{
    if (client->socket == -1) {
        return CHAT_ERR_NOT_STARTED;
    }


    int timeout_ms = timeout >= 0 ? (int)(timeout * 1000) : -1;
    client->pollfd.revents = 0;
    int err_timeout = poll(&client->pollfd, 1, timeout_ms);
    if (err_timeout <= 0) {
        return CHAT_ERR_TIMEOUT;
    }

    if (client->pollfd.revents & POLLOUT) {
        if(client->buffer_to_send_capacity == 0){
            client->pollfd.events ^= POLLOUT;
            return 0;
        }

        ssize_t sent_bytes = send(client->socket, client->buffer_to_send, client->buffer_to_send_size, 0);
        if (sent_bytes < 0) {
            return CHAT_ERR_SYS;
        }

        if (sent_bytes == 0) {
            close(client->socket);
            client->socket = -1;
            return 0;
        }

        if(client->buffer_to_send_size - sent_bytes <= 0){
            client->buffer_to_send_capacity = 0;
            client->buffer_to_send_size = 0;
            free(client->buffer_to_send);
            client->pollfd.events ^= POLLOUT;
        }else{
            char * copy = strdup(client->buffer_to_send + sent_bytes);
            memset(client->buffer_to_send, '\0', client->buffer_to_send_capacity);
            client->buffer_to_send_size = strlen(copy);
            strcpy(client->buffer_to_send, copy);
            free(copy);
        }
    }

    if (client->pollfd.revents & POLLIN) {
        if(client->received_buffer_capacity == 0){
            client->received_buffer_capacity = MAX_BUFFER_SIZE;
            client->received_buffer = calloc(client->received_buffer_capacity, sizeof(char));
            client->received_buffer_size = 0;
        }

        if(client->received_buffer_size == client->received_buffer_capacity){
            client->received_buffer_capacity *= 2;
            client->received_buffer = realloc(client->received_buffer, client->received_buffer_capacity*sizeof(char));
        }

        ssize_t recv_bytes = recv(client->socket, client->received_buffer + client->received_buffer_size, client->received_buffer_capacity - client->received_buffer_size, 0);
        if (recv_bytes < 0) {
            return CHAT_ERR_SYS;
        }
        client->received_buffer_size += recv_bytes;
    }

    return 0;
}

int
chat_client_get_descriptor(const struct chat_client *client)
{
    return client->socket;
}

int
chat_client_get_events(const struct chat_client *client)
{
    if (client->socket == -1) {
        return 0;
    }
    return ((client->pollfd.events & POLLIN) ? CHAT_EVENT_INPUT : 0) | ((client->pollfd.events & POLLOUT) ? CHAT_EVENT_OUTPUT : 0);
}

int
chat_client_feed(struct chat_client *client, const char *msg, uint32_t msg_size)
{
    if (client->socket == -1) {
        return CHAT_ERR_NOT_STARTED;
    }

    if(client->buffer_to_send_capacity == 0){
        client->buffer_to_send_capacity = MAX_BUFFER_SIZE;
        client->buffer_to_send = calloc(client->buffer_to_send_capacity, sizeof(char));
    }

    const char * message = msg;
    char * last = NULL;
    while((last = strchr(message,  '\n')) != NULL){
        int size = last - message + 1;
        if(last >= msg + msg_size){
            goto lastMsg;
        }

        char * new_message = calloc(size, sizeof(char));
        int cursor = 0;

        int start = 0, end = size - 1;
        for(; isspace(message[start]); ++start);
        for(; isspace(message[end]); --end);
        for(; start <= end; ++start) {
            new_message[cursor++] = message[start];
        }

        new_message[cursor++] = '\n';

        if(client->buffer_to_send_capacity - client->buffer_to_send_size < cursor + 1){
            client->buffer_to_send_capacity = client->buffer_to_send_size + cursor + 1;
            client->buffer_to_send = realloc(client->buffer_to_send, client->buffer_to_send_capacity*sizeof(char));
        }

        strncpy(client->buffer_to_send + client->buffer_to_send_size, new_message, cursor);
        client->buffer_to_send_size += cursor;

        client->buffer_to_send[client->buffer_to_send_size] = '\0';
        assert((int)strlen(client->buffer_to_send) == client->buffer_to_send_size);

        client->pollfd.events |= POLLOUT;

        free(new_message);
        message = last + 1;
        if (last + 1 == msg + msg_size) {
            break;
        }
    }

    int size;
    lastMsg:
    size = msg + msg_size - message;
    if(size <= 0){
        return 0;
    }

    char * new_message = calloc(size, sizeof(char));
    int cursor = 0;

    int start = 0, end = size - 1;
    for(; isspace(message[start]); ++start);
    for(; isspace(message[end]); --end);
    for(; start <= end; ++start) {
        new_message[cursor++] = message[start];
    }

    if(client->buffer_to_send_capacity - client->buffer_to_send_size < cursor + 1){
        client->buffer_to_send_capacity = client->buffer_to_send_size + cursor + 1;
        client->buffer_to_send = realloc(client->buffer_to_send, client->buffer_to_send_capacity*sizeof(char));
    }

    strncpy(client->buffer_to_send + client->buffer_to_send_size, new_message, cursor);
    client->buffer_to_send_size += cursor;

    client->buffer_to_send[client->buffer_to_send_size] = '\0';

    client->pollfd.events |= POLLOUT;

    free(new_message);
    return 0;
}
