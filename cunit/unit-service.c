/* unit-service.c - CUnit service API */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */
#include <config.h>

#include "cunit/unit-service.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static unsigned serial;

EXPORTED const struct mock_service_config cunit_service_default_config =
{
    .sasl_plain = true,
    .sasl_login = false,
    .starttls = false,
    .deflate = false,
    .caps_one_per_line = true,
    .alpn_map = {{{0}, NULL, NULL }},
    .banner = {0},
};

EXPORTED struct mock_service *cunit_service_spawn(
    const char *name,
    const char *banner,
    const struct mock_service_config *config)
{
    struct mock_service *service = NULL;
    char shm_name[MAX_SHM_NAME + 1] = {0};
    struct timespec timeout;
    int shm_fd, r;
    pid_t child;

    r = snprintf(shm_name, sizeof(shm_name),
                 "/%ld-%u-%s",
                 (long) getpid(),
                 serial++,
                 name);
    if (r <= 0 || (unsigned) r >= sizeof(shm_name))
        return NULL;

    shm_fd = shm_open(shm_name,
                      O_RDWR | O_CREAT | O_EXCL,
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

    if (!config) config = &cunit_service_default_config;
    memcpy(&service->config, config, sizeof(*config));

    if (banner && *banner) {
        strlcpy(service->config.banner,
                banner,
                sizeof(service->config.banner));
    }
    else {
        strlcpy(service->config.banner,
                "mockserviced default banner",
                sizeof(service->config.banner));
    }

    memcpy(service->shm_name, shm_name, sizeof(shm_name));
    sem_init(&service->semaphore, 1, 0);

    child = fork();
    if (child < 0) goto error;

    if (child == 0) {
        /* child process */
        char *const argv[] = {
            (char []) { "-s" }, shm_name,
            // XXX pass username, password
            NULL,
        };

        execv("mockserviced", argv);
        perror("exec mockserviced");
        exit(EX_OSERR);
    }

    /* wait for child to be ready */
    timeout.tv_sec = time(NULL) + 20;
    timeout.tv_nsec = 0;
    while ((r = sem_timedwait(&service->semaphore, &timeout)) == -1
           && errno == EINTR)
        continue;

    if (r) {
        fputs("mockserviced took too long to start up!\n", stderr);
        kill(child, SIGKILL);
        while ((r = waitpid(child, NULL, 0)) == -1 && errno == EINTR)
            continue;
        goto error;
    }

    service->pid = child;
    sem_post(&service->semaphore);
    return service;

 error:
    if (service) {
        sem_destroy(&service->semaphore);
        munmap(service, sizeof(*service));
    }
    if (shm_fd != -1)
        shm_unlink(shm_name);

    return NULL;
}

EXPORTED void cunit_service_destroy(struct mock_service **pservice)
{
    struct mock_service *service = *pservice;
    char shm_name[MAX_SHM_NAME + 1];
    int sig = SIGTERM;
    pid_t reaped;

    do {
        kill(service->pid, sig);
        while ((reaped = waitpid(service->pid, NULL, 0)) == -1
               && errno == EINTR)
            continue;
        sig = SIGKILL;
    } while (reaped != service->pid);

    memcpy(shm_name, service->shm_name, sizeof(shm_name));

    sem_destroy(&service->semaphore);
    munmap(service, sizeof(*service));
    shm_unlink(shm_name);
    *pservice = NULL;
}
