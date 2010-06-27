/*
 * mod_gearman_status
 *
 * Copyright (c) 2010, Amir Mohammad Saied <amirsaied@gmail.com>
 *
 * This source file is subject to the New BSD license, That is bundled
 * with this package in the file LICENSE, and is available through
 * the world-wide-web at
 * http://www.opensource.org/licenses/bsd-license.php
 * If you did not receive a copy of the new BSDlicense and are unable
 * to obtain it through the world-wide-web, please send a note to
 * amirsaied@gmail.com so we can mail you a copy immediately.
 */

#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_log.h"
#include "ap_config.h"

#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

module AP_MODULE_DECLARE_DATA gearman_status_module;

typedef struct {
    const char *hostname;
    int port;
} mod_gearman_status_config;

enum task {
    TASK_STATUS  = 0,
    TASK_WORKERS = 1
};

int readline(int socket_fd, char *buffer, size_t len)
{
    char *bufx = buffer;
    static char *bp;
    static int cnt = 0;
    static char b[1500];
    char c;

    while (--len > 0) {
        if (--cnt <= 0) {
            cnt = recv(socket_fd, b, sizeof(b), 0);
            if (cnt < 0) {
                if (errno == EINTR) {
                    len++;
                    continue;
                }
                return -1;
            }
            if (cnt == 0)
                return 0;
            bp = b;
        }
        c = *bp++;
        *buffer++ = c;
        if (c == '\n') {
            *buffer = '\0';
            return buffer - bufx;
        }
    }

    return -1;
}

void get_info(int socket_fd, request_rec *r, int task)
{
    char line[128];
    char buffer[50];
    int i;
    char *delim;
    char *token;

    if (task == TASK_WORKERS) {
        ap_rputs("<h2>Workers</h2>", r);
        ap_rputs("<table border='0'><tr><th>File Descriptor</th><th>IP Address</th><th>Client ID</th><th>Function</th></tr>", r);
        sprintf(buffer, "workers\n");
        delim = " ";
    } else if (task == TASK_STATUS) {
        ap_rputs("<h2>Status</h2>", r);
        ap_rputs("<table border='0'><tr><th>Function</th><th>Total</th><th>Running</th><th>Available Workers</th></tr>", r);
        sprintf(buffer, "status\n");
        delim = "\t";
    }
    write(socket_fd, buffer, strlen(buffer));
    while (readline(socket_fd, line, sizeof(line)) > 0) {
        if (line[0] == '.' && line[1] == '\n')
            break;
        ap_rputs("<tr>", r);
        for (i = 0, token = strtok(line, delim); token; token = strtok(NULL, delim), i++) {
            if (token == NULL)
                break;

            if (task == TASK_WORKERS && i == 3)
                continue;

            ap_rprintf(r, "<td>%s</td>", token);
        }

        ap_rputs("</tr>", r);
    }
    ap_rputs("</table>", r);
}

void get_version(int socket_fd, request_rec *r)
{
    char line[10];
    char buffer[50];
    sprintf(buffer, "version\n");
    write(socket_fd, buffer, strlen(buffer));
    if (readline(socket_fd, line, sizeof(line)) > 0) {
        ap_rprintf(r, "Server Version: %s", line);
    }
}

static int gearman_status_handler(request_rec *r)
{
    int socket_fd;
    struct sockaddr_in name;
    struct hostent* hostinfo;
    const char *hostname;
    int port, status = 1;
    mod_gearman_status_config *cfg =
        ap_get_module_config(r->server->module_config, &gearman_status_module);

    hostname = strdup(cfg->hostname);
    port = cfg->port;

    if (strcmp(r->handler, "gearman_status")) {
        return DECLINED;
    }
    r->content_type = "text/html";

    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    name.sin_family = AF_INET;
    hostinfo = gethostbyname(hostname);
    name.sin_addr = *((struct in_addr *) hostinfo->h_addr);
    name.sin_port = htons(port);
    if (connect(socket_fd, &name, sizeof(struct sockaddr_in)) < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "Error connecting to Gearman server %s:%d", hostname, port
        );
        status = 0;
    }

    if (!r->header_only) {
        ap_rputs(DOCTYPE_HTML_3_2
                "<html>\n<head>\n<title>Gearman Status</title>\n",
                r);
        ap_rputs("<style type='text/css'>body{font-family: 'Trebuchet MS';color: #444;background: #f9f9f9;}h1{background: #eee;border: 1px solid #ddd;padding: 3px;text-shadow: #ccc 1px 1px 0;color: #756857;text-transform:uppercase;}h2{padding: 3px;text-shadow: #ccc 1px 1px 0;color: #ACA39C;text-transform:uppercase;border-bottom: 1px dotted #ddd;display: inline-block;}hr{color: transparent;}table{width: 100%;border: 1px solid #ddd;border-spacing:0px;}table th{border-bottom: 1px dotted #ddd;background: #eee;padding: 5px;font-size: 15px;text-shadow: #fff 1px 1px 0;}table td{text-align: center;padding: 5px;font-size: 13px;color: #444;text-shadow: #ccc 1px 1px 0;}</style>", r);
        ap_rputs("</head>\n<body>\n", r);
        if (status) {
            ap_rprintf(r, "<h1>Gearman Server Status for %s</h1>", hostname);
            get_version(socket_fd, r);
            ap_rputs("<hr/>", r);
            get_info(socket_fd, r, TASK_STATUS);
            ap_rputs("<hr/>", r);
            get_info(socket_fd, r, TASK_WORKERS);
        } else {
            ap_rputs("Error connecting to Gearman server", r);
        }
        ap_rputs("</body></html>", r);
    }
    shutdown(socket_fd, 2);

    return OK;
}

static void gearman_status_register_hooks(apr_pool_t *p)
{
    ap_hook_handler(gearman_status_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

static const char *set_mod_gearman_status_hostname(
        cmd_parms *params, void *mconfig, const char *arg
) {
    mod_gearman_status_config *cfg =
        ap_get_module_config(params->server->module_config, &gearman_status_module);

    cfg->hostname = (char *)arg;

    return NULL;
}

static const char *set_mod_gearman_status_port(
        cmd_parms *params, void *mconfig, const char *arg
) {
    mod_gearman_status_config *cfg =
        ap_get_module_config(params->server->module_config, &gearman_status_module);

    cfg->port = atoi((char *)arg);

    return NULL;
}

static const command_rec gearman_status_commands[] =
{
    AP_INIT_TAKE1(
        "GearmanHostname",
        set_mod_gearman_status_hostname,
        NULL,
        RSRC_CONF,
        "GearmanHostname <string> -- Gearman hostname."
    ),
    AP_INIT_TAKE1(
        "GearmanPort",
        set_mod_gearman_status_port,
        NULL,
        RSRC_CONF,
        "GearmanPort <integer> -- Gearman port."
    ),
    {NULL}
};

static void *create_mod_gearman_status_config(apr_pool_t *p, server_rec *s)
{
    mod_gearman_status_config *cfg;

    cfg = (mod_gearman_status_config *) apr_pcalloc(p, sizeof(mod_gearman_status_config));

    cfg->hostname = strdup("localhost");
    cfg->port = 4730;

    return (void *) cfg;
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA gearman_status_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                               /* create per-dir    config structures */
    NULL,                               /* merge  per-dir    config structures */
    create_mod_gearman_status_config,   /* create per-server config structures */
    NULL,                               /* merge  per-server config structures */
    gearman_status_commands,            /* table of config file commands       */
    gearman_status_register_hooks       /* register hooks                      */
};
