/* 
 **  Gearman Status Module.
 **
 **  To play with this module first compile it into a DSO
 **  file and install it into Apache's modules directory 
 **  by running:
 **
 **    $ apxs -c -i mod_gearman_status.c
 **
 **  Then activate it in Apache's apache2.conf file for instance
 **  for the URL /gearman-status in as follows:
 **
 **    #   apache2.conf
 **    LoadModule gearman_status_module modules/mod_gearman_status.so
 **    <Location /gearman-status>
 **    SetHandler gearman_status
 **    </Location>
 **
 **  Then after restarting Apache via
 **
 **    $ apachectl restart
 **
 **  you immediately can request the URL /gearman-status and watch for the
 **  output of this module. This can be achieved for instance via:
 **
 **    $ lynx -mime_header http://localhost/gearman-status 
 **
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

    hostname = strdup("localhost");
    port = 4730;

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
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "Error connecting to Gearman server");
        status = 0;
    }

    if (!r->header_only) {
        ap_rputs(DOCTYPE_HTML_3_2
                "<html>\n\t<head>\n\t\t<title>Gearman Status</title>\n\t</head>\n<body>\n",
                r);
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
        ap_rputs(ap_psignature("<hr />\n",r), r);
        ap_rputs("</body></html>", r);
    }
    shutdown(socket_fd, 2);

    return OK;
}

static void gearman_status_register_hooks(apr_pool_t *p)
{
    ap_hook_handler(gearman_status_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA gearman_status_module = {
    STANDARD20_MODULE_STUFF, 
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    gearman_status_register_hooks  /* register hooks                      */
};

