/* unit-service.h - CUnit service API */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */

#ifndef CUNIT_UNIT_SERVICE_H
#define CUNIT_UNIT_SERVICE_H

#include <semaphore.h>

#define MAX_ALPN_MAP    (4)
#define MAX_SHM_NAME    (255)

struct mock_service
{
    char shm_name[MAX_SHM_NAME + 1];
    sem_t semaphore;
    pid_t pid;
    int port;

    /* dynamic configuration */
    struct server_config config;

    /* global state */
    int rend_sock;
    char banner[64];

    /* per-connection state */
    int is_connected;
    int is_authenticated;
    int is_tls;
    sasl_conn_t *saslconn;
    SSL *tls_conn;
    char tls_alpn_proto[64];
    struct protstream *in;
    struct protstream *out;
};

#endif
