/* mockserviced.c - mock service daemon */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */
#include <config.h>

#include "cunit/unit-service.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static volatile sig_atomic_t got_signal;

static void usage(void)
{
    fputs("Usage:\n", stderr);
    fputs("    mockserviced -s shm_name -h host\n", stderr);
    exit(EX_USAGE);
}

static void signalled(int sig)
{
    got_signal = sig;
}

/*
 * Create a bound and listening TCP4 server socket, bound to the IPv4
 * loopback address and a port chosen by the kernel.
 *
 * Returns: socket, or -1 on error.  *@portp is filled with the port
 * number.
 */
static int create_server_socket(int *portp)
{
    int sock;
    struct sockaddr_in sin;
    int r;

    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        perror("socket(TCP)");
        return -1;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (*portp)
        sin.sin_port = htons(*portp);

    r = bind(sock, (struct sockaddr *)&sin, sizeof(sin));
    if (r < 0) {
        perror("bind");
        goto error;
    }

    r = listen(sock, 5);
    if (r < 0) {
        perror("listen");
        goto error;
    }

    if (!*portp) {
        socklen_t len = sizeof(sin);
        r = getsockname(sock, (struct sockaddr *)&sin, &len);
        if (r < 0) {
            perror("getsockname");
            goto error;
        }
        if (len != sizeof(sin) || sin.sin_family != AF_INET) {
            fprintf(stderr, "Bad address from getsockname()\n");
            goto error;
        }
        *portp = ntohs(sin.sin_port);
    }

    return sock;

error:
    close(sock);
    return -1;
}

int main(int argc, char **argv)
{
    const char *shm_name = NULL;
    struct mock_service *service = NULL;
    struct sigaction sa = {0};
    int opt, r;
    int shm_fd, sock, port;

    while (-1 != (opt = getopt(argc, argv, "s:"))) {
        switch (opt) {
        case 's':
            shm_name = optarg;
            break;
        }
    }

    if (!shm_name) usage();

    shm_fd = shm_open(shm_name, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EX_OSERR);
    }

    service = mmap(NULL,
                   sizeof(*service),
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED,
                   shm_fd,
                   0);
    if (!service || service == MAP_FAILED) {
        perror("mmap");
        service = NULL;
        r = EX_OSERR;
        goto done;
    }

    close(shm_fd);
    shm_fd = -1;

    sock = create_server_socket(&port);
    if (sock < 0) {
        r = EX_OSERR;
        goto done;
    }

    service->port = port;
    sem_post(&service->semaphore);

    sa.sa_handler = &signalled;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // initialise sasl server:
    // need mutex_fake
    // sasl_set_alloc
    // sasl_set_mutex
    // sasl_server_init

    while (!got_signal) {
        r = server_accept(service);
        if (r) break;

        server_cmdloop(service);
        server_reset(service);
    }

    // sasl done

 done:
    if (sock >= 0)
        shutdown(sock, SHUT_RDWR);
    if (service)
        munmap(service, sizeof(*service));

    return r;
}
