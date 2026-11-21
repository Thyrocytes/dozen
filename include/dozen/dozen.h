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
#include <liburing.h>

#define dozen_get "GET"
#define dozen_post "POST"
#define dozen_put "PUT"
#define dozen_patch "PATCH"
#define dozen_delete "DELETE"

typedef const char* dozen_cc;

typedef const char* dozen_rs;

typedef struct {
	dozen_cc body;
	dozen_cc headers;
	dozen_cc cookies;
	dozen_cc url;
} dozen_rq;

typedef struct {
	dozen_cc restrict address;
	uint16_t port;
} dozen_thread_details;

dozen_rs dozen_internal_route_DONT_RENAME(dozen_cc request);
void* dozen_thread_job(void* void_details);

int dozen_init(dozen_cc restrict address, uint16_t port) {
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1) {
		printf("dozen: socket() error\n");
		return 1;
	}

	int opt = 1;
	if (
		setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0 ||
		setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) < 0
	) {
		printf("dozen: setsockopt() error\n");
		close(socket_fd);
		return 1;
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if (inet_pton(AF_INET, address, &server.sin_addr) <= 0) {
		printf("dozen: inet_pton() error; is the address \"%s\" valid?\n", address);
		close(socket_fd);
		return 1;
	}
	
	if (bind(socket_fd, (const struct sockaddr*) &server, sizeof(server)) < 0) {
		printf("dozen: bind() error; is %s:%u already in use?\n", address, port);
		close(socket_fd);
		return 1;
	}

	if (listen(socket_fd, INT_MAX) < 0) {
		printf("dozen: listen() error\n");
		close(socket_fd);
		return 1;
	}

	printf("dozen listening at %s:%u\n", address, port);

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

	close(socket_fd);
	return 0;
}

void* dozen_thread_job(void* void_details) {
	dozen_thread_details* details = (dozen_thread_details*) void_details;

	struct io_uring_cqe* cqe;
	struct sockaddr_in client_addr;
	
	while (true) {
		
	}

	return NULL;
}

#endif