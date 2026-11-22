#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _DOZEN_H_
#define _DOZEN_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <limits.h>
#include <stdint.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <pthread.h>
#include <string.h>
#include "liburing.h"

#ifndef DOZEN_BUFFERS
	#define DOZEN_BUFFERS 256
#endif

#ifndef DOZEN_BUFFER_SIZE
	#define DOZEN_BUFFER_SIZE 4096
#endif

#ifndef DOZEN_RING_ENTRIES
	#define DOZEN_RING_ENTRIES 64
#endif

#define dozen_get "GET"
#define dozen_post "POST"
#define dozen_put "PUT"
#define dozen_patch "PATCH"
#define dozen_delete "DELETE"

/// dozen util
#define dozen_error(fd, ...) printf(__VA_ARGS__); \
	fflush(stdout); \
	close(fd); \
	exit(1); \

/// dozen common types
typedef const char* dozen_cc;

typedef struct {
	dozen_cc body;
	dozen_cc headers;
	dozen_cc cookies;
	dozen_cc url;
} dozen_rq;
typedef const char* dozen_rs;

/// dozen thread job util
typedef struct {
	dozen_cc restrict address;
	uint16_t port;
} dozen_thread_details;

typedef enum {
	kAccept = 1,
	kRecv = 2,
	kSend = 3,
	kClose = 4,
} dozen_op_type;

static void* dozen_thread_job(void* void_details);
static struct io_uring_sqe* dozen_get_sqe(struct io_uring* ring);
static void dozen_add_multishot(struct io_uring* ring, int socket_fd);
static void dozen_close(struct io_uring* ring, int client_fd);

dozen_rs dozen_internal_routing_DONT_RENAME(dozen_cc request);

#define dozen_data_pack(op, fd) ( ((uint64_t) (uint32_t) (op) << 32) | (uint32_t) (fd) )
#define dozen_get_op(b) ( (dozen_op_type) (int32_t) (b >> 32) )
#define dozen_get_fd(b) ( (int32_t) (b & 0xFFFFFFFF) )

void dozen_init(dozen_cc restrict address, uint16_t port) {
	printf("starting up dozen at %s:%u\n", address, port);

	#ifdef DOZEN_SYNCHRONOUS
		printf("note: synchronous (main-thread) mode is enabled; if your server is slow, remove \"#define DOZEN_SYNCHRONOUS\" from your code!\n");
	#endif
	
	#ifdef DOZEN_THREADS
		printf("note: your server is using a specific thread count (%d); to fallback to thread-per-core, remove \"#define DOZEN_THREADS\" from your code\n", DOZEN_THREADS);
	#endif

	// TODO: fix
	dozen_thread_details details = { address, port };

	#ifndef DOZEN_SYNCHRONOUS
		int thread_count;
		#ifdef DOZEN_THREADS
			thread_count = DOZEN_THREADS;
		#else
			thread_count = sysconf(_SC_NPROCESSORS_ONLN);
		#endif

		pthread_t* threads = malloc(thread_count * sizeof(pthread_t));
		for (int i = 0; i < thread_count; i++) pthread_create(&threads[i], NULL, dozen_thread_job, &details);
		for (int i = 0; i < thread_count; i++) pthread_join(threads[i], NULL);
		free(threads);
	#else
		dozen_thread_job(&details);
	#endif
}

void* dozen_thread_job(void* void_details) {
	dozen_thread_details* details = (dozen_thread_details*) void_details;
	dozen_cc address = details->address;
	uint16_t port = details->port;

	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1) {
		printf("dozen: socket() error\n");
		exit(1);
	}

	int opt = 1;
	if (
		setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0 ||
		setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) < 0
	) {
		dozen_error(socket_fd, "dozen: setsockopt() error\n");
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if (inet_pton(AF_INET, address, &server.sin_addr) <= 0) {
		dozen_error(socket_fd, "dozen: inet_pton() error; is the address \"%s\" valid?\n", address);
	}
	
	if (bind(socket_fd, (const struct sockaddr*) &server, sizeof(server)) < 0) {
		dozen_error(socket_fd, "dozen: bind() error; is %s:%u already in use?\n", address, port);
	}

	if (listen(socket_fd, INT_MAX) < 0) {
		dozen_error(socket_fd, "dozen: listen() error\n");
	}

	struct io_uring_params params;
	memset(&params, 0, sizeof(params));
	params.cq_entries = DOZEN_RING_ENTRIES * 8;
	params.flags = IORING_SETUP_SUBMIT_ALL | IORING_SETUP_CQSIZE |
				   IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
	
	struct io_uring ring;
	if (io_uring_queue_init_params(DOZEN_RING_ENTRIES, &ring, &params) < 0) {
		dozen_error(socket_fd, "dozen: io_uring_queue_init_params() error\n");
	}

	if (io_uring_register_files_sparse(&ring, 1024) < 0) {
		dozen_error(socket_fd, "dozen: io_uring_register_files_sparse error()\n");
	}

	char* buffers = malloc(DOZEN_BUFFERS * DOZEN_BUFFER_SIZE);
	int err = 0;
	struct io_uring_buf_ring* buffer_ring = io_uring_setup_buf_ring(&ring, DOZEN_BUFFERS, 0, 0, &err);
	if (!buffer_ring) {
		dozen_error(socket_fd, "dozen: io_uring_setup_buf_ring() error\n");
	}

	for (int i = 0; i < DOZEN_BUFFERS; i++) {
		io_uring_buf_ring_add(
			buffer_ring, buffers + i * DOZEN_BUFFER_SIZE, DOZEN_BUFFER_SIZE, 
			i, io_uring_buf_ring_mask(DOZEN_BUFFERS), i
		);
	}
	io_uring_buf_ring_advance(buffer_ring, DOZEN_BUFFERS);

	struct io_uring_sqe* sqe = dozen_get_sqe(&ring);
	io_uring_prep_multishot_accept_direct(sqe, socket_fd, NULL, NULL, 0);
	sqe->user_data = dozen_data_pack(kAccept, socket_fd);

	struct io_uring_cqe* cqe;
	unsigned int head;
	int count;
	
	while (1) {
		if (io_uring_submit_and_wait(&ring, 1) < 0) continue;

		count = 0;
		io_uring_for_each_cqe(&ring, head, cqe) {
			switch (dozen_get_op(cqe->user_data)) {
			case kAccept: {
				if (!(cqe->flags & IORING_CQE_F_MORE))
					dozen_add_multishot(&ring, socket_fd);

				if (cqe->res < 0)
					break;

				int client_fd = cqe->res;
				// TODO: limit connections if needed
				
				struct io_uring_sqe* sqe = dozen_get_sqe(&ring);
				io_uring_prep_recv_multishot(sqe, client_fd, NULL, 0, 0);
				sqe->flags |= IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE;
				sqe->buf_group = 0;
				io_uring_sqe_set_data64(sqe, dozen_data_pack(kRecv, client_fd));
			}
				break;
			case kRecv: {
				int client_fd = dozen_get_fd(cqe->user_data);
				if (cqe->res == 0) {
					dozen_close(&ring, client_fd);
					break;
				}

				if (cqe->res < 0) {
					if (cqe->res == -ENOBUFS) 
						break;
					
					dozen_close(&ring, client_fd);
					break;
				}

				int buffer_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
				char* buffer = buffers + buffer_id * DOZEN_BUFFER_SIZE;
				buffer[cqe->res] = '\0';

				const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello Dozen!";
				io_uring_buf_ring_add(buffer_ring, buffer, DOZEN_BUFFER_SIZE, buffer_id, io_uring_buf_ring_mask(DOZEN_BUFFERS), 0);
				io_uring_buf_ring_advance(buffer_ring, 1);

				struct io_uring_sqe* sqe = dozen_get_sqe(&ring);
				io_uring_prep_send(sqe, client_fd, response, sizeof(response) - 1, 0);
				sqe->flags |= IOSQE_CQE_SKIP_SUCCESS | IOSQE_FIXED_FILE;
				io_uring_sqe_set_data64(sqe, dozen_data_pack(kSend, client_fd));
			}
				break;
			case kSend: {
				if (cqe->res < 0) {
					dozen_close(&ring, dozen_get_fd(cqe->user_data));
				}
			}
				break;
			default:
				break;
			}
			count++;
		}
		io_uring_cq_advance(&ring, count);
	}

	io_uring_free_buf_ring(&ring, buffer_ring, DOZEN_BUFFERS, 0);
	free(buffers);

	io_uring_queue_exit(&ring);
	close(socket_fd);
	return NULL;
}

struct io_uring_sqe* dozen_get_sqe(struct io_uring* ring) {
	struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		io_uring_submit(ring);
		sqe = io_uring_get_sqe(ring);
	}
	return sqe;
}

void dozen_add_multishot(struct io_uring* ring, int socket_fd) {
	struct io_uring_sqe* sqe = dozen_get_sqe(ring);
	io_uring_prep_multishot_accept_direct(sqe, socket_fd, NULL, NULL, 0);
	io_uring_sqe_set_data64(sqe, dozen_data_pack(kAccept, socket_fd));
}

void dozen_close(struct io_uring* ring, int client_fd) {
	struct io_uring_sqe* sqe = dozen_get_sqe(ring);
	io_uring_prep_close_direct(sqe, client_fd);
	sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
	io_uring_sqe_set_data64(sqe, dozen_data_pack(kClose, client_fd));
}

#endif