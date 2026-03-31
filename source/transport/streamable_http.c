#include "streamable_http.h"

#define INITIAL_REQUEST_SIZE 4096
#define MAX_REQUEST_SIZE (128 * 1024)

// 线程池相关
#define WORKER_COUNT 2
static Thread worker_threads[WORKER_COUNT];
static int worker_client_fd[WORKER_COUNT];
static volatile int worker_busy[WORKER_COUNT];
static int listen_fd = -1;

Result socket_init() {
    struct sockaddr_in addr;
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return -1;
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MCP_PORT);
    
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    
    if (listen(listen_fd, 1) < 0) {
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    
    srand((unsigned int)time(NULL));
    return 0;
}

// 解析 HTTP header，获取指定 key 的值
char *get_header(char *req, char *key) {
    static char val[128];
    val[0] = 0;
    char *p = strstr(req, key);
    if (p) {
        p += strlen(key);
        while (*p == ' ' || *p == ':') ++p;
        int i = 0;
        while (*p && *p != '\r' && *p != '\n' && i < 127) val[i++] = *p++;
        val[i] = 0;
        return val;
    }
    return NULL;
}

// Read full HTTP request. Starts with small buffer, grows if Content-Length requires it.
// *out_buf is set to the buffer (caller must free if != initial_buf).
// Returns total bytes read, or -1 on error.
static int recv_full_request(int fd, char *initial_buf, int initial_size, char **out_buf) {
    char *buf = initial_buf;
    int buf_size = initial_size;
    int total = 0;
    int n;

    *out_buf = buf;

    // Read headers first
    while (total < buf_size - 1) {
        n = recv(fd, buf + total, buf_size - 1 - total, 0);
        if (n <= 0) return total > 0 ? total : -1;
        total += n;
        buf[total] = '\0';

        char *header_end = strstr(buf, "\r\n\r\n");
        if (!header_end) continue;

        // Parse Content-Length
        char *cl = strstr(buf, "Content-Length:");
        if (!cl) cl = strstr(buf, "content-length:");
        if (!cl) return total;

        int content_length = atoi(cl + 15);
        if (content_length <= 0) return total;

        int header_len = (int)(header_end - buf) + 4;
        int need = header_len + content_length;

        // Grow buffer if needed
        if (need >= buf_size) {
            int new_size = need + 1;
            if (new_size > MAX_REQUEST_SIZE) new_size = MAX_REQUEST_SIZE;
            char *new_buf = (char *)malloc(new_size);
            if (!new_buf) {
                // Can't grow, read what fits
                need = buf_size - 1;
            } else {
                memcpy(new_buf, buf, total);
                if (buf != initial_buf) free(buf);
                buf = new_buf;
                buf_size = new_size;
                *out_buf = buf;
            }
            if (need >= buf_size) need = buf_size - 1;
        }

        while (total < need) {
            n = recv(fd, buf + total, need - total, 0);
            if (n <= 0) break;
            total += n;
        }
        buf[total] = '\0';
        return total;
    }
    return total;
}

void worker_func(void* arg) {
    int idx = (int)(intptr_t)arg;
    while (1) {
        if (!worker_busy[idx]) {
            svcSleepThread(1000000ULL); // 1ms
            continue;
        }
        int client_fd = worker_client_fd[idx];
        char small_buf[INITIAL_REQUEST_SIZE];
        char *req = NULL;
        int n = recv_full_request(client_fd, small_buf, sizeof(small_buf), &req);
        if (n > 0) {
            log_info("Received %d bytes from client_fd: %d", n, client_fd);
            if (strncmp(req, "GET /mcp", 8) == 0) {
                char sid_buf[128] = {0};
                char eid_buf[128] = {0};
                char *sid = get_header(req, "Mcp-Session-Id");
                if (sid) strncpy(sid_buf, sid, sizeof(sid_buf) - 1);
                char *eid = get_header(req, "Last-Event-ID");
                if (eid) strncpy(eid_buf, eid, sizeof(eid_buf) - 1);
                if (sid_buf[0]) {
                    add_sse_connection(client_fd, sid_buf, eid_buf[0] ? eid_buf : NULL);
                } else {
                    const char *resp =
                        "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n"
                        "{\"error\":\"Missing Mcp-Session-Id header. Use an MCP client to connect.\"}";
                    send(client_fd, resp, strlen(resp), 0);
                    close(client_fd);
                }
            } else {
                handle_http_request(req, n, client_fd);
                close(client_fd);
            }
        } else {
            close(client_fd);
        }
        if (req != small_buf) free(req);
        log_info("Processed request from client_fd: %d", client_fd);
        worker_busy[idx] = 0;
    }
}

void run(void* arg) {
    // 初始化 worker 线程
    for (int i = 0; i < WORKER_COUNT; ++i) {
        worker_busy[i] = 0;
        worker_client_fd[i] = -1;
        Result rs = threadCreate(&worker_threads[i], worker_func, (void*)(intptr_t)i, NULL, 0x10000, 49, -2);
        if (R_FAILED(rs)) {
            log_error("Failed to create worker thread %d (%x)", i, rs);
            return;
        }
        rs = threadStart(&worker_threads[i]);
        if (R_FAILED(rs)) {
            log_error("Failed to start worker thread %d (%x)", i, rs);
            return;
        }
    }
    while (1) {
        if (listen_fd < 0 && R_FAILED(socket_init())) {
            svcSleepThread(10000000ULL); // 10ms
            continue;
        }
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            listen_fd = -1; // 重置监听套接字
            log_error("Failed to accept client connection");
            svcSleepThread(10000000ULL); // 10ms
            continue;
        }
        log_info("Accepted client connection");
        // 分配给空闲 worker
        int assigned = 0;
        for (int i = 0; i < WORKER_COUNT; ++i) {
            if (!worker_busy[i]) {
                worker_client_fd[i] = client_fd;
                worker_busy[i] = 1;
                assigned = 1;
                break;
            }
        }
        if (!assigned) {
            log_error("No available worker thread, rejecting client_fd: %d", client_fd);
            close(client_fd);
        }
    }
}

Result streamable_http_init() {
    Thread listen_thread;
    Result rs = threadCreate(&listen_thread, run, NULL, NULL, 0x4000, 49, -2);
    if (R_FAILED(rs)) {
        log_error("Failed to create listen thread for streamable_http (%x)", rs);
        return rs;
    }
    rs = threadStart(&listen_thread);
    if (R_FAILED(rs)) {
        log_error("Failed to start listen thread for streamable_http (%x)", rs);
        return rs;
    }
    Thread notification_thread;
    rs = threadCreate(&notification_thread, sse_heartbeat, NULL, NULL, 0x4000, 49, -2);
    if (R_FAILED(rs)) {
        log_error("Failed to create notification thread for streamable_http (%x)", rs);
        return rs;
    }
    rs = threadStart(&notification_thread);
    if (R_FAILED(rs)) {
        log_error("Failed to start notification thread for streamable_http (%x)", rs);
        return rs;
    }
    return 0;
}