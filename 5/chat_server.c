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
#include <assert.h>

#define DEBUG



#define BOTH() EPOLLIN

struct chat_peer {
	/** Client's socket. To read/write messages. */
	int socket;
	/** Output buffer. */
    char *buffer_to_send;
    char *buffer_to_read;

    size_t read_capacity;
    size_t read_size;

    size_t capacity;
    size_t size;
    struct epoll_event event;

//    struct epoll_event event;
};

struct chat_server {
	/** Listening socket. To accept new clients. */
	int socket;
	/** Array of peers. */
	struct chat_peer peers[100];
    int num_peers;
    int counter;

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
    server->counter = 0;
    server->capacity_recieved_buffer = 0;
    server->recieved_buffer = calloc(server->capacity_recieved_buffer, sizeof(char));

    return server;
}

void
chat_server_delete(struct chat_server *server)
{
	if (server->socket >= 0)
		close(server->socket);

    for(int i = 0; i < server->num_peers; ++i){
        if(server->peers[i].socket != 0){
            close(server->peers[i].socket);
        }

        if(server->peers[i].capacity > 0){
            free(server->peers[i].buffer_to_send);
            server->peers[i].capacity = 0;
            server->peers[i].size = 0;
        }

        if(server->peers[i].read_capacity > 0){
            free(server->peers[i].buffer_to_read);
            server->peers[i].read_capacity = 0;
            server->peers[i].read_size = 0;
        }
    }

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

    if ((server->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return CHAT_ERR_SYS;
    }

    int optval = 1;
    if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    if(fcntl(server->socket, F_SETFL, O_NONBLOCK | fcntl(server->socket, F_GETFL, 0)) < 0){
        close(server->socket);
        return  CHAT_ERR_SYS;
    }

    if (bind(server->socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server->socket);
        return CHAT_ERR_PORT_BUSY;
    }

    if (listen(server->socket, SOMAXCONN) < 0) {
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    if ((server->epfd = epoll_create(1)) < 0) {
        close(server->socket);
        return CHAT_ERR_SYS;
    }


    server->event.events = BOTH();
    server->event.data.fd = server->socket;
    if(epoll_ctl(server->epfd, EPOLL_CTL_ADD, server->socket, &server->event) < 0) {
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

    if(server->capacity_recieved_buffer == 0){
        return chat_message;
    }

    char * messageEnd;
    if((messageEnd = strchr(server->recieved_buffer, '\n')) == NULL){
        return NULL;
    }

    size_t size = messageEnd - server->recieved_buffer + 1;
    chat_message = calloc(1, sizeof(struct chat_message));
    chat_message->data = calloc(size, sizeof(char));
    strncpy(chat_message->data, server->recieved_buffer, size);
    chat_message->data[size - 1] = '\0';

    if((int)size >= server->size_recieved_buffer){
        memset(server->recieved_buffer, '\0', server->capacity_recieved_buffer);
        server->size_recieved_buffer = 0;
    }else {
        char *copy = strdup(server->recieved_buffer + size);
        memset(server->recieved_buffer, '\0', server->capacity_recieved_buffer);
        server->size_recieved_buffer -= size;
        strncpy(server->recieved_buffer, copy, server->size_recieved_buffer);
        free(copy);
    }

    return chat_message;
}

int
chat_server_update(struct chat_server *server, double timeout)
{
    if (server->socket < 0) {
        return CHAT_ERR_NOT_STARTED;
    }

    int epoll_events_count;
    int size_events = server->num_peers + 1;
    struct epoll_event *events = calloc(size_events, sizeof(struct epoll_event));

    if ((epoll_events_count = epoll_wait(server->epfd, events, size_events, (int)(timeout * 1000))) < 0) {
        free(events);
        return CHAT_ERR_SYS;
    } else if (epoll_events_count == 0) {
        free(events);
        return CHAT_ERR_TIMEOUT;
    }

    for(int i = 0; i < size_events; ++i) {
        if (events[i].events & EPOLLIN && events[i].data.fd == server->socket) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd;
            if ((client_fd = accept(server->socket, (struct sockaddr*)&client_addr, &client_len)) < 0) {
                free(events);
                return CHAT_ERR_SYS;
            }

            if(fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK) < 0){
                free(events);
                return CHAT_ERR_SYS;
            }

            server->peers[server->num_peers].event.events = BOTH();
            server->peers[server->num_peers].event.data.fd = client_fd;

            if (epoll_ctl(server->epfd, EPOLL_CTL_ADD, client_fd, &server->peers[server->num_peers].event) < 0) {
                close(client_fd);
                free(events);
                return CHAT_ERR_SYS;
            }

            server->peers[server->num_peers].capacity = 0;
            server->peers[server->num_peers].read_capacity = 0;
            server->peers[server->num_peers].read_size = 0;
            server->peers[server->num_peers].size = 0;

            server->peers[server->num_peers++].socket = client_fd;
            continue;
        }

        if (events[i].events & EPOLLIN) { // CLIENT write
            struct chat_peer* chat_peer = NULL;
            for(int j = 0; j < server->num_peers; ++j) {
                if (server->peers[j].socket == events[i].data.fd) {
                    chat_peer = &server->peers[j];
                }
            }
            if (chat_peer == NULL) {
                exit(EXIT_FAILURE);
            }

            if(chat_peer->read_capacity == 0){
                chat_peer->read_capacity = MAX_BUFFER_SIZE;
                chat_peer->buffer_to_read = calloc(MAX_BUFFER_SIZE, sizeof(char));
            }

            ssize_t rec;
            char buffer[MAX_BUFFER_SIZE];

            memset(buffer, '\0',MAX_BUFFER_SIZE);

            if((rec = recv(events[i].data.fd, buffer, MAX_BUFFER_SIZE, 0)) == 0){;
                for(int z = 0; z < server->num_peers; ++z) {
                    if (server->peers[z].socket == events[i].data.fd) {
                        epoll_ctl(server->epfd, EPOLL_CTL_DEL, events[i].data.fd,  &server->peers[z].event);
                        close(server->peers[z].socket);
                        server->peers[z].size = 0;
                        free(server->peers[z].buffer_to_send);
                        server->peers[z].capacity = 0;
                        break;
                    }
                }
                free(events);
                return  0;
            }

            if(rec == 0){
                goto epollout;;
            }

            if((int)(chat_peer->read_capacity - chat_peer->read_size) <= rec + 1){
                chat_peer->read_capacity += rec + chat_peer->read_size + 1;
                chat_peer->buffer_to_read = realloc(chat_peer->buffer_to_read, chat_peer->read_capacity*sizeof(char));
            }

            strncpy(chat_peer->buffer_to_read + chat_peer->read_size, buffer, rec);
            chat_peer->read_size += rec;
            chat_peer->buffer_to_read[chat_peer->read_size] = '\0';

            int last = strlen(chat_peer->buffer_to_read);
            assert(last == (int)chat_peer->read_size);

            char * messageEnd;
            while( (messageEnd = strchr(chat_peer->buffer_to_read, '\n'))!= NULL){
                int size = messageEnd - chat_peer->buffer_to_read + 1;
                server->counter++;

                if(server->capacity_recieved_buffer - server->size_recieved_buffer < size + 1){
                    server->capacity_recieved_buffer = server->size_recieved_buffer + size + 1;
                    server->recieved_buffer = realloc(server->recieved_buffer, server->capacity_recieved_buffer*sizeof(char));
                }

                strncpy(server->recieved_buffer + server->size_recieved_buffer, chat_peer->buffer_to_read, size);
                server->size_recieved_buffer += size;

                server->recieved_buffer[server->size_recieved_buffer] = '\0';

                for(int j = 0; j < server->num_peers; ++j){
                    if(server->peers[j].socket == 0 || server->peers[j].socket == chat_peer->socket){
                        continue;
                    }

                    struct chat_peer * receiver = server->peers + j;

                    if(receiver->capacity == 0){
                        receiver->capacity = MAX_BUFFER_SIZE;
                        receiver->buffer_to_send = calloc(MAX_BUFFER_SIZE, sizeof(char));
                    }

                    if(receiver->capacity - receiver->size < (size_t)size + 1){
                        receiver->capacity = receiver->size + size + 1;
                        receiver->buffer_to_send = realloc(receiver->buffer_to_send, sizeof(char)*receiver->capacity);
                    }

                    strncpy(receiver->buffer_to_send + receiver->size, chat_peer->buffer_to_read, size);
                    receiver->size += size;

                    receiver->buffer_to_send[receiver->size] = '\0';


                    receiver->event.events = BOTH() | EPOLLOUT;
                    if (epoll_ctl(server->epfd, EPOLL_CTL_MOD, server->peers[j].socket, &receiver->event) < 0) {
                        return CHAT_ERR_SYS;
                    }
                }

                if(size >= (int)chat_peer->read_size){
                    memset(chat_peer->buffer_to_read, '\0', chat_peer->read_capacity);
                    chat_peer->read_size = 0;
                }else {
                    char *copy = strdup(chat_peer->buffer_to_read + size);
                    memset(chat_peer->buffer_to_read, '\0', chat_peer->read_capacity);
                    chat_peer->read_size = strlen(copy);
                    strncpy(chat_peer->buffer_to_read, copy, chat_peer->read_size);
                    free(copy);
                }
            }
        }

        epollout:

        if (events[i].events & EPOLLOUT) { // SERVER write
            struct chat_peer* chat_peer = NULL;
            for(int j = 0; j < server->num_peers; ++j) {
                if (server->peers[j].socket == events[i].data.fd) {
                    chat_peer = &server->peers[j];
                }
            }
            if (chat_peer == NULL) {
                exit(EXIT_FAILURE);
            }

            if(chat_peer->capacity == 0){
                continue;
            }

            ssize_t send_bytes_check = send(events[i].data.fd, chat_peer->buffer_to_send, chat_peer->size, 0);

            if (send_bytes_check < 0) {
                return CHAT_ERR_SYS;
            }

            if(send_bytes_check == 0){
                continue;
            }

            size_t send_bytes = (size_t)send_bytes_check;
            if (send_bytes >= chat_peer->size) {
                chat_peer->size -= send_bytes;

                chat_peer->event.events = BOTH();
                if (epoll_ctl(server->epfd, EPOLL_CTL_MOD, chat_peer->socket, &chat_peer->event) < 0) {
                    return CHAT_ERR_SYS;
                }
                free(chat_peer->buffer_to_send);
                chat_peer->size = 0;
                chat_peer->capacity = 0;
                continue;
            }

            for(int j = 0; j + send_bytes != chat_peer->size ; ++j) {
                chat_peer->buffer_to_send[j] = chat_peer->buffer_to_send[j + send_bytes];
            }
            char * copy = strdup(chat_peer->buffer_to_send + send_bytes);
            memset(chat_peer->buffer_to_send, '\0', chat_peer->capacity);
            chat_peer->size = strlen(copy);
            strcpy(chat_peer->buffer_to_send, copy);
            free(copy);
        }
    }

    free(events);
    return 0;
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
chat_server_get_socket(const struct chat_server *server)
{
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
