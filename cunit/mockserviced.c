/* mockserviced.c - mock service daemon */
/* SPDX-License-Identifier: BSD-3-Clause-CMU */
/* See COPYING file at the root of the distribution for more details. */
#include <config.h>

#include "cunit/unit-service.h"

#include "imap/mutex.h"
#include "imap/tls.h"

#include "lib/assert.h"
#include "lib/prot.h"
#include "lib/xmalloc.h"

#include <sasl/sasl.h>
#include <sasl/saslutil.h>

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
static char accept_username[MAX_USERNAME + 1];
static char accept_password[MAX_PASSWORD + 1];
static int verbose = 0;

static void cmd_authenticate(struct mock_service *service,
                             const char *mech,
                             const char *initial_in_b64);
static void cmd_starttls(struct mock_service *service);

static void usage(void)
{
    fputs("Usage: mockserviced [-v] shm_name\n", stderr);
    exit(EX_USAGE);
}

void fatal(const char *msg, int code)
{
    fprintf(stderr, "fatal error: %s\n", msg);
    exit(code);
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

/*
 * Callback which we use to fake server-side password checking for
 * those mechanisms where a plaintext password from the client is
 * available to the SASL server code (only PLAIN)
 */
static int server_checkpass(sasl_conn_t *conn __attribute__((unused)),
                            void *context __attribute__((unused)),
                            const char *user,
                            const char *pass,
                            unsigned passlen,
                            struct propctx *propctx __attribute__((unused)))
{
    if (strcmp(user, accept_username))
        return SASL_NOUSER;

    if (passlen != strlen(accept_password)
        || strncmp(pass, accept_password, passlen))
    {
        return SASL_BADAUTH;
    }

    return SASL_OK;
}

/*
 * Block until an incoming connection is received, then establish the
 * connection and initialise per-connection state.  On success, set
 * @state->is_connected.
 *
 * Returns 0 on success, -1 on failure (errors indicate some resource
 * limitation in the server, not any behaviour of the client, and
 * should be fatal).
 */
static int server_accept(struct mock_service *service, int listen)
{
    int conn_sock;
    int r;

    if (verbose > 1)
        fprintf(stderr, "Server waiting for connection\n");

    conn_sock = accept(listen, NULL, NULL);
    if (conn_sock < 0) {
        perror("accept");
        return -1;
    }
    if (verbose > 1)
        fprintf(stderr, "Server accepted connection\n");

    service->in = prot_new(conn_sock, /*read*/0);
    service->out = prot_new(dup(conn_sock), /*write*/1);

    r = sasl_server_new(CUNIT_SERVICE_SASLNAME, "localhost",
                        /*user_realm*/NULL, /*iplocalport*/NULL,
                        /*ipremoteport*/NULL, /*callbacks*/NULL,
                        /*flags*/0, &service->saslconn);
    if (r != SASL_OK) {
        fprintf(stderr, "sasl_server_new: %s\n",
                        sasl_errstring(r, NULL, NULL));
        goto error;
    }

    service->tls_conn = NULL;

    service->state.is_authenticated = false;
    service->state.is_connected = true;
    service->state.is_tls = false;

    return 0;

 error:
    if (service->in) prot_free(service->in);
    if (service->out) prot_free(service->out);

    return -1;
}

/*
 * Reset all per-connection state
 */
static void server_reset(struct mock_service *service)
{
    prot_free(service->in);
    service->in = NULL;
    prot_free(service->out);
    service->out = NULL;

    sasl_dispose(&service->saslconn);

    if (service->tls_conn) {
        tls_reset_servertls(&service->tls_conn);
        service->tls_conn = NULL;
    }

    memset(&service->state, 0, sizeof(service->state));
}

/*
 * Main routine for pushing text back to the client.
 */
static void
__attribute__((format(printf, 2, 3)))
server_printf(struct mock_service *service, const char *fmt, ...)
{
    va_list args;
    int r;
    char buf[2048];

    if (verbose > 1) {
        va_start(args, fmt);
        fprintf(stderr, "S: ");
        vfprintf(stderr, fmt, args);
        va_end(args);
    }

    va_start(args, fmt);
    r = vsnprintf(buf, sizeof(buf), fmt, args);
    assert(r < (int)sizeof(buf));
    va_end(args);

    r = prot_write(service->out, buf, strlen(buf));
    assert(r >= 0);
}

/*
 * Flush any pending output back to the client.
 */
static void server_flush(struct mock_service *service)
{
    prot_flush(service->out);
}

static const char *server_banner(struct mock_service *service)
{
    static const char default_banner[] = "mockserviced default banner";

    if (!*service->config.banner) {
        strlcpy(service->config.banner,
                default_banner,
                sizeof(service->config.banner));
    }

    return service->config.banner;
}

/*
 * Emit to the client a banner including the server's capability
 * strings.  This might be in response to the client connecting, or to
 * the client issuing the XXCAPABILITY command.  Various flags in
 * @service->config control which of the magical caps known by the
 * backend.c client code are reported.  Some other test capabilities are
 * always reported.  Depending on @service->config.caps_one_per_line, the
 * caps are listed in a multi-line format, one cap per line (LMTP, POP3,
 * ManageSieve, and sync style) or a single-line, many caps per line
 * format (IMAP style).
 */
static void server_emit_caps(struct mock_service *service)
{
    const char *saslmechs = NULL;
    char *p;
    char *b;
    int n = 0;
    const char *name;
    const char *words[256];     /* array of:
                                 * NAME, VALUE, VALUE, NULL,
                                 * NAME, VALUE, NULL,
                                 * NULL */
    char line[1024];

    /*
     * The ccSASL cap reports a list of SASL mechanism names.
     * Note that we suppress them all if STARTTLS is enabled.
     */
    if (!service->config.starttls || service->state.is_tls) {
        int got_login = 0;
        int got_plain = 0;

        /* First see what mechanisms SASL has; no point reporting
         * mechanisms which aren't actually available. */
        sasl_listmech(service->saslconn, /*user*/NULL, /*prefix*/"",
                      /*sep*/" ", /*suffix*/"", &saslmechs, /*plen*/NULL,
                      /*pcount*/NULL);
        b = xstrdup(saslmechs);

        /* Build our own list of mechanisms, which is the intersection
         * of the ones configured in SASL and the ones our server config
         * allows us. */
        words[n++] = "ccSASL";
        for (p = strtok(b, " ") ; p ; p = strtok(NULL, " ")) {
            if (!strcasecmp(p, "LOGIN") && service->config.sasl_login) {
                words[n++] = "LOGIN";
                got_login = 1;
            }
            if (!strcasecmp(p, "PLAIN") && service->config.sasl_plain) {
                words[n++] = "PLAIN";
                got_plain = 1;
            }
        }
        words[n++] = NULL;
        free(b);

        if (service->config.sasl_login && !got_login)
            fprintf(stderr, "Server failed to find requested "
                            "SASL mechanism \"LOGIN\"\n");
        if (service->config.sasl_plain && !got_plain)
            fprintf(stderr, "Server failed to find requested "
                            "SASL mechanism \"PLAIN\"\n");
    }

    /*
     * The ccSTARTTLS cap reports the ability to do STARTTLS
     */
    if (service->config.starttls) {
        words[n++] = "ccSTARTTLS";
        words[n++] = NULL;
    }

    /*
     * The ccCOMPRESS=DEFLATE cap reports the ability to do Deflate
     * compression.
     */
    if (service->config.deflate) {
        words[n++] = "ccCOMPRESS";
        words[n++] = "DEFLATE";
        words[n++] = NULL;
    }

    /*
     * Various test capabilities; the test code in the client knows what
     * to expect for these.
     */
    words[n++] = "FOO";
    words[n++] = "BAR";
    words[n++] = NULL;

    words[n++] = "BAZ";
    words[n++] = NULL;

    words[n++] = "QUUX";
    words[n++] = "FOONLY";
    words[n++] = "FMEH";
    words[n++] = NULL;

    words[n++] = "FNORD";
    words[n++] = "BOO";
    words[n++] = "GNERGH";
    words[n++] = NULL;

    words[n++] = "cApItAls";
    words[n++] = "Oslo";
    words[n++] = NULL;

    words[n++] = NULL;

    /*
     * Finally we have all the capability names and values, now
     * pull them out of words[] and emit lines to the client.
     */
    n = 0;
    if (service->config.caps_one_per_line) {
        /* Multi-line, one cap and all its values on each line (LMTP,
         * ManageSieve, POP3 and sync style) */
        while ((name = words[n++])) {
            line[0] = '\0';
            for ( ; words[n] ; n++) {
                strcat(line, " ");
                strcat(line, words[n]);
            }
            n++;
            server_printf(service, "* %s%s\r\n", name, line);
        }
        server_printf(service, "OK %s server ready\r\n",
                      server_banner(service));
    } else {
        /* One-line, NAME=VALUE pairs inside IMAP-style response code */
        line[0] = '\0';
        while ((name = words[n++])) {
            if (words[n]) {
                for ( ; words[n] ; n++) {
                    strcat(line, " ");
                    strcat(line, name);
                    strcat(line, "=");
                    strcat(line, words[n]);
                }
            } else {
                strcat(line, " ");
                strcat(line, name);
            }
            n++;
        }
        server_printf(service, "OK [CAPABILITY%s] %s server ready\r\n",
                      line, server_banner(service));
    }
    server_flush(service);
}

/*
 * Get a line of text from the client.  Blocks until
 * a while line is available.
 *
 * Returns: 0 on success, -1 on error or end of file.
 */
static int server_getline(struct mock_service *service,
                          char *buf, int maxlen)
{
    if (!prot_fgets(buf, maxlen, service->in))
        return -1;
    if (verbose > 1)
        fprintf(stderr, "C: %s\n", buf);
    return 0;
}

/*
 * Server main command loop for a single connection.  Blocks waiting for
 * commands from the client, and handles each command.  Returns when the
 * client sends a XXLOGOUT command or drops the connection.
 */
static void server_cmdloop(struct mock_service *service)
{
    char *command;
    char buf[1024];
    static const char sep[] = " \t\r\n";

    /* Emit the connection banner */
    server_emit_caps(service);

    while (server_getline(service, buf, sizeof(buf)) == 0) {
        command = strtok(buf, sep);

        if (!command) {
            server_printf(service, "BAD command\r\n");
            server_flush(service);
            continue;
        }

        if (!strcasecmp(command, "XXLOGOUT")) {
            /* graceful disconnect */
            server_printf(service, "OK\r\n");
            server_flush(service);
            break;
        } else if (!strcasecmp(command, "XXNOOP")) {
            server_printf(service, "OK\r\n");
            server_flush(service);
        } else if (!strcasecmp(command, "XXCAPABILITY")) {
            server_emit_caps(service);
        } else if (!strcasecmp(command, "XXAUTHENTICATE")) {
            char *mech = strtok(NULL, sep);
            char *initial_in = strtok(NULL, sep);
            cmd_authenticate(service, mech, initial_in);
        } else if (!strcasecmp(command, "XXSTARTTLS")) {
            cmd_starttls(service);
        } else {
            server_printf(service, "BAD command\r\n");
            server_flush(service);
        }
    }
}

/*
 * Handle the XXAUTHENTICATE command from the client.  The 1st and 2nd
 * arguments to the command on wire are passed as @mech and
 * @initial_in_b64.  Handles any SASL conversation with the client (e.g.
 * server challenges and client responses), and telling the client the
 * final status.  If successful, sets @service->is_authenticated.
 */
static void cmd_authenticate(struct mock_service *service,
                             const char *mech,
                             const char *initial_in_b64)
{
    char *word;
    const char *server_out;
    unsigned int server_out_len;
    int r;
    unsigned int buflen = 0;
    char buf[21848];
    static const char sep[] = " \t\r\n";

    if (!mech)
        goto badsyntax;

    if (initial_in_b64) {
        r = sasl_decode64(initial_in_b64, strlen(initial_in_b64),
                          buf, sizeof(buf), &buflen);
        if (r != SASL_OK)
            goto badsyntax;
    } else {
        buflen = 0;
    }

    server_out = NULL;
    server_out_len = 0;
    r = sasl_server_start(service->saslconn, mech,
                          buf, buflen,
                          &server_out, &server_out_len);

    while (r == SASL_CONTINUE) {
        if (server_out_len)
            sasl_encode64(server_out, server_out_len, buf, sizeof(buf), NULL);
        else
            strcpy(buf, "=");
        server_printf(service, "+ %s\n", buf);
        server_flush(service);

        if (server_getline(service, buf, sizeof(buf)) < 0) {
            fprintf(stderr, "No response to AUTH challenge\n");
            return;
        }
        word = strtok(buf, sep);
        if (word) {
            r = sasl_decode64(word, strlen(word), buf, sizeof(buf), &buflen);
            if (r != SASL_OK)
                goto badsyntax;
        } else {
            buflen = 0;
        }

        server_out = NULL;
        server_out_len = 0;
        r = sasl_server_step(service->saslconn, buf, buflen,
                             &server_out, &server_out_len);
    }

    if (r != SASL_OK) {
        server_printf(service, "BAD sasl error code %d\r\n", r);
        server_flush(service);
        return;
    }

    service->state.is_authenticated = 1;
    server_printf(service, "OK\r\n");
    server_flush(service);
    return;

badsyntax:
    server_printf(service, "BAD syntax\r\n");
    server_flush(service);
}

/*
 * Handle the XXSTARTTLS command from the client.
 * If successful, sets @state->is_tls.
 */
static void cmd_starttls(struct mock_service *service)
{
    int r;
    SSL *tls_conn = NULL;
    char *alpn_proto = NULL;
    static struct saslprops_t saslprops = SASLPROPS_INITIALIZER;

    r = tls_init_serverengine("mockserviced", /*verifydepth*/5,
                              /*askcert*/1, NULL);
    if (r < 0) {
        server_printf(service, "BAD error initializing TLS\r\n");
        server_flush(service);
        return;
    }

    server_printf(service, "OK Begin TLS negotiation now\r\n");
    /* must flush our buffers before starting tls */
    server_flush(service);

    r = tls_start_servertls(/*readfd*/service->in->fd,
                            /*writefd*/service->out->fd,
                            /*timeout_sec*/3000,
                            &saslprops,
                            service->config.alpn_map,
                            &tls_conn);
    if (r < 0) {
        server_printf(service, "BAD STARTTLS negotiation failed\r\n");
        server_flush(service);
        return;
    }

    /* tell SASL about the negotiated layer */
    r = saslprops_set_tls(&saslprops, service->saslconn);
    if (r != SASL_OK) {
        server_printf(service,
                      "BAD saslprops_set_tls() failed: cmd_starttls()\r\n");
        server_flush(service);
        return;
    }

    /* tell the prot layer about our new layers */
    prot_settls(service->in, tls_conn);
    prot_settls(service->out, tls_conn);
    service->tls_conn = tls_conn;
    service->state.is_tls = 1;

    if ((alpn_proto = tls_get_alpn_protocol(tls_conn))) {
        snprintf(service->state.tls_alpn_proto,
                 sizeof(service->state.tls_alpn_proto),
                 "%s", alpn_proto);
        free(alpn_proto);
    }
    else {
        memset(service->state.tls_alpn_proto,
               0,
               sizeof(service->state.tls_alpn_proto));
    }
}

int main(int argc, char **argv)
{
    const char *shm_name = NULL;
    struct mock_service *service = NULL;
    struct sigaction sa = {0};
    int opt, r;
    int shm_fd, sock = -1, port = 0;

    while (-1 != (opt = getopt(argc, argv, "hv"))) {
        switch (opt) {
        case 'v':
            verbose++;
            break;
        default:
            usage();
        }
    }

    if (optind >= argc) usage();
    shm_name = argv[optind++];

    if (optind < argc) usage();

    shm_fd = shm_open(shm_name, O_RDWR, 0);
    if (shm_fd == -1) {
        perror(shm_name);
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

    /* copy username and password to somewhere server_checkpass can see them */
    strlcpy(accept_username, service->config.username, sizeof(accept_username));
    strlcpy(accept_password, service->config.password, sizeof(accept_password));

    sock = create_server_socket(&port);
    if (sock < 0) {
        r = EX_OSERR;
        goto done;
    }

    /* write port back so clients can find us */
    r = snprintf(service->port, sizeof(service->port), "%d", port);
    if (r <= 0 || (unsigned) r >= sizeof(service->port)) {
        r = EX_SOFTWARE;
        goto done;
    }

    sa.sa_handler = &signalled;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sasl_set_alloc(malloc, calloc, realloc, free);
    sasl_set_mutex(cyrus_mutex_alloc,
                   cyrus_mutex_lock,
                   cyrus_mutex_unlock,
                   cyrus_mutex_free);

    r = sasl_server_init((const struct sasl_callback []) {
                            { SASL_CB_SERVER_USERDB_CHECKPASS,
                                (void *) &server_checkpass, NULL },
                            { SASL_CB_LIST_END, NULL, NULL },
                         },
                         "mockserviced");
    if (r != SASL_OK) {
        fprintf(stderr, "sasl_server_init: %s\n",
                        sasl_errstring(r, NULL, NULL));
        goto done;
    }

    /* okay, we're ready now */
    sem_post(&service->semaphore);

    while (!got_signal) {
        r = server_accept(service, sock);
        if (r) break;

        server_cmdloop(service);
        server_reset(service);
    }

    sasl_done();

 done:
    if (sock >= 0)
        shutdown(sock, SHUT_RDWR);
    if (service)
        munmap(service, sizeof(*service));

    return r;
}
