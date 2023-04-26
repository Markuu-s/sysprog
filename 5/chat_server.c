#include "chat.h"
#include "chat_server.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>

#include <sys/epoll.h>
#include <fcntl.h>

#define DEBUG


#define MAX_BUFFER_SIZE 1024

#define BOTH() EPOLLIN | EPOLLET

struct chat_peer {
	/** Client's socket. To read/write messages. */
	int socket;
	/** Output buffer. */
    char *buffer_to_send;
    size_t capacity;
    size_t size;

//    struct epoll_event event;
};

struct chat_server {
	/** Listening socket. To accept new clients. */
	int socket;
	/** Array of peers. */
	struct chat_peer peers[1024];
    int num_peers;

    struct epoll_event event;
    int epfd;

    char *recieved_buffer;
    int size_recieved_buffer;
    int capacity_recieved_buffer;
};

struct chat_server *
chat_server_new(void)
{
	struct chat_server *server = calloc(1, sizeof(*server));
	server->socket = -1;
    server->num_peers = 0;

    server->size_recieved_buffer = 0;
    server->capacity_recieved_buffer = 2;
    server->recieved_buffer = calloc(server->capacity_recieved_buffer, sizeof(char));

	return server;
}

void
chat_server_delete(struct chat_server *server)
{
	if (server->socket >= 0)
		close(server->socket);

    free(server->recieved_buffer);
	free(server);
}

int
chat_server_listen(struct chat_server *server, uint16_t port)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(port);
	/* Listen on all IPs of this machine. */
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/*
	 * 1) Create a server socket (function socket()).
	 * 2) Bind the server socket to addr (function bind()).
	 * 3) Listen the server socket (function listen()).
	 * 4) Create epoll/kqueue if needed.
	 */
    if ((server->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() failed");
        return CHAT_ERR_SYS;
    }

    int optval = 1;
    if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt() failed");
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    fcntl(server->socket, F_SETFL, O_NONBLOCK | fcntl(server->socket, F_GETFL, 0));

    if (bind(server->socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        close(server->socket);
        return CHAT_ERR_PORT_BUSY;
    }

    if (listen(server->socket, SOMAXCONN) < 0) {
        perror("listen() failed");
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    if ((server->epfd = epoll_create(1)) < 0) {
        perror("epoll_create() failed");
        close(server->socket);
        return CHAT_ERR_SYS;
    }


    server->event.events = BOTH();
    server->event.data.fd = server->socket;
    if(epoll_ctl(server->epfd, EPOLL_CTL_ADD, server->socket, &server->event) < 0) {
        perror("epoll_ctl() failed");
        close(server->socket);
        close(server->epfd);
        return CHAT_ERR_SYS;
    }

	return 0;
}

struct chat_message *
chat_server_pop_next(struct chat_server *server)
{
    struct chat_message *chat_message = NULL;

    int cursor = 0;
    for(; cursor < server->size_recieved_buffer && server->recieved_buffer[cursor] != '\n'; ++cursor);
    ++cursor;
    if (cursor <= server->size_recieved_buffer) {
        chat_message = calloc(1, sizeof(struct chat_message));
        chat_message->data = calloc(cursor + 1, sizeof(char));
        strncpy(chat_message->data, server->recieved_buffer, cursor);
        chat_message->data[cursor] = '\0';

        for(int i = cursor; i < server->size_recieved_buffer; ++i) {
            server->recieved_buffer[i - cursor] = server->recieved_buffer[i];
        }
        server->size_recieved_buffer -= cursor;
    }

    return chat_message;
}

int
chat_server_update(struct chat_server *server, double timeout)
{
	/*
	 * 1) Wait on epoll for update on any socket.
	 * 2) Handle the update.
	 * 2.1) If the update was on listen-socket, then you probably need to
	 *     call accept() on it - a new client wants to join.
	 * 2.2) If the update was on a client-socket, then you might want to
	 *     read/write on it.
	 */
    if (server->socket < 0) {
        return CHAT_ERR_NOT_STARTED;
    }

    int timeout_ms = timeout >= 0 ? (int)timeout * 1000 : -1;

    int epoll_events_count;
    int size_events = server->num_peers + 1;
    struct epoll_event *events = calloc(size_events, sizeof(struct epoll_event));

    DEBUG_PRINT("Wait\n");
    if ((epoll_events_count = epoll_wait(server->epfd, events, size_events, timeout_ms)) < 0) {
        perror("epoll_wait() failed");
        free(events);
        return CHAT_ERR_SYS;
    } else if (epoll_events_count == 0) {
        perror("timeout");
        free(events);
        return CHAT_ERR_TIMEOUT;
    }

    for(int i = 0; i < size_events; ++i) {
        if (events[i].events & EPOLLIN && events[i].data.fd == server->socket) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd;
            if ((client_fd = accept(server->socket, (struct sockaddr*)&client_addr, &client_len)) < 0) {
                perror("accept() failed");
                free(events);
                return CHAT_ERR_SYS;
            }

            fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);

            struct epoll_event epoll_event;
            epoll_event.data.fd = client_fd;
            epoll_event.events = BOTH();

            if (epoll_ctl(server->epfd, EPOLL_CTL_ADD, client_fd, &epoll_event) < 0) {
                perror("epoll_ctl() failed");
                close(client_fd);
                free(events);
                return CHAT_ERR_SYS;
            }

            server->peers[server->num_peers].capacity = 2;
            server->peers[server->num_peers].size = 0;
            server->peers[server->num_peers].buffer_to_send = calloc(server->peers[server->num_peers].capacity, sizeof(char));
            server->peers[server->num_peers++].socket = client_fd;

            DEBUG_PRINT("Connect new client with fd: %d\n", client_fd);
            continue;
        }

        if (events[i].events & EPOLLOUT) { // SERVER write
            DEBUG_PRINT("Server WRITE to FD: %d\n", events[i].data.fd);
            struct chat_peer* chat_peer = NULL;
            for(int j = 0; j < server->num_peers; ++j) {
                if (server->peers[j].socket == events[i].data.fd) {
                    chat_peer = &server->peers[j];
                }
            }
            if (chat_peer == NULL) {
                perror("chat_peer");
                exit(EXIT_FAILURE);
            }

            ssize_t send_bytes_check = send(events[i].data.fd, chat_peer->buffer_to_send, chat_peer->size, 0);
            if (send_bytes_check < 0) {
                return CHAT_ERR_SYS;
            }
            size_t send_bytes = (size_t)send_bytes_check;
            if (send_bytes != chat_peer->size) {
                for(int j = 0; j + send_bytes != chat_peer->size ; ++j) {
                    chat_peer->buffer_to_send[j] = chat_peer->buffer_to_send[j + send_bytes];
                }

                chat_peer->size -= send_bytes;
            } else {
                chat_peer->size -= send_bytes;

                struct epoll_event epoll_event;
                epoll_event.events = BOTH();
                epoll_event.data.fd = chat_peer->socket;
                if (epoll_ctl(server->epfd, EPOLL_CTL_MOD, chat_peer->socket, &epoll_event) < 0) {
                    perror("epoll_ctl() failed");
                    return CHAT_ERR_SYS;
                }
            }
        }

        if (events[i].events & EPOLLIN) { // CLIENT write
            DEBUG_PRINT("Client WRITE with FD: %d\n", events[i].data.fd);

            char buf[MAX_BUFFER_SIZE];
            ssize_t rec = recv(events[i].data.fd, buf, MAX_BUFFER_SIZE, MSG_DONTWAIT);

            for(int j = 0; j < rec; ++j) {
                if (j + server->size_recieved_buffer >= server->capacity_recieved_buffer) {
                    server->capacity_recieved_buffer *= 2;
                    server->recieved_buffer = realloc(server->recieved_buffer, server->capacity_recieved_buffer * sizeof(char));
                }
                server->recieved_buffer[j + server->size_recieved_buffer] = buf[j];
            }
            server->size_recieved_buffer += (int)rec;

            buf[rec] = '\0'; // TODO remove
            DEBUG_PRINT("New message: %s\n", buf);
            if (rec == 0) {
                // TODO disconnect
            }

            for(int j = 0; j < server->num_peers; ++j) {
                if (server->peers[j].socket != events[i].data.fd) {
                    DEBUG_PRINT("SET epollout to client with FD: %d\n", server->peers[j].socket);
                    struct epoll_event epoll_event;
                    epoll_event.data.fd = server->peers[j].socket;
                    epoll_event.events = BOTH() | EPOLLOUT;
                    if (epoll_ctl(server->epfd, EPOLL_CTL_MOD, server->peers[j].socket, &epoll_event) < 0) {
                        perror("epoll_ctl() failed");
                        return CHAT_ERR_SYS;
                    }
                }
            }

            for(int j = 0; j < server->num_peers; ++j) {
                if (events[i].data.fd == server->peers[j].socket) continue;
                for(int k = 0; k < rec; ++k) {
                    if (k + server->peers[j].size >= server->peers[j].capacity) {
                        server->peers[j].capacity *= 2;
                        server->peers[j].buffer_to_send = realloc(server->peers[j].buffer_to_send, sizeof(char) * server->peers[j].capacity);
                    }
                    server->peers[j].buffer_to_send[k + server->peers[j].size] = buf[k];
                }
                server->peers[j].size += rec;
            }
        }
    }

    free(events);
    return 0;
}

int
chat_server_get_socket(const struct chat_server *server)
{
    return server->socket;
}


int
chat_server_get_descriptor(const struct chat_server *server)
{
#if NEED_SERVER_FEED
	/* IMPLEMENT THIS FUNCTION if want +5 points. */

	/*
	 * Server has multiple sockets - own and from connected clients. Hence
	 * you can't return a socket here. But if you are using epoll/kqueue,
	 * then you can return their descriptor. These descriptors can be polled
	 * just like sockets and will return an event when any of their owned
	 * descriptors has any events.
	 *
	 * For example, assume you created an epoll descriptor and added to
	 * there a listen-socket and a few client-sockets. Now if you will call
	 * poll() on the epoll's descriptor, then on return from poll() you can
	 * be sure epoll_wait() can return something useful for some of those
	 * sockets.
	 */
#endif
    return server->socket;
}

int
chat_server_get_events(const struct chat_server *server)
{
	/*
	 * IMPLEMENT THIS FUNCTION - add OUTPUT event if has non-empty output
	 * buffer in any of the client-sockets.
	 */
    if (server->socket == -1) {
        return 0;
    }

    int res = CHAT_EVENT_INPUT;
    for(int i = 0; i < server->num_peers; ++i) {
        if (server->peers[i].socket != -1 && (server->peers[i].size > 0)) {
            return res | CHAT_EVENT_OUTPUT;
        }
    }

    return res;
}

int
chat_server_feed(struct chat_server *server, const char *msg, uint32_t msg_size)
{
#if NEED_SERVER_FEED
	/* IMPLEMENT THIS FUNCTION if want +5 points. */
#endif
    (void)server;
    (void)msg;
    (void)msg_size;
	return CHAT_ERR_NOT_IMPLEMENTED;
}
