#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <sys/wait.h>
#include "workflow/HttpMessage.h"
#include "workflow/HttpUtil.h"
#include "workflow/WFHttpServer.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"
#include "workflow/WFFacilities.h"

using namespace protocol;

// Global stream state (will be controlled by API)
static std::atomic<bool> g_stream_running{false};
static std::atomic<bool> g_stream_enabled{false};
static std::string g_rtsp_url;
static pid_t g_stream_pid = -1;

static const std::unordered_map<std::string, std::string> MIME_TYPES = {
    {".html", "text/html"},        {".htm", "text/html"},
    {".css",  "text/css"},         {".js",  "application/javascript"},
    {".json", "application/json"}, {".xml", "application/xml"},
    {".txt",  "text/plain"},       {".png", "image/png"},
    {".jpg",  "image/jpeg"},       {".jpeg","image/jpeg"},
    {".gif",  "image/gif"},        {".svg", "image/svg+xml"},
    {".ico",  "image/x-icon"},     {".webp","image/webp"},
    {".mp4",  "video/mp4"},        {".webm","video/webm"},
    {".mp3",  "audio/mpeg"},       {".wav", "audio/wav"},
    {".pdf",  "application/pdf"},  {".zip", "application/zip"},
};

static std::string get_mime_type(const std::string &path) {
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        auto it = MIME_TYPES.find(path.substr(dot));
        if (it != MIME_TYPES.end())
            return it->second;
    }
    return "application/octet-stream";
}

using Handler = std::function<void(WFHttpTask *, HttpRequest *, HttpResponse *)>;

struct Route {
    std::string method;
    std::string path;
    Handler handler;
};

class HttpDemoServer {
public:
    HttpDemoServer(unsigned short port, const std::string &doc_root)
        : port_(port), doc_root_(doc_root) {}

    void Get(const std::string &path, Handler handler) {
        routes_.push_back({"GET", path, std::move(handler)});
    }

    void Post(const std::string &path, Handler handler) {
        routes_.push_back({"POST", path, std::move(handler)});
    }

    int start() {
        auto proc = [this](WFHttpTask *task) { this->dispatch(task); };
        server_ = new WFHttpServer(proc);
        if (server_->start(port_) < 0) {
            perror("Cannot start server");
            return -1;
        }
        std::cerr << "HTTP server started on port " << port_
                  << ", doc_root: " << doc_root_ << std::endl;
        return 0;
    }

    void stop() {
        if (server_) {
            server_->stop();
            delete server_;
            server_ = nullptr;
        }
    }

private:
    void serve_file(WFHttpTask *task, const std::string &abs_path) {
        HttpResponse *resp = task->get_resp();
        struct stat st;
        if (stat(abs_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            resp->set_status_code("404");
            resp->set_reason_phrase("Not Found");
            resp->add_header_pair("Content-Type", "text/html");
            resp->append_output_body("<h1>404 Not Found</h1>");
            return;
        }

        int fd = open(abs_path.c_str(), O_RDONLY);
        if (fd < 0) {
            resp->set_status_code("403");
            resp->set_reason_phrase("Forbidden");
            resp->add_header_pair("Content-Type", "text/html");
            resp->append_output_body("<h1>403 Forbidden</h1>");
            return;
        }

        size_t size = st.st_size;
        void *buf = malloc(size);
        if (!buf) {
            close(fd);
            resp->set_status_code("500");
            resp->set_reason_phrase("Internal Server Error");
            return;
        }

        resp->set_status_code("200");
        resp->set_reason_phrase("OK");
        resp->add_header_pair("Content-Type", get_mime_type(abs_path));
        resp->add_header_pair("Content-Length", std::to_string(size));

        WFFileIOTask *pread_task = WFTaskFactory::create_pread_task(
            fd, buf, size, 0,
            [](WFFileIOTask *task) {
                FileIOArgs *args = task->get_args();
                long ret = task->get_retval();
                HttpResponse *resp = (HttpResponse *)task->user_data;
                close(args->fd);
                if (task->get_state() != WFT_STATE_SUCCESS || ret < 0) {
                    resp->set_status_code("500");
                    resp->set_reason_phrase("Internal Server Error");
                } else {
                    resp->append_output_body_nocopy(args->buf, ret);
                }
            }
        );
        pread_task->user_data = resp;
        task->set_callback([buf](WFHttpTask *) { free(buf); });
        series_of(task)->push_back(pread_task);
    }

    void dispatch(WFHttpTask *task) {
        HttpRequest *req = task->get_req();
        HttpResponse *resp = task->get_resp();

        std::string method = req->get_method();
        std::string uri = req->get_request_uri();
        std::string path = uri.substr(0, uri.find('?'));

        resp->set_http_version("HTTP/1.1");
        resp->add_header_pair("Server", "MediaTor-HttpDemo");

        // 1. API routes first
        for (auto &route : routes_) {
            if (route.method == method && route.path == path) {
                resp->add_header_pair("Content-Type", "application/json");
                route.handler(task, req, resp);
                return;
            }
        }

        // 2. Static file serving for GET
        if (method != "GET") {
            resp->set_status_code("405");
            resp->set_reason_phrase("Method Not Allowed");
            resp->add_header_pair("Content-Type", "application/json");
            resp->append_output_body(R"({"error":"Method Not Allowed"})");
            return;
        }

        std::string abs_path = doc_root_ + path;

        // If directory, try index.html
        struct stat st;
        if (stat(abs_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            if (abs_path.back() != '/') abs_path += '/';
            abs_path += "index.html";
        }

        serve_file(task, abs_path);
    }

    unsigned short port_;
    std::string doc_root_;
    WFHttpServer *server_ = nullptr;
    std::vector<Route> routes_;
};

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo) {
    wait_group.done();
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "USAGE: %s <port> [doc_root]\n", argv[0]);
        fprintf(stderr, "  doc_root: static files directory (default: ./www)\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    unsigned short port = atoi(argv[1]);
    std::string doc_root = (argc >= 3) ? argv[2] : "./www";

    HttpDemoServer server(port, doc_root);

    // ---------- Stream control routes ----------

    server.Get("/api/stream/status", [](WFHttpTask *, HttpRequest *, HttpResponse *resp) {
        // Check if child process is still alive
        if (g_stream_pid > 0) {
            int status;
            pid_t ret = waitpid(g_stream_pid, &status, WNOHANG);
            if (ret != 0) {
                // Child has exited
                g_stream_pid = -1;
                g_stream_running = false;
                g_stream_enabled = false;
            }
        }

        resp->set_status_code("200");
        resp->set_reason_phrase("OK");
        std::string body = R"({"running":)" + std::string(g_stream_running ? "true" : "false") +
                           R"(,"enabled":)" + std::string(g_stream_enabled ? "true" : "false") +
                           R"(,"pid":)" + std::to_string(g_stream_pid) +
                           R"(,"rtsp_url":")" + g_rtsp_url + R"("})";

        std::cout << "Stream status requested: " << body << std::endl;
        resp->append_output_body(body);
    });

    server.Post("/api/stream/start", [](WFHttpTask *, HttpRequest *req, HttpResponse *resp) {
        // Optionally accept rtsp_url from body
        const void *body = nullptr;
        size_t size = 0;
        req->get_parsed_body(&body, &size);

        if (body && size > 0) {
            std::string data(static_cast<const char *>(body), size);
            auto pos = data.find("\"rtsp_url\"");
            if (pos != std::string::npos) {
                auto colon = data.find(':', pos);
                auto q1 = data.find('"', colon + 1);
                auto q2 = data.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    g_rtsp_url = data.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }

        if (g_stream_running || g_stream_enabled) {
            resp->set_status_code("409");
            resp->set_reason_phrase("Conflict");
            resp->append_output_body(R"({"error":"Stream already running"})");
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            resp->set_status_code("500");
            resp->set_reason_phrase("Internal Server Error");
            resp->append_output_body(R"({"error":"fork failed"})");
            return;
        }

        if (pid == 0) {
            // Child process: exec v4l2_rkmpp_enc
            execl("./run_v4l2_rkmpp_enc.sh", "run_v4l2_rkmpp_enc.sh",
                  "/dev/video21", "stream.h264", "30000", "h264",
                  g_rtsp_url.c_str(), nullptr);
            // If execl returns, it failed
            _exit(1);
        }

        // Parent process
        g_stream_pid = pid;
        g_stream_enabled = true;
        g_stream_running = true;
        std::cout << "Stream started, pid=" << pid
                  << ", rtsp_url=" << g_rtsp_url << std::endl;
        resp->set_status_code("200");
        resp->set_reason_phrase("OK");
        resp->append_output_body(R"({"status":"started","pid":)" + std::to_string(pid) +
                                 R"(,"rtsp_url":")" + g_rtsp_url + R"("})");
    });

    server.Post("/api/stream/stop", [](WFHttpTask *, HttpRequest *, HttpResponse *resp) {
        if (g_stream_pid > 0) {
            kill(g_stream_pid, SIGINT);
            // Wait briefly for clean shutdown
            int status;
            waitpid(g_stream_pid, &status, 0);
            std::cout << "Stream stopped, pid=" << g_stream_pid << std::endl;
            g_stream_pid = -1;
        }
        g_stream_enabled = false;
        g_stream_running = false;
        resp->set_status_code("200");
        resp->set_reason_phrase("OK");
        resp->append_output_body(R"({"status":"stopped"})");
    });

    if (server.start() < 0)
        return 1;

    wait_group.wait();
    server.stop();
    return 0;
}
