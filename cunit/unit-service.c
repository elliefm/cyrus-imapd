/* unit-service.c - CUnit service API */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */
#include <config.h>

#include "cunit/unit-service.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

static unsigned serial;

EXPORTED struct mock_service *cunit_service_spawn(const char *name)
{
    struct mock_service *service = NULL;
    char shm_name[MAX_SHM_NAME + 1] = {0};
    int shm_fd, r;
    pid_t child;

    r = snprintf(shm_name, sizeof(shm_name),
                 "/%ld-%u-%s",
                 (long) getpid(),
                 serial++,
                 name);
    if (r <= 0 || r >= sizeof(shm_name))
        return NULL;

    shm_fd = shm_open(shm_name,
                      O_RDWR | O_CREATE | O_EXCL,
                      S_IWUSR | S_IRUSR);
    if (shm_fd == -1)
        return NULL;

    r = ftruncate(shm_fd, sizeof(*service));
    if (r) goto error;

    service = mmap(NULL,
                   sizeof(*service),
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED,
                   shm_fd,
                   0);
    if (!service || service == MAP_FAILED) {
        service = NULL;
        goto error;
    }

    close(shm_fd);
    shm_fd = -1;

    memcpy(service->shm_name, shm_name, sizeof(shm_name));
    sem_init(&service->semaphore, 1, 0);

    child = fork();
    if (child < 0) goto error;

    if (child == 0) {
        /* child process */
        const char *const argv[] = {
            "-s",
            shm_name,
            NULL,
        };

        execv("mockserviced", argv);
        perror("exec mockserviced");
        exit(EX_OSERR);
    }

    // sem_timedwait for child to be ready
    // listening port will be available in shared state

 error:
    if (service) {
        sem_destroy(&service->semaphore);
        munmap(service, sizeof(*service));
    }
    if (shm_fd != -1)
        shm_unlink(shm_name);

    return NULL;
}

EXPORTED void cunit_service_destroy(struct server_state *server_state)
{
    // signal child to exit
    // wait for child pid
    // destroy semaphore
    // munmap
    // shm_unlink
}
