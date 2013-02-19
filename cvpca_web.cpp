
/* Based on libwebsockets test server example. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <queue>
#include <string>
#include <mutex>
#include <array>

#include "cvpca_web.h"

#include "libwebsockets/lib/libwebsockets.h"

class CvPCA_Server_Impl
{
  public:
    CvPCA_Server_Impl();
    virtual ~CvPCA_Server_Impl();

    bool start(int port);
    void stop();

    std::shared_ptr<std::queue<std::string>> get_queue();

  private:
    std::unique_ptr<std::thread> server_thread;
    std::shared_ptr<std::queue<std::string>> q1, q2;
    bool done;

    std::queue<std::string> send_queue;
    std::shared_ptr<std::queue<std::string>> read_queue;
    std::mutex g_read_queue_mutex;
    std::mutex g_send_queue_mutex;

    enum protocols {
        PROTOCOL_HTTP = 0, /* always first */
        PROTOCOL_PHONEPCA,
        PROTOCOL_COUNT
    };

    struct libwebsocket_protocols protocols[3];

    int callback_phonepca(struct libwebsocket_context *context,
                          struct libwebsocket *wsi,
                          enum libwebsocket_callback_reasons reason,
                          void *user, void *in, size_t len);

    int callback_http(struct libwebsocket_context *context,
                      struct libwebsocket *wsi,
                      enum libwebsocket_callback_reasons reason,
                      void *in, size_t len);

};

class CvPCA_Session
{
  public:
    CvPCA_Session(libwebsocket *_wsi)
        : wsi(_wsi) {}
    ~CvPCA_Session();

  private:
    struct libwebsocket *wsi;
};

/* Handle serving files over HTTP */
int CvPCA_Server_Impl::callback_http(struct libwebsocket_context *context,
                                     struct libwebsocket *wsi,
                                     enum libwebsocket_callback_reasons reason,
                                     void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_HTTP:
		fprintf(stderr, "serving HTTP URI %s\n", (char *)in);

		if (in && strncmp((const char*)in, "/", len) == 0) {
			if (libwebsockets_serve_http_file(context, wsi,
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

int CvPCA_Server_Impl::callback_phonepca(struct libwebsocket_context *context,
                                         struct libwebsocket *wsi,
                                         enum libwebsocket_callback_reasons reason,
                                         void *user, void *in, size_t len)
{
	int rc;

    CvPCA_Session *session = static_cast<CvPCA_Session*>(user);

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		fprintf(stderr, "callback_cj: "
						 "LWS_CALLBACK_ESTABLISHED\n");

        new (session) CvPCA_Session(wsi);

        libwebsocket_callback_on_writable(context, wsi);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:

        if (send_queue.empty()) {
            g_send_queue_mutex.lock();
            g_send_queue_mutex.unlock();
        }

        if (!send_queue.empty()) {
            std::string &s = send_queue.front();

            char buf[s.size() + LWS_SEND_BUFFER_PRE_PADDING];
            memcpy(buf + LWS_SEND_BUFFER_PRE_PADDING,
                   s.c_str(), s.size());

            send_queue.pop();
            g_send_queue_mutex.unlock();

            rc = libwebsocket_write(wsi, (unsigned char*)buf,
                                    strlen((const char *)buf),
                                    LWS_WRITE_TEXT);
            if (rc < 0)
                fprintf(stderr, "ERROR writing to socket (cj)");
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

CvPCA_Server::CvPCA_Server()
    : impl(new CvPCA_Server_Impl)
{
}

CvPCA_Server::~CvPCA_Server()
{
}


CvPCA_Server_Impl::CvPCA_Server_Impl()
    : done(false),
      protocols({
      /* first protocol must always be HTTP handler */
      {
          "http-only",		/* name */
          [](struct libwebsocket_context *context,
             struct libwebsocket *wsi,
             enum libwebsocket_callback_reasons reason,
             void *, void *in, size_t len)->int
          {
              CvPCA_Server_Impl* impl =
                  (CvPCA_Server_Impl*)libwebsocket_context_user(context);
              return impl->callback_http(context, wsi, reason, in, len);
          },
          0,          /* per_session_data_size */
          0,          /* max frame size / rx buffer */
          0, 0,
      },
      {
          "phonepca-protocol",
          [](struct libwebsocket_context *context,
             struct libwebsocket *wsi,
             enum libwebsocket_callback_reasons reason,
             void *user, void *in, size_t len)->int
          {
              CvPCA_Server_Impl* impl =
                  (CvPCA_Server_Impl*)libwebsocket_context_user(context);
              return impl->callback_phonepca(context, wsi, reason,
                                             user, in, len);
          },
          sizeof(CvPCA_Session),
          1024,
          0, 0,
      },
      {
          NULL, NULL, 0, 0, 0, 0  /* End of list */
      }
      })
{
}

CvPCA_Server_Impl::~CvPCA_Server_Impl()
{
    stop();
}

bool CvPCA_Server::start(int port)
{
    return impl->start(port);
}

bool CvPCA_Server_Impl::start(int port)
{
	fprintf(stderr, "phonepca server, port %d\n", port);

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof info);
	info.port = port;

	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = libwebsocket_get_internal_extensions();
#endif
	info.gid = -1;
	info.uid = -1;

    info.user = this;

	libwebsocket_context *context = libwebsocket_create_context(&info);
	if (context == NULL) {
		fprintf(stderr, "libwebsocket init failed\n");
		return true;
	}

    done = false;
    server_thread = std::unique_ptr<std::thread>(new std::thread([=](){
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
    impl->stop();
}

void CvPCA_Server_Impl::stop()
{
    done = true;
    if (server_thread) {
        server_thread->join();
        server_thread = nullptr;
    }
}

std::shared_ptr<std::queue<std::string>> CvPCA_Server::get_queue()
{
    return impl->get_queue();
}

std::shared_ptr<std::queue<std::string>> CvPCA_Server_Impl::get_queue()
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
