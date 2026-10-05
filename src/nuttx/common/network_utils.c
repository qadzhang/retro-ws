/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * network_utils.c - curl/wget 网络工具
 *
 * WHAT : curl/wget 网络工具
 * WHY  : HTTP 抓取与文件下载（浏览器/脚本共用）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/common/network_utils.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : socket 直连 HTTP/1.1，分块写文件
 */

#include <nuttx/config.h>
#include <sys/time.h>
#include <nuttx/arch.h>
#include <nuttx/net/net.h>
#include <nuttx/net/tcp.h>
#include <nuttx/net/udp.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_NETUTILS_CURL
#  define CONFIG_NETUTILS_CURL 1
#endif

#ifndef CONFIG_NETUTILS_WGET
#  define CONFIG_NETUTILS_WGET 1
#endif

/* URL 各段最大长度 / max segment lengths for URL parsing */
#define URL_HOST_MAX 256
#define URL_PATH_MAX 256

/*==========================
 *  URL 解析 / URL parsing
 *==========================*/

/*
 * 功能描述 / WHAT:
 *   拆分 "http://host[:port]/path" 为 host/port/path 三段（有界拷贝）
 * WHY : 原实现 strcpy/strncpy 无边界检查，长 URL 直接打爆 256 字节栈缓冲；
 *       且带端口时 path 被截断成 "/"
 * WHO : curl_get / wget_download
 * WHERE: retro-ws/src/nuttx/common/network_utils.c
 * WHEN : 2026-10-04 重写
 * HOW  : 指针扫描 + snprintf("%.*s") 有界拷贝；超长返回 -ENAMETOOLONG
 * 参数 / Params:
 *   url - 完整 URL / full URL
 *   host/host_sz - 主机输出缓冲 / host output buffer
 *   port - 端口输出（默认 80）/ port output (default 80)
 *   path/path_sz - 路径输出（默认 "/"）/ path output (default "/")
 * 返回值 / Return:
 *   OK (0) - 成功 / success
 *   -ENOTSUP - 非 http 协议 / non-http scheme
 *   -ENAMETOOLONG - host 或 path 超出缓冲 / segment overflows buffer
 */
static int url_split(const char *url,
                     char *host, size_t host_sz, int *port,
                     char *path, size_t path_sz)
{
    const char *p;
    const char *slash;
    const char *colon;
    size_t host_len;
    int n;

    if (strncmp(url, "http://", 7) != 0)
        return -ENOTSUP;  /* HTTPS 或其他，暂不支持 / HTTPS unsupported */

    p = url + 7;
    slash = strchr(p, '/');
    colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else {
        host_len = slash ? (size_t)(slash - p) : strlen(p);
        *port = 80;
    }
    if (*port <= 0)
        *port = 80;

    n = snprintf(host, host_sz, "%.*s", (int)host_len, p);
    if (n < 0 || (size_t)n >= host_sz)
        return -ENAMETOOLONG;

    /* path 从首个 '/' 开始完整保留（含查询串）/ keep full path */
    n = snprintf(path, path_sz, "%s", slash ? slash : "/");
    if (n < 0 || (size_t)n >= path_sz)
        return -ENAMETOOLONG;

    return OK;
}

/*==========================
 *  curl - HTTP 客户端
 *==========================*/

#ifdef CONFIG_NETUTILS_CURL

/* curl 全局选项 */
struct curl_global {
    bool initialized;
    uint32_t timeout;      /* 超时（秒）*/
    uint32_t max_redirs;    /* 最大重定向次数 */
    bool follow_location;    /* 自动跟随重定向 */
    bool verbose;           /* 详细输出 */
    char user_agent[128];
    char cookie[512];       /* Cookie 字符串 */
};

static struct curl_global g_curl = {
    .initialized = false,
    .timeout = 30,
    .max_redirs = 5,
    .follow_location = true,
    .verbose = false,
    .user_agent = "ESP32-S3-NuttX/1.0 curl/8.0"
};

/**
 * curl 初始化
 */
int curl_global_init(void)
{
    if (g_curl.initialized)
        return OK;

    memset(&g_curl.cookie, 0, sizeof(g_curl.cookie));
    g_curl.initialized = true;

    syslog(LOG_INFO, "[CURL] Initialized\n");
    return OK;
}

/**
 * HTTP GET 请求
 *
 * @param url    目标 URL
 * @param data   输出缓冲区（NULL则输出到stdout）
 * @param len   输出缓冲区大小
 * @return       实际读取的字节数，负数为错误码
 */
ssize_t curl_get(const char *url, char *data, size_t len)
{
    if (!g_curl.initialized)
        curl_global_init();

    syslog(LOG_INFO, "[CURL] GET %s\n", url);

    /* 解析 URL（有界拆分，防栈溢出）/ bounded split, no stack smash */
    char host[URL_HOST_MAX];
    char path[URL_PATH_MAX];
    int port = 80;

    int uret = url_split(url, host, sizeof(host), &port,
                         path, sizeof(path));
    if (uret == -ENOTSUP) {
        syslog(LOG_ERR, "[CURL] Only HTTP is supported\n");
        return -ENOTSUP;
    }
    if (uret < 0) {
        syslog(LOG_ERR, "[CURL] URL too long\n");
        return uret;
    }

    /* DNS 解析 */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        syslog(LOG_ERR, "[CURL] Cannot resolve host: %s\n", host);
        return -EHOSTUNREACH;
    }

    /* 创建 socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -ENOMEM;
    }

    /* 设置超时 */
    struct timeval tv = {
        .tv_sec = g_curl.timeout,
        .tv_usec = 0
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* 连接 */
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr = *(struct in_addr *)he->h_addr;

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        close(sock);
        return -ECONNREFUSED;
    }

    /* 构造 HTTP 请求 */
    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, port, g_curl.user_agent);

    /* 发送请求 */
    ssize_t sent = send(sock, req, req_len, 0);
    if (sent < 0) {
        close(sock);
        return -EIO;
    }

    /* 接收响应（循环 recv 直到连接关闭，处理超长响应）*/
    char resp[4096] = {0};
    ssize_t total = 0;
    ssize_t n;
    bool header_parsed = false;
    size_t body_offset = 0;  /* data 缓冲区中已写入的偏移 */

    while ((n = recv(sock, resp, sizeof(resp) - 1, 0)) > 0) {
        resp[n] = '\0';

        if (!header_parsed) {
            /* 查找 HTTP 头部结束（\r\n\r\n）*/
            char *body = strstr(resp, "\r\n\r\n");
            if (body) {
                body += 4;
                header_parsed = true;

                /* 计算 body 长度 */
                size_t header_len = body - resp;
                size_t body_len = n - header_len;

                if (data) {
                    size_t copy_len = (body_len < len - 1 - body_offset) ?
                                      body_len : len - 1 - body_offset;
                    memcpy(data + body_offset, body, copy_len);
                    body_offset += copy_len;
                    total = body_offset;
                } else {
                    write(STDOUT_FILENO, body, body_len);
                    total += body_len;
                }
            }
            /* 还没收到完整头部，继续接收 */
        } else {
            /* 头部已解析，后续数据均为 body */
            if (data) {
                size_t copy_len = ((size_t)n < len - 1 - body_offset) ?
                                  (size_t)n : len - 1 - body_offset;
                if (copy_len > 0) {
                    memcpy(data + body_offset, resp, copy_len);
                    body_offset += copy_len;
                    total = body_offset;
                }
            } else {
                write(STDOUT_FILENO, resp, n);
                total += n;
            }
        }
    }

    if (data && total < (ssize_t)len) {
        data[total] = '\0';
    }

    close(sock);

    if (g_curl.verbose) {
        syslog(LOG_INFO, "[CURL] Received %zd bytes\n", total);
    }

    return total;
}

/**
 * curl 下载文件
 */
int curl_download(const char *url, const char *output_path)
{
    syslog(LOG_INFO, "[CURL] Downloading %s -> %s\n", url, output_path);

    /* 打开输出文件 */
    int fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "[CURL] Cannot open output file: %s\n", output_path);
        return -EIO;
    }

    /* 创建临时缓冲区 */
    char *buf = malloc(8192);
    if (!buf) {
        close(fd);
        return -ENOMEM;
    }

    /* 解析 URL 并发送请求 */
    /* TODO: 实现完整的文件下载 */

    /* 简化：假设 data 是小块，直接写入文件 */
    ssize_t written = 0;
    ssize_t n = curl_get(url, buf, 8192);
    if (n > 0) {
        written = write(fd, buf, n);
    }

    free(buf);
    close(fd);

    if (n < 0)
        return n;

    syslog(LOG_INFO, "[CURL] Downloaded %zd bytes to %s\n", written, output_path);
    return written;
}

/**
 * curl 设置选项
 */
int curl_setopt(const char *option, const char *value)
{
    if (strcmp(option, "timeout") == 0) {
        g_curl.timeout = atoi(value);
    } else if (strcmp(option, "user_agent") == 0) {
        strncpy(g_curl.user_agent, value, sizeof(g_curl.user_agent) - 1);
    } else if (strcmp(option, "cookie") == 0) {
        strncpy(g_curl.cookie, value, sizeof(g_curl.cookie) - 1);
    } else if (strcmp(option, "verbose") == 0) {
        g_curl.verbose = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(option, "followlocation") == 0) {
        g_curl.follow_location = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    }

    return OK;
}

/**
 * curl 命令
 */
int cmd_curl(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: curl <url> [-o <output>] [-s] [-v]\n");
        printf("  curl <url>        - 下载并显示到 stdout\n");
        printf("  curl <url> -o f  - 下载到文件\n");
        printf("  curl -s <url>     - 静默模式\n");
        printf("  curl -v <url>     - 详细输出\n");
        return OK;
    }

    bool silent = false;
    const char *url = NULL;
    const char *output = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            g_curl.verbose = true;
        } else if (strcmp(argv[i], "-s") == 0) {
            silent = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (argv[i][0] != '-') {
            url = argv[i];
        }
    }

    if (!url) {
        printf("Usage: curl <url> [-o <output>] [-s] [-v]\n");
        return OK;
    }

    if (output) {
        int ret = curl_download(url, output);
        if (ret < 0)
            printf("Download failed: %d\n", ret);
        else
            printf("Downloaded %d bytes to %s\n", ret, output);
    } else {
        char buf[4096];
        ssize_t n = curl_get(url, buf, sizeof(buf));
        if (n < 0) {
            if (!silent)
                printf("curl failed: %zd\n", n);
        } else {
            write(STDOUT_FILENO, buf, n);
            printf("\n");
        }
    }

    return OK;
}

#else
int cmd_curl(int argc, char **argv) { (void)argc; (void)argv; printf("curl not configured\n"); return OK; }
#endif /* CONFIG_NETUTILS_CURL */

/*==========================
 *  wget - 文件下载
 *==========================*/

#ifdef CONFIG_NETUTILS_WGET

/**
 * wget 下载文件
 *
 * 支持：
 *   - HTTP 下载
 *   - 断点续传（-c）
 *   - 后台下载（待扩展）
 */
int wget_download(const char *url, const char *output, bool continue_download)
{
    syslog(LOG_INFO, "[WGET] Downloading %s -> %s\n", url, output);

    /* 检查是否已存在部分下载 */
    off_t resume_pos = 0;
    int fd = -1;  /* 初始化为 -1，避免未初始化使用 */

    if (continue_download) {
        fd = open(output, O_WRONLY | O_APPEND);
        if (fd >= 0) {
            /* 获取已有文件大小 */
            struct stat st;
            fstat(fd, &st);
            resume_pos = st.st_size;
            syslog(LOG_INFO, "[WGET] Resuming from offset %ld\n", (long)resume_pos);
        }
    }

    if (fd < 0) {
        fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            syslog(LOG_ERR, "[WGET] Cannot open output file: %s\n", output);
            return -EIO;
        }
    }

    /* 分块下载，支持断点续传 Range 请求 */
    char *buf = malloc(65536);
    if (!buf) {
        close(fd);
        return -ENOMEM;
    }

    ssize_t total = 0;

    /* 如果有断点续传偏移量，需要通过自定义 HTTP 请求发送 Range 头 */
    if (resume_pos > 0) {
        /* 解析 URL（有界拆分）/ bounded URL split */
        char host[URL_HOST_MAX];
        char path[URL_PATH_MAX];
        int port = 80;

        int uret = url_split(url, host, sizeof(host), &port,
                             path, sizeof(path));
        if (uret < 0) {
            syslog(LOG_ERR, "[WGET] bad or overlong URL: %s\n", url);
            free(buf);
            close(fd);
            return uret;
        }

        struct hostent *he = gethostbyname(host);
        if (!he) {
            syslog(LOG_ERR, "[WGET] Cannot resolve host: %s\n", host);
            free(buf);
            close(fd);
            return -EHOSTUNREACH;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            free(buf);
            close(fd);
            return -ENOMEM;
        }

        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        dest.sin_addr = *(struct in_addr *)he->h_addr;

        if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            close(sock);
            free(buf);
            close(fd);
            return -ECONNREFUSED;
        }

        /* 发送带 Range 头的 GET 请求 */
        char req[1024];
        int req_len = snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "User-Agent: ESP32-S3-NuttX/1.0\r\n"
            "Range: bytes=%ld-\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host, port, (long)resume_pos);

        send(sock, req, req_len, 0);

        /* 接收响应并写入文件 */
        ssize_t n;
        while ((n = recv(sock, buf, 65536, 0)) > 0) {
            /* 跳过 HTTP 头部 */
            if (total == 0) {
                char *body = strstr(buf, "\r\n\r\n");
                if (body) {
                    body += 4;
                    size_t header_len = body - buf;
                    size_t body_len = n - header_len;
                    ssize_t written = write(fd, body, body_len);
                    if (written < 0) {
                        close(sock);
                        free(buf);
                        close(fd);
                        return -EIO;
                    }
                    total += written;
                }
            } else {
                ssize_t written = write(fd, buf, n);
                if (written < 0) {
                    close(sock);
                    free(buf);
                    close(fd);
                    return -EIO;
                }
                total += written;
            }
        }
        close(sock);
    } else {
        /* 无断点续传，直接调用 curl_get */
        ssize_t n = curl_get(url, buf, 65536);
        if (n > 0) {
            ssize_t written = write(fd, buf, n);
            if (written < 0) {
                syslog(LOG_ERR, "[WGET] Write error\n");
                free(buf);
                close(fd);
                return -EIO;
            }
            total = written;
        }
    }

    free(buf);
    close(fd);

    syslog(LOG_INFO, "[WGET] Download complete: %zd bytes\n", total);
    return total;
}

/**
 * wget 命令
 */
int cmd_wget(int argc, char **argv)
{
    bool continue_download = false;
    const char *url = NULL;
    const char *output = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--continue") == 0) {
            continue_download = true;
        } else if (strcmp(argv[i], "-O") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (argv[i][0] != '-') {
            url = argv[i];
        }
    }

    if (!url) {
        printf("用法: wget <url> [-O <output>] [-c]\n");
        printf("  wget <url>     - 下载到当前目录（文件名取自 URL）\n");
        printf("  wget <url> -O f - 下载到指定文件\n");
        printf("  wget <url> -c  - 断点续传\n");
        return OK;
    }

    /* 如果没有指定输出文件名，从 URL 提取 */
    if (!output) {
        /* 从 URL 中提取文件名 */
        const char *p = strrchr(url, '/');
        if (p && strlen(p) > 1) {
            output = p + 1;
        } else {
            output = "index.html";
        }
    }

    int ret = wget_download(url, output, continue_download);
    if (ret < 0) {
        printf("wget failed: %d\n", ret);
    } else {
        printf("Downloaded %d bytes to %s\n", ret, output);
    }

    return OK;
}

#else
int cmd_wget(int argc, char **argv) { (void)argc; (void)argv; printf("wget not configured\n"); return OK; }
#endif /* CONFIG_NETUTILS_WGET */
