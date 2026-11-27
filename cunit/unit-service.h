/* unit-service.h - CUnit service API */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */

#ifndef CUNIT_UNIT_SERVICE_H
#define CUNIT_UNIT_SERVICE_H

#include "imap/protocol.h"
#include "imap/tls.h"

#include <sasl/sasl.h>

#include <semaphore.h>
#include <stdbool.h>

#define MAX_ALPN_MAP    (4)
#define MAX_SHM_NAME    (255)
#define MAX_PORT        (31)
#define MAX_BANNER      (63)
#define MAX_USERID      (15)
#define MAX_PASSWORD    (15)

#define CUNIT_SERVICE_SASLNAME  "vorpal"
#define CUNIT_SERVICE_USERID    "fbloggs"
#define CUNIT_SERVICE_PASSWORD  "shibboleth"

struct mock_service_config
{
    bool sasl_plain;
    bool sasl_login;
    bool starttls;
    bool deflate;
    bool caps_one_per_line;
    struct tls_alpn_t alpn_map[MAX_ALPN_MAP + 1];
    char userid[MAX_USERID + 1];
    char password[MAX_PASSWORD + 1];
    char banner[MAX_BANNER + 1];
};
extern const struct mock_service_config cunit_service_default_config;

struct mock_service
{
    char shm_name[MAX_SHM_NAME + 1];
    char port[MAX_PORT + 1];
    sem_t semaphore;
    pid_t pid;

    struct protstream *in;
    struct protstream *out;
    sasl_conn_t *saslconn;
    SSL *tls_conn;

    struct {
        bool is_connected;
        bool is_authenticated;
        bool is_tls;
        char tls_alpn_proto[64];
    } state;

    struct mock_service_config config;
};

extern struct mock_service *cunit_service_spawn(
    const char *name,
    const char *banner,
    const struct mock_service_config *config,
    struct protocol_t *protocol);

extern void cunit_service_destroy(struct mock_service **pservice);

extern void cunit_service_set_banner(struct mock_service *service,
                                     const char *banner);
#endif
