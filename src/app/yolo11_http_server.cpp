#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>

#include "workflow/HttpMessage.h"
#include "workflow/HttpUtil.h"
#include "workflow/WFHttpServer.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"
#include "workflow/WFFacilities.h"

#include "yolo11.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

using namespace protocol;

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char *data, size_t len)
{
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len)
            v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len)
            v |= (uint32_t)data[i + 2];
        out.push_back(b64_table[(v >> 18) & 0x3F]);
        out.push_back(b64_table[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? b64_table[v & 0x3F] : '=');
    }
    return out;
}

static long get_current_time_us()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

static const std::unordered_map<std::string, std::string> MIME_TYPES = {
    {".html", "text/html"},   {".htm", "text/html"},
    {".css",  "text/css"},    {".js",  "application/javascript"},
    {".json", "application/json"}, {".xml", "application/xml"},
    {".txt",  "text/plain"},  {".png", "image/png"},
    {".jpg",  "image/jpeg"},  {".jpeg","image/jpeg"},
    {".gif",  "image/gif"},   {".svg", "image/svg+xml"},
    {".ico",  "image/x-icon"},{".webp","image/webp"},
};

static std::string get_mime_type(const std::string &path)
{
    auto dot = path.rfind('.');
    if (dot != std::string::npos)
    {
        auto it = MIME_TYPES.find(path.substr(dot));
        if (it != MIME_TYPES.end())
            return it->second;
    }
    return "application/octet-stream";
}

static rknn_app_context_t g_rknn_ctx;
static std::mutex g_rknn_mutex;
static bool g_model_loaded = false;

using Handler = std::function<void(WFHttpTask *, HttpRequest *, HttpResponse *)>;

struct Route
{
    std::string method;
    std::string path;
    Handler handler;
};

class YoloHttpServer
{
public:
    YoloHttpServer(unsigned short port, const std::string &doc_root)
        : port_(port), doc_root_(doc_root) {}

    void Get(const std::string &path, Handler handler)
    {
        routes_.push_back({"GET", path, std::move(handler)});
    }

    void Post(const std::string &path, Handler handler)
    {
        routes_.push_back({"POST", path, std::move(handler)});
    }

    int start()
    {
        auto proc = [this](WFHttpTask *task) { this->dispatch(task); };
        server_ = new WFHttpServer(proc);
        if (server_->start(port_) < 0)
        {
            perror("Cannot start server");
            return -1;
        }
        std::cerr << "YOLO11 HTTP server started on port " << port_
                  << ", doc_root: " << doc_root_ << std::endl;
        return 0;
    }

    void stop()
    {
        if (server_)
        {
            server_->stop();
            delete server_;
            server_ = nullptr;
        }
    }

private:
    void serve_file(WFHttpTask *task, const std::string &abs_path)
    {
        HttpResponse *resp = task->get_resp();
        struct stat st;
        if (stat(abs_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        {
            resp->set_status_code("404");
            resp->set_reason_phrase("Not Found");
            resp->add_header_pair("Content-Type", "text/html");
            resp->append_output_body("<h1>404 Not Found</h1>");
            return;
        }

        int fd = open(abs_path.c_str(), O_RDONLY);
        if (fd < 0)
        {
            resp->set_status_code("403");
            resp->set_reason_phrase("Forbidden");
            resp->add_header_pair("Content-Type", "text/html");
            resp->append_output_body("<h1>403 Forbidden</h1>");
            return;
        }

        size_t size = st.st_size;
        void *buf = malloc(size);
        if (!buf)
        {
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
                if (task->get_state() != WFT_STATE_SUCCESS || ret < 0)
                {
                    resp->set_status_code("500");
                    resp->set_reason_phrase("Internal Server Error");
                }
                else
                {
                    resp->append_output_body_nocopy(args->buf, ret);
                }
            });
        pread_task->user_data = resp;
        task->set_callback([buf](WFHttpTask *) { free(buf); });
        series_of(task)->push_back(pread_task);
    }

    void dispatch(WFHttpTask *task)
    {
        HttpRequest *req = task->get_req();
        HttpResponse *resp = task->get_resp();

        std::string method = req->get_method();
        std::string uri = req->get_request_uri();
        std::string path = uri.substr(0, uri.find('?'));

        resp->set_http_version("HTTP/1.1");
        resp->add_header_pair("Server", "MediaTor-YOLO11");
        resp->add_header_pair("Access-Control-Allow-Origin", "*");
        resp->add_header_pair("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        resp->add_header_pair("Access-Control-Allow-Headers", "Content-Type");

        if (method == "OPTIONS")
        {
            resp->set_status_code("204");
            resp->set_reason_phrase("No Content");
            return;
        }

        for (auto &route : routes_)
        {
            if (route.method == method && route.path == path)
            {
                resp->add_header_pair("Content-Type", "application/json");
                route.handler(task, req, resp);
                return;
            }
        }

        if (method != "GET")
        {
            resp->set_status_code("405");
            resp->set_reason_phrase("Method Not Allowed");
            resp->add_header_pair("Content-Type", "application/json");
            resp->append_output_body(R"({"error":"Method Not Allowed"})");
            return;
        }

        std::string abs_path = doc_root_ + path;
        struct stat st;
        if (stat(abs_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        {
            if (abs_path.back() != '/')
                abs_path += '/';
            abs_path += "yolo11.html";
        }

        serve_file(task, abs_path);
    }

    unsigned short port_;
    std::string doc_root_;
    WFHttpServer *server_ = nullptr;
    std::vector<Route> routes_;
};

static void handle_detect(WFHttpTask *, HttpRequest *req, HttpResponse *resp)
{
    if (!g_model_loaded)
    {
        resp->set_status_code("503");
        resp->set_reason_phrase("Service Unavailable");
        resp->append_output_body(R"({"success":false,"error":"Model not loaded"})");
        return;
    }

    const void *body = nullptr;
    size_t body_size = 0;
    req->get_parsed_body(&body, &body_size);

    if (!body || body_size == 0)
    {
        resp->set_status_code("400");
        resp->set_reason_phrase("Bad Request");
        resp->append_output_body(R"({"success":false,"error":"Empty body"})");
        return;
    }

    std::lock_guard<std::mutex> lock(g_rknn_mutex);

    const char *input_path = "/tmp/yolo11_http_input.jpg";
    const char *output_path = "/tmp/yolo11_http_output.jpg";

    FILE *f = fopen(input_path, "wb");
    if (!f)
    {
        resp->set_status_code("500");
        resp->set_reason_phrase("Internal Server Error");
        resp->append_output_body(R"({"success":false,"error":"Cannot write temp file"})");
        return;
    }
    fwrite(body, 1, body_size, f);
    fclose(f);

    image_buffer_t src_image;
    memset(&src_image, 0, sizeof(src_image));
    int ret = read_image(input_path, &src_image);
    if (ret != 0)
    {
        resp->set_status_code("400");
        resp->set_reason_phrase("Bad Request");
        resp->append_output_body(R"({"success":false,"error":"Cannot read image"})");
        unlink(input_path);
        return;
    }

    int img_width = src_image.width;
    int img_height = src_image.height;

    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));

    long infer_start = get_current_time_us();
    ret = inference_yolo11_model(&g_rknn_ctx, &src_image, &od_results);
    long infer_end = get_current_time_us();
    float infer_ms = (infer_end - infer_start) / 1000.0;

    if (ret != 0)
    {
        resp->set_status_code("500");
        resp->set_reason_phrase("Internal Server Error");
        resp->append_output_body(R"({"success":false,"error":"Inference failed"})");
        if (src_image.virt_addr)
            free(src_image.virt_addr);
        unlink(input_path);
        return;
    }

    char text[256];
    for (int i = 0; i < od_results.count; i++)
    {
        object_detect_result *det = &(od_results.results[i]);
        int x1 = det->box.left;
        int y1 = det->box.top;
        int x2 = det->box.right;
        int y2 = det->box.bottom;
        sprintf(text, "%s %.1f%%", coco_cls_to_name(det->cls_id), det->prop * 100);
        draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_RED, 3);
        draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
    }

    ret = write_image(output_path, &src_image);
    if (src_image.virt_addr)
        free(src_image.virt_addr);
    unlink(input_path);

    if (ret != 0)
    {
        resp->set_status_code("500");
        resp->set_reason_phrase("Internal Server Error");
        resp->append_output_body(R"({"success":false,"error":"Cannot write output image"})");
        return;
    }

    FILE *of = fopen(output_path, "rb");
    if (!of)
    {
        resp->set_status_code("500");
        resp->set_reason_phrase("Internal Server Error");
        resp->append_output_body(R"({"success":false,"error":"Cannot read output image"})");
        unlink(output_path);
        return;
    }

    fseek(of, 0, SEEK_END);
    size_t out_size = ftell(of);
    fseek(of, 0, SEEK_SET);
    std::vector<unsigned char> out_buf(out_size);
    fread(out_buf.data(), 1, out_size, of);
    fclose(of);
    unlink(output_path);

    std::string b64 = base64_encode(out_buf.data(), out_size);

    std::string json = "{\"success\":true,";
    json += "\"inference_ms\":" + std::to_string(infer_ms) + ",";
    json += "\"width\":" + std::to_string(img_width) + ",";
    json += "\"height\":" + std::to_string(img_height) + ",";
    json += "\"count\":" + std::to_string(od_results.count) + ",";
    json += "\"objects\":[";
    for (int i = 0; i < od_results.count; i++)
    {
        if (i > 0)
            json += ",";
        auto &r = od_results.results[i];
        char obj[512];
        sprintf(obj,
                "{\"cls\":\"%s\",\"conf\":%.4f,\"left\":%d,\"top\":%d,\"right\":%d,\"bottom\":%d}",
                coco_cls_to_name(r.cls_id), r.prop,
                r.box.left, r.box.top, r.box.right, r.box.bottom);
        json += obj;
    }
    json += "],";
    json += "\"image\":\"data:image/jpeg;base64," + b64 + "\"}";

    resp->set_status_code("200");
    resp->set_reason_phrase("OK");
    resp->append_output_body(json);
}

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
    wait_group.done();
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 4)
    {
        fprintf(stderr, "USAGE: %s <model_path> [port] [doc_root]\n", argv[0]);
        fprintf(stderr, "  model_path: RKNN model file path\n");
        fprintf(stderr, "  port:       HTTP port (default: 8080)\n");
        fprintf(stderr, "  doc_root:   static files directory (default: ./www)\n");
        return 1;
    }

    const char *model_path = argv[1];
    unsigned short port = (argc >= 3) ? atoi(argv[2]) : 8080;
    std::string doc_root = (argc >= 4) ? argv[3] : "./www";

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    memset(&g_rknn_ctx, 0, sizeof(g_rknn_ctx));
    init_post_process();

    int ret = init_yolo11_model(model_path, &g_rknn_ctx);
    if (ret != 0)
    {
        fprintf(stderr, "Failed to init YOLO11 model: %s\n", model_path);
        deinit_post_process();
        return 1;
    }
    g_model_loaded = true;
    printf("YOLO11 model loaded: %s (%dx%d)\n",
           model_path, g_rknn_ctx.model_width, g_rknn_ctx.model_height);

    YoloHttpServer server(port, doc_root);

    server.Post("/api/detect", handle_detect);

    server.Get("/api/model/status", [](WFHttpTask *, HttpRequest *, HttpResponse *resp) {
        resp->set_status_code("200");
        resp->set_reason_phrase("OK");
        std::string body = R"({"loaded":)" + std::string(g_model_loaded ? "true" : "false") +
                           R"(,"width":)" + std::to_string(g_rknn_ctx.model_width) +
                           R"(,"height":)" + std::to_string(g_rknn_ctx.model_height) +
                           R"(,"channel":)" + std::to_string(g_rknn_ctx.model_channel) + "}";
        resp->append_output_body(body);
    });

    if (server.start() < 0)
    {
        deinit_post_process();
        release_yolo11_model(&g_rknn_ctx);
        return 1;
    }

    wait_group.wait();

    server.stop();
    deinit_post_process();
    release_yolo11_model(&g_rknn_ctx);
    printf("Server stopped.\n");

    return 0;
}