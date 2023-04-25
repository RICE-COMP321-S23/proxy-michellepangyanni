/*
 * COMP 321 Project 6: Web Proxy
 *
 * This program implements a multithreaded HTTP proxy.
 *
 * <Michelle Pang, yp29; Lily Gao, qg8.>
 */ 

#include <assert.h>
#include <stdbool.h>
#include "csapp.h"

#define SBUFSIZE 40	/* Buffer size we want. */ 
#define NUMTHREADS 10	/* Number of threads we want. */ 

struct conn_info
{
	int connfd;			/* File descriptor. */
	struct sockaddr_in clientaddr;	/* Client address. */
};

/* Global variables: */
pthread_mutex_t mutex;		/* pthread mutex. */ 
pthread_cond_t cond_empty;	/* variable to wait on empty buffer. */
pthread_cond_t cond_full;	/* variable to wait on full buffer. */
unsigned int prod_index = 0;    /* Producer index into shared buffer. */ 
unsigned int cons_index = 0;    /* Consumer index into shard buffer. */
unsigned int share_cnt = 0;     /* Item count. */
FILE *logfd;		        /* Log file. */
struct conn_info *shared_buffer[SBUFSIZE];	/* Buffer Array. */

/* Functions: */
static void	client_error(int fd, const char *cause, int err_num, 
		    const char *short_msg, const char *long_msg);
static char    *create_log_entry(const struct sockaddr_in *sockaddr,
		    const char *uri, int size);
static int	parse_uri(const char *uri, char **hostnamep, char **portp,
		    char **pathnamep);
void		*consumer(void *arg);
void 		doit(int fd, struct sockaddr_in *clientaddr);
void 		sig_ignore(int sig);


/* 
 * Requires:
 *   Nothing. 
 *
 * Effects:
 *   A main routine for proxy program. This proxy can deal with multiple 
 *   threads concurrently, supported by prethreading approach.
 */
int
main(int argc, char **argv)
{

	int listenfd, connfd, i;
	pthread_t cons_tid[NUMTHREADS];
	socklen_t clientlen;
    	struct sockaddr_in clientaddr;

	/* Initialize pthread variables. */
	Pthread_mutex_init(&mutex, NULL);
	
	/* Initialize condition variables. */
	pthread_cond_init(&cond_empty, NULL);
	pthread_cond_init(&cond_full, NULL);

	/* Ignore the signal SIGPIPE. */
	Signal(SIGPIPE, sig_ignore);

	// Initialize logging.
    	logfd = fopen("proxy.log", "a+");
    	if (logfd == NULL) 
		return -1;

    	// Check command line args. 
    	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
    	}

    	listenfd = Open_listenfd(argv[1]);
    	
   	// Create threads for pre-threading.
   	for (i = 0; i < NUMTHREADS; i++) {
		Pthread_create(&cons_tid[i], NULL, consumer, NULL);
   	}

	clientlen = sizeof(clientaddr);

	/* Producer thread. */
	while (true) {
		struct conn_info *sbuffer = Malloc(sizeof(struct conn_info));
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Pthread_mutex_unlock(&mutex); 

		/* Wait on full condition variable. */
		while (!(share_cnt < SBUFSIZE)) {
			Pthread_cond_wait(&cond_full, &mutex);
		}

		sbuffer->connfd = connfd;
		sbuffer->clientaddr = clientaddr;
		
		/* Store key in shared buffer. */ 
		shared_buffer[prod_index] = sbuffer;

		/* Signal empty condition variable. */
		Pthread_cond_signal(&cond_empty);

		/* Update shared count variable. */
		share_cnt++;

		/* Update producer index. */
		if (prod_index == SBUFSIZE - 1)
			prod_index = 0;
		else
			prod_index++;

		/* Release mutex lock. */
		Pthread_mutex_unlock(&mutex); 

	}
	/* Clean up. */
	Pthread_mutex_destroy(&mutex);
	
	/* Destroy condition variables. */
	pthread_cond_destroy(&cond_empty);
	pthread_cond_destroy(&cond_full);

	/* Close log file. */
	Fclose(logfd);
	return (0);
}

/* 
 * Requires:
 *   Nothing. 
 *
 * Effects:
 *   Consumer thread executes this function.
 */
void *
consumer(void *arg)
{
	struct conn_info *key;
	Pthread_detach(pthread_self());

	 /* Cast to eliminate warnings. */
        (void)arg;

	while (true) {
		/* Wait on empty condition variable. */
		while (!(share_cnt > 0)) {
			// Acquire mutex lock.
			Pthread_cond_wait(&cond_empty, &mutex);
		}

		/* Read key from shared buffer. */ 
		key = shared_buffer[cons_index];
		
		/* Signal full condition variable. */
		Pthread_cond_signal(&cond_full);

		/* Update shared count variable. */
		share_cnt--;

		/* Update consumer index. */
		if (cons_index == SBUFSIZE - 1)
			cons_index = 0;
		else
			cons_index++;

		/* Release mutex lock. */
		Pthread_mutex_unlock(&mutex);

		doit(key->connfd, &key->clientaddr);

		Close(key->connfd);
	}
	return (NULL);
}
/* 
 * Requires:
 *   Nothing. 
 *
 * Effects:
 *   Processes the client's request, and edits it to be appropriate
 *   for the end server. Then, doit delegates the rest of the work to
 *   open_client. 
 */
void
doit(int fd, struct sockaddr_in *clientaddr) 
{
	/* Initialization. */
	char method[MAXLINE], version[MAXLINE], uri[MAXLINE];
	char buf_server[MAXLINE], response[MAXLINE], read_line[MAXLINE];
	char *hostname, *port, *pathname, *buf;
	char *entry;
	rio_t rio, rio_client;
	int client_fd, n, read;
	int uri_len = 0;
	int bytes_forwarded = 0;
	int num = 0;
	int max_copy = MAXLINE;

	/* Allocate space for buf. */
	buf = Malloc(MAXLINE);

	/* Read request line and headers */
	rio_readinitb(&rio, fd);
	
	/* When exceeds MAXLINE, realloc. */
	while ((read = rio_readlineb(&rio, buf + uri_len, 
	    max_copy - uri_len)) > 0) {
		uri_len += read;
		if (uri_len >= max_copy) {
			max_copy *= 2;
			buf = (char *)realloc(buf, max_copy);
		}
		if (strstr(buf, "\r\n")) 
			break;
	}

	/* Fill in the method, URI, and version info from the first header. */
	if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
		client_error(fd, buf, 400, "Bad request",
			"Request formatted in the wrong way.");
		return;
	}

	/* Check that the request is GET and return an error if not. */
	if (strcasecmp(method, "GET")) {
		client_error(fd, method, 501, "Not implemented",
			"Server only sopport GET.");
		return;
	}

	if (strcasecmp(version, "HTTP/1.0") &&
	    strcasecmp(version, "HTTP/1.1")) {
		client_error(fd, (const char *)version, 505, "Not Support", 
		    "Request is neither HTTP/1.0 nor HTTP/1.1."); 
		    return;
	}

	if (parse_uri((const char *)uri, &hostname, &port, &pathname) < 0) {
		client_error(fd, (const char *)uri, 404, "Not found",
		"Parse request failed");
		Free(hostname);
		Free(port);
		Free(pathname);
		return;
	}
	
	sprintf(buf_server, "%s %s %s\r\n", method, pathname, version);
	

	while (((num = rio_readlineb(&rio, read_line, MAXLINE)) > 0)) {
		if (!strcmp(read_line, "\r\n"))
			break;
		if (!strstr(read_line, "Connection") && 
		    !strstr(read_line, "Keep-Alive") && 
		    !strstr(read_line, "Proxy-Connection")) 
			strcat(buf_server, read_line);
	}

	if (strstr(version, "HTTP/1.1")) 
		strcat(buf_server, "Connection: close\r\n");

	strcat(buf_server, "\r\n");
	printf("%s", buf_server);

	client_fd = Open_clientfd(hostname, port);

	if (client_fd < 0) {
		client_error(fd, (const char *)uri, 404, "Not found",
		 "The requested file does not exist");
	}

	rio_readinitb(&rio_client, client_fd);

	if (rio_writen(client_fd, buf_server, strlen(buf_server)) < 0) {
		client_error(client_fd, buf, 400, "Bad request",
			"Malformed Get Request");
		return;
	}

	while ((n = rio_readn(client_fd, response, MAXLINE)) > 0) {

		bytes_forwarded += n;
		if (rio_writen(fd, response, n) != n) {
			client_error(fd, buf, 400, "Bad request",
				"Sending back error.");			
			Free(hostname);
			Free(port);
			Free(pathname);
			return;
		}
	}

	/* Get the log info using the clientaddr stored in our buffer. */
	entry = create_log_entry(clientaddr, uri, bytes_forwarded);
	strcat(entry, "\n");

	/* Write entry to the log file. */
	fwrite(entry, sizeof(char), strlen(entry), logfd);
	fflush(logfd);
	Free(entry);
	return;
}


void 
sig_ignore(int sig) {
    	printf("SIGPIPE ignored %d\n", sig);
    	return;
}

/*
 * Requires:
 *   The parameter "uri" must point to a properly NUL-terminated string.
 *
 * Effects:
 *   Given a URI from an HTTP proxy GET request (i.e., a URL), extract the
 *   host name, port, and path name.  Create strings containing the host name,
 *   port, and path name, and return them through the parameters "hostnamep",
 *   "portp", "pathnamep", respectively.  (The caller must free the memory
 *   storing these strings.)  Return -1 if there are any problems and 0
 *   otherwise.
 */
static int
parse_uri(const char *uri, char **hostnamep, char **portp, char **pathnamep)
{
	const char *pathname_begin, *port_begin, *port_end;

	if (strncasecmp(uri, "http://", 7) != 0)
		return (-1);

	/* Extract the host name. */
	const char *host_begin = uri + 7;
	const char *host_end = strpbrk(host_begin, ":/ \r\n");
	if (host_end == NULL)
		host_end = host_begin + strlen(host_begin);
	int len = host_end - host_begin;
	char *hostname = Malloc(len + 1);
	strncpy(hostname, host_begin, len);
	hostname[len] = '\0';
	*hostnamep = hostname;

	/* Look for a port number.  If none is found, use port 80. */
	if (*host_end == ':') {
		port_begin = host_end + 1;
		port_end = strpbrk(port_begin, "/ \r\n");
		if (port_end == NULL)
			port_end = port_begin + strlen(port_begin);
		len = port_end - port_begin;
	} else {
		port_begin = "80";
		port_end = host_end;
		len = 2;
	}
	char *port = Malloc(len + 1);
	strncpy(port, port_begin, len);
	port[len] = '\0';
	*portp = port;

	/* Extract the path. */
	if (*port_end == '/') {
		pathname_begin = port_end;
		const char *pathname_end = strpbrk(pathname_begin, " \r\n");
		if (pathname_end == NULL)
			pathname_end = pathname_begin + strlen(pathname_begin);
		len = pathname_end - pathname_begin;
	} else {
		pathname_begin = "/";
		len = 1;
	}
	char *pathname = Malloc(len + 1);
	strncpy(pathname, pathname_begin, len);
	pathname[len] = '\0';
	*pathnamep = pathname;

	return (0);
}

/*
 * Requires:
 *   The parameter "sockaddr" must point to a valid sockaddr_in structure.  The
 *   parameter "uri" must point to a properly NUL-terminated string.
 *
 * Effects:
 *   Returns a string containing a properly formatted log entry.  This log
 *   entry is based upon the socket address of the requesting client
 *   ("sockaddr"), the URI from the request ("uri"), and the size in bytes of
 *   the response from the server ("size").
 */
static char *
create_log_entry(const struct sockaddr_in *sockaddr, const char *uri, int size)
{
	struct tm result;

	/*
	 * Create a large enough array of characters to store a log entry.
	 * Although the length of the URI can exceed MAXLINE, the combined
	 * lengths of the other fields and separators cannot.
	 */
	const size_t log_maxlen = MAXLINE + strlen(uri);
	char *const log_str = Malloc(log_maxlen + 1);

	/* Get a formatted time string. */
	time_t now = time(NULL);
	int log_strlen = strftime(log_str, MAXLINE, 
	    "%a %d %b %Y %H:%M:%S %Z: ", localtime_r(&now, &result));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form.
	 */
	Inet_ntop(AF_INET, &sockaddr->sin_addr, &log_str[log_strlen],
	    INET_ADDRSTRLEN);
	log_strlen += strlen(&log_str[log_strlen]);

	/*
	 * Assert that the time and IP address fields occupy less than half of
	 * the space that is reserved for the non-URI fields.
	 */
	assert(log_strlen < MAXLINE / 2);

	/*
	 * Add the URI and response size onto the end of the log entry.
	 */
	snprintf(&log_str[log_strlen], log_maxlen - log_strlen, " %s %d", uri,
	    size);

	return (log_str);
}

/*
 * Requires:
 *   The parameter "fd" must be an open socket that is connected to the client.
 *   The parameters "cause", "short_msg", and "long_msg" must point to properly 
 *   NUL-terminated strings that describe the reason why the HTTP transaction
 *   failed.  The string "short_msg" may not exceed 32 characters in length,
 *   and the string "long_msg" may not exceed 80 characters in length.
 *
 * Effects:
 *   Constructs an HTML page describing the reason why the HTTP transaction
 *   failed, and writes an HTTP/1.0 response containing that page as the
 *   content.  The cause appearing in the HTML page is truncated if the
 *   string "cause" exceeds 2048 characters in length.
 */
static void
client_error(int fd, const char *cause, int err_num, const char *short_msg,
    const char *long_msg)
{
	char body[MAXBUF], headers[MAXBUF], truncated_cause[2049];

	assert(strlen(short_msg) <= 32);
	assert(strlen(long_msg) <= 80);
	/* Ensure that "body" is much larger than "truncated_cause". */
	assert(sizeof(truncated_cause) < MAXBUF / 2);

	/*
	 * Create a truncated "cause" string so that the response body will not
	 * exceed MAXBUF.
	 */
	strncpy(truncated_cause, cause, sizeof(truncated_cause) - 1);
	truncated_cause[sizeof(truncated_cause) - 1] = '\0';

	/* Build the HTTP response body. */
	snprintf(body, MAXBUF,
	    "<html><title>Proxy Error</title><body bgcolor=""ffffff"">\r\n"
	    "%d: %s\r\n"
	    "<p>%s: %s\r\n"
	    "<hr><em>The COMP 321 Web proxy</em>\r\n",
	    err_num, short_msg, long_msg, truncated_cause);

	/* Build the HTTP response headers. */
	snprintf(headers, MAXBUF,
	    "HTTP/1.0 %d %s\r\n"
	    "Content-type: text/html\r\n"
	    "Content-length: %d\r\n"
	    "\r\n",
	    err_num, short_msg, (int)strlen(body));

	/* Write the HTTP response. */
	if (rio_writen(fd, headers, strlen(headers)) != -1)
		rio_writen(fd, body, strlen(body));
}


// Prevent "unused function" and "unused variable" warnings.
static const void *dummy_ref[] = { client_error, create_log_entry, dummy_ref,
    parse_uri };