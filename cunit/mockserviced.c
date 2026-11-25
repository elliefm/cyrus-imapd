/* mockserviced.c - mock service daemon */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */
#include <config.h>

#include "cunit/unit-service.h"

int main(int argc, char **argv)
{
    char *shm_name;
    int shm_fd;

    // XXX get shm name from command line

    shm_fd = shm_open(shm_name, O_RDRW, 0);

    // mmap
    // create socket
    // set port in shared state
    // sem_post to tell parent we're ready
    // accept loop

}
