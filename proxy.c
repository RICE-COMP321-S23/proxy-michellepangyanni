/*
 * COMP 321 Project 6: Web Proxy
 *
 * This program implements a multithreaded HTTP proxy.
 *
 * <Michelle Pang, yp29; Lily Gao, qg8.>
 */ 

#include <assert.h>

#include "csapp.h"

#define SBUFSIZE 16	// Buffer size we want
#define NUMTHREADS 8	// Number of threads we want

struct conn_info
{
	int connffd;
	struct sockaddr_in clientaddr;
}
static void	client_error(int fd, const char *cause, int err_num, 
		    const char *short_msg, const char *long_msg);
static char    *create_log_entry(const struct sockaddr_in *sockaddr,
		    const char *uri, int size);
static int	parse_uri(const char *uri, char **hostnamep, char **portp,
		    char **pathnamep);
static int 	open_listen(int port);
static int 	open_client(char *hostname, char *port);
static void	doit(int fd);

/* 
 * Requires:
 *   <to be filled in by the student(s)> 
 *
 * Effects:
 *   <to be filled in by the student(s)> 
 */
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
//     int *connfdp;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    // port to listen on is argv[1]
    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
	doit(connfd);
	Close(connfd);
    }
}

/*
 * Requires:
 *   port is an unused TCP port number.
 *
 * Effects:
 *   Opens and returns a listening socket on the specified port.  Returns -1
 *   and sets errno on a Unix error.
 */
static int
open_listen(int port)
{
	struct sockaddr_in serveraddr;
	int listenfd, optval;

	// // Prevent an "unused parameter" warning.  REMOVE THIS STATEMENT!
	// (void)port;
	// Set listenfd to a newly created stream socket.
	// REPLACE THIS.
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
		// return (-1);

	// listenfd = 0;
	// Eliminate "Address already in use" error from bind().
	optval = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
	    (const void *)&optval, sizeof(int)) == -1) {
		return (-1);
	    }
		
	memset(&serveraddr, 0, sizeof(serveraddr));
	/*
	 * Set the IP address in serveraddr to the special ANY IP address, and
	 * set the port to the input port.  Be careful to ensure that the IP
	 * address and port are in network byte order.
	 */
	// FILL THIS IN.
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	// Use bind() to set the address of listenfd to be serveraddr.
	// FILL THIS IN.
	if (bind(listenfd, (const struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
		return (-2);
	/*
	 * Use listen() to ready the socket for accepting connection requests.
	 * Set the backlog to 8.
	 */
	// FILL THIS IN.
	if (listen(listenfd, 8) == -1)
		return (-1);
	
	return (listenfd);
}

/*
 * Requires:
 *   hostname points to a string representing a host name, and port points to
 *   a string representing a TCP port number.
 *
 * Effects:
 *   Opens a TCP connection to the server at <hostname, port>, and returns a
 *   file descriptor ready for reading and writing.  Returns -1 and sets
 *   errno on a Unix error.  Returns -2 on a DNS (getaddrinfo) error.
 */
static int
open_client(char *hostname, char *port)
{
	struct addrinfo *ai, hints, *listp;
	int fd;

	int error;

	// /*
	//  * Prevent "unused parameter/variable" warnings.  REMOVE THESE
	//  * STATEMENTS!
	//  */
	// (void)hostname;
	// (void)port;

	// Initialize the hints that should be passed to getaddrinfo().
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;  // Open a connection ...
	hints.ai_flags = AI_NUMERICSERV;  // ... using a numeric port arg.
	hints.ai_flags |= AI_ADDRCONFIG;  // Recommended for connections

	/*
	 * Use getaddrinfo() to get the server's address list (&listp).  On
	 * failure, return -2.
	 */
	// REPLACE THIS.
	error = getaddrinfo(hostname, port, &hints, &listp);
	if (error != 0) {
		fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n",
				hostname, port, gai_strerror(error));
		return (-2);
	}
	/*
	 * Iterate over the address list for one that can be successfully
	 * connected to.
	 */
	for (ai = listp; ai != NULL; ai = ai->ai_next) {
		/*
		 * Create a new socket using ai's family, socktype, and
		 * protocol, and assign its descriptor to fd.  On failure,
		 * continue to the next ai.
		 */
		// REPLACE THIS.

		if ((fd = socket(ai->ai_family, ai->ai_socktype,
						 ai->ai_protocol)) == -1)
			continue;
		
		/*
		 * Try connecting to the server with ai's addr and addrlen.
		 * Break out of the loop if connect() succeeds.
		 */
		// FILL THIS IN.
		if (connect(fd, ai -> ai_addr, ai -> ai_addrlen) != -1)
			break;

		/*
		 * Connect() failed.  Close the descriptor and continue to
		 * the next ai.
		 */
		if (close(fd) == -1)
			unix_error("close");
	}

	// Clean up.  Avoid memory leaks!
	freeaddrinfo(listp);

	if (ai == NULL) {
		// All connection attempts failed.
		return (-1);
	} else {
		// The last connect succeeded.
		return (fd);
	}
}

// void *
// start_routine(void *vargp)
// {
//         int connfd = *(int *)vargp;

//         Pthread_detach(pthread_self());
//         Free(vargp);
//         doit(connfd);
//         Close(connfd);
//         return (NULL);
// }

/*
 * doit - handle one HTTP request/response transaction
 */
static void
doit(struct conn_info connection) 
{
	struct sockaddr_in *clientaddr;
	char *hostnamep, *portp, *pathnamep, *request_url;
    	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    	char filename[MAXLINE], cgiargs[MAXLINE];
	int fd;
    	rio_t rio;

	// Declare connection.
	fd = connection.connfd; 
	clientaddr = &connection.clientaddr;

	

  

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
	int log_strlen = strftime(log_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z: ",
	    localtime_r(&now, &result));

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
