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

#define SIZE_BUFFER 1024


struct chat_client {
	/** Socket connected to the server. */
	int socket;

	/** Array of received messages. */
	char received_buffer[SIZE_BUFFER];
    size_t received_buffer_size;

	/** Output buffer. */
    char buffer_to_send[SIZE_BUFFER];
    size_t buffer_to_send_size;

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

	return client;
}

void
chat_client_delete(struct chat_client *client)
{
	if (client->socket >= 0)
		close(client->socket);

    free(client->name);
	free(client);
}

int
chat_client_connect(struct chat_client *client, const char *addr)
{
    if (client->socket != -1) {
        return CHAT_ERR_ALREADY_STARTED;
    }

	/*
	 * 1) Use getaddrinfo() to resolve addr to struct sockaddr_in.
	 * 2) Create a client socket (function socket()).
	 * 3) Connect it by the found address (function connect()).
	 */
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
        return CHAT_ERR_SYS;
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
    size_t cursor = 0;
    for(; cursor < client->received_buffer_size && client->received_buffer[cursor] != '\n'; ++cursor);
    ++cursor;
    if (cursor <= client->received_buffer_size) {
        chat_message = calloc(1, sizeof(struct chat_message));
        chat_message->data = calloc(cursor + 1, sizeof(char));
        strncpy(chat_message->data, client->received_buffer, cursor);
        chat_message->data[cursor] = '\0';

        for(size_t i = cursor; i < client->received_buffer_size; ++i) {
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
	/*
	 * The easiest way to wait for updates on a single socket with a timeout
	 * is to use poll(). Epoll is good for many sockets, poll is good for a
	 * few.
	 *
	 * You create one struct pollfd, fill it, call poll() on it, handle the
	 * events (do read/write).
	 */
    int timeout_ms = timeout >= 0 ? (int)timeout * 1000 : -1;
    client->pollfd.revents = 0;
    int err_timeout = poll(&client->pollfd, 1, timeout_ms);
    if (err_timeout <= 0) {
        return CHAT_ERR_TIMEOUT;
    }

    if (client->pollfd.revents & POLLOUT) {
        DEBUG_PRINT("POLLOUT\n");

        ssize_t sent_bytes = send(client->socket, client->buffer_to_send, client->buffer_to_send_size, 0);
        if (sent_bytes < 0) {
            return CHAT_ERR_SYS;
        }
        if (sent_bytes == 0) {
            close(client->socket);
            client->socket = -1;
            return 0;
        }
        else {
            for(size_t i = sent_bytes; i < client->buffer_to_send_size; ++i) {
                client->buffer_to_send[i - sent_bytes] = client->buffer_to_send[i];
            }

            client->buffer_to_send_size -= sent_bytes;
            if (client->buffer_to_send_size == 0) {
                client->pollfd.events ^= POLLOUT;
            }
        }

    }
    if (client->pollfd.revents & POLLIN) {
        DEBUG_PRINT("POLLIN\n");
        ssize_t recv_bytes = recv(client->socket, client->received_buffer + client->received_buffer_size, SIZE_BUFFER - client->received_buffer_size, MSG_DONTWAIT);
        if (recv_bytes < 0) {
            return CHAT_ERR_SYS;
        } else if (recv_bytes == 0) {
            close(client->socket);
            client->socket = -1;
            return 0;
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
	/*
	 * IMPLEMENT THIS FUNCTION - add OUTPUT event if has non-empty output
	 * buffer.
	 */

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

    char *new_msg = calloc(msg_size, sizeof(char));
    int flag = 0;
    int cursor = 0;
    for(uint32_t i = 0; i < msg_size; ++i) {
        if (msg[i] != ' ') {
            new_msg[cursor++] = msg[i];
            flag = msg[i] == '\n' ? 0 : 1;
        } else {
            if (flag) {
                new_msg[cursor++] = msg[i];
            }
        }
    }

    if (cursor == 0) {
        return -1;
    }

    strncpy(client->buffer_to_send + client->buffer_to_send_size, new_msg, cursor);
    client->buffer_to_send_size += cursor;

    client->pollfd.events |= POLLOUT;

    free(new_msg);
    return 0;
}
