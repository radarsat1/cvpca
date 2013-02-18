
/* Based on libwebsockets test server example. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <queue>
#include <string>
#include <mutex>

#include "cvpca_web.h"

#include "libwebsockets/lib/libwebsockets.h"

// Global data
int port = 8000;
struct libwebsocket_context *context = 0;

std::queue<std::string> send_queue;
std::shared_ptr<std::queue<std::string>> read_queue;
std::mutex g_read_queue_mutex;

enum my_protocols {
	/* always first */
	PROTOCOL_HTTP = 0,

    PROTOCOL_PHONEPCA,

	/* always last */
	PROTOCOL_COUNT
};

/* Handle serving files over HTTP */
static int callback_http(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		enum libwebsocket_callback_reasons reason, void *user,
							   void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_HTTP:
		fprintf(stderr, "serving HTTP URI %s\n", (char *)in);

		if (in && strcmp((const char*)in, "/") == 0) {
			if (libwebsockets_serve_http_file(wsi,
                                              "./cvpca.html", "text/html"))
				fprintf(stderr, "Failed to send cvpca.html\n");
			break;
		}

	default:
		break;
	}

	return 0;
}

/* phonepca_protocol */

struct per_session_data__cj {
	struct libwebsocket *wsi;
};

static int
callback_phonepca(struct libwebsocket_context *context,
                       struct libwebsocket *wsi,
                       enum libwebsocket_callback_reasons reason,
                       void *user, void *in, size_t len)
{
	int rc;
	struct per_session_data__cj *pss = (struct per_session_data__cj *)user;

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		fprintf(stderr, "callback_cj: "
						 "LWS_CALLBACK_ESTABLISHED\n");
		pss->wsi = wsi;

        // TO DO: initialization

        libwebsocket_callback_on_writable(context, wsi);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:

        if (!send_queue.empty()) {
            std::string &s = send_queue.front();

            char buf[s.size() + LWS_SEND_BUFFER_PRE_PADDING];
            memcpy(buf + LWS_SEND_BUFFER_PRE_PADDING,
                   s.c_str(), s.size());

            rc = libwebsocket_write(wsi, (unsigned char*)buf,
                                    strlen((const char *)buf),
                                    LWS_WRITE_TEXT);
            if (rc < 0)
                fprintf(stderr, "ERROR writing to socket (cj)");

            send_queue.pop();
        }
		break;

	case LWS_CALLBACK_RECEIVE:
        {
            printf("in, len=%d: %s\n", len, (char*)in);
            std::string s((const char*)in, len);
            g_read_queue_mutex.lock();
            read_queue->push(s);
            g_read_queue_mutex.unlock();
        }

	default:
		break;
	}

	return 0;
}


/* list of supported protocols and callbacks */

static struct libwebsocket_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		"http-only",		/* name */
		callback_http,		/* callback */
		0			/* per_session_data_size */
	},
	{
		"phonepca-protocol",
		callback_phonepca,
		sizeof(struct per_session_data__cj)
	},
	{
		NULL, NULL, 0		/* End of list */
	}
};

int run_ws_server()
{
	fprintf(stderr, "phonepca server, port %d\n", port);

	context = libwebsocket_create_context(port, 0, protocols,
                                          libwebsocket_internal_extensions,
                                          0, 0, -1, -1, 0);
	if (context == NULL) {
		fprintf(stderr, "libwebsocket init failed\n");
		return -1;
	}

	/* Using the "no fork" approach */

	while (1) {
		libwebsocket_service(context, 50);
	}

	libwebsocket_context_destroy(context);
}

// int main(int argc, char **argv)
// {
//     int rc = run_ws_server();

//     return rc;
// }

CvPCA_Server::~CvPCA_Server()
{
    stop();
}

bool CvPCA_Server::start(int port)
{
	context = libwebsocket_create_context(port, 0, protocols,
                                          libwebsocket_internal_extensions,
                                          0, 0, -1, -1, 0);
	if (context == NULL) {
		fprintf(stderr, "libwebsocket init failed\n");
		return true;
	}

    done = false;
    server_thread = std::unique_ptr<std::thread>(new std::thread([&](){
                printf("Thread started on %d\n", port);
                while (!done) {
                    libwebsocket_service(context, 50);
                } 
                printf("Thread ended\n");
                libwebsocket_context_destroy(context);
            }));

    return false;
}

void CvPCA_Server::stop()
{
    done = true;
    if (server_thread) {
        server_thread->join();
        server_thread = nullptr;
    }
}

std::shared_ptr<std::queue<std::string>> CvPCA_Server::get_queue()
{
    auto newq = std::shared_ptr<std::queue<std::string>>(
        new std::queue<std::string>());

    auto q = read_queue;

    g_read_queue_mutex.lock();
    if (read_queue == q1) {
        read_queue = q2;
        q1 = newq;
    } else {
        read_queue = q1;
        q2 = newq;
    }
    g_read_queue_mutex.unlock();

    return q;
}
