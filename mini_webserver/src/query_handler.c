/*
 * W3D3 query_handler.c — 动态查询处理器实现
 *
 * 功能：
 *   1. URL 解码（%XX 和 + → 空格）
 *   2. 查询字符串解析（class=2011&keyword=男）
 *   3. HTML 转义（防止注入）
 *   4. 参数校验（class: 4位数字, keyword: 1-64字节安全字符）
 *   5. 数据文件查询（data/<class>.txt 中 grep keyword）
 *   6. 动态 HTML 生成（搜索表单 / 查询结果 / 错误页）
 *
 * 对照 W3D3 知识点：
 *   - GET 参数在 URL 查询串，POST 参数在请求体
 *   - URL 解码 + 参数校验 + HTML 输出转义 三阶段处理
 *   - 数据文件路径映射：class → data/<class>.txt
 *   - 动态 HTML 先生成正文再计算 Content-Length
 *   - send_all() 统一发送
 */

#include "query_handler.h"
#include "static_handler.h"   /* send_all() */
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ===== URL 解码 ====================================================== */

static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int url_decode(const char *src, char *dst, int dst_size)
{
    int di = 0;
    const char *si;

    if (src == NULL || dst == NULL || dst_size <= 0) {
        return -1;
    }

    for (si = src; *si != '\0' && di < dst_size - 1; si++) {
        if (*si == '%') {
            /* %XX 编码 */
            int hi, lo;
            if (si[1] == '\0' || si[2] == '\0') {
                return -1;  /* 截断的 % 序列 */
            }
            hi = hex_to_int(si[1]);
            lo = hex_to_int(si[2]);
            if (hi < 0 || lo < 0) {
                return -1;  /* 非法的十六进制字符 */
            }
            dst[di++] = (char)((hi << 4) | lo);
            si += 2;
        } else if (*si == '+') {
            /* + → 空格（表单编码约定） */
            dst[di++] = ' ';
        } else {
            dst[di++] = *si;
        }
    }
    dst[di] = '\0';
    return 0;
}

/* ===== 查询字符串解析 ================================================ */

int parse_query_string(const char *query,
                       char *class_buf, int class_size,
                       char *keyword_buf, int keyword_size)
{
    const char *p;
    int found_class   = 0;
    int found_keyword = 0;

    if (query == NULL || class_buf == NULL || keyword_buf == NULL) {
        return -1;
    }

    /* 初始化输出 */
    class_buf[0]   = '\0';
    keyword_buf[0] = '\0';

    if (*query == '\0') {
        return -1;  /* 空查询字符串 */
    }

    p = query;
    while (*p != '\0') {
        const char *key_start, *key_end;
        const char *val_start, *val_end;
        char key[64], raw_val[512];
        char decoded_val[512];
        int key_len, val_len;

        /* 提取 key */
        key_start = p;
        while (*p != '\0' && *p != '=' && *p != '&') p++;
        key_end = p;
        key_len = (int)(key_end - key_start);
        if (key_len <= 0 || key_len >= (int)sizeof(key)) {
            /* 跳过此参数 */
            if (*p == '=') p++;
            while (*p != '\0' && *p != '&') p++;
            if (*p == '&') p++;
            continue;
        }
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';

        /* 提取 value（如果有） */
        if (*p == '=') {
            p++;  /* 跳过 = */
            val_start = p;
            while (*p != '\0' && *p != '&') p++;
            val_end = p;
            val_len = (int)(val_end - val_start);
            if (val_len >= (int)sizeof(raw_val)) {
                val_len = (int)sizeof(raw_val) - 1;
            }
            memcpy(raw_val, val_start, val_len);
            raw_val[val_len] = '\0';
        } else {
            raw_val[0] = '\0';
        }

        /* 跳过 & */
        if (*p == '&') p++;

        /* URL 解码 value */
        if (url_decode(raw_val, decoded_val, sizeof(decoded_val)) != 0) {
            continue;  /* 解码失败，跳过 */
        }

        /* 匹配参数名 */
        if (strcmp(key, "class") == 0) {
            if (strlen(decoded_val) < (size_t)class_size) {
                strncpy(class_buf, decoded_val, class_size - 1);
                class_buf[class_size - 1] = '\0';
                found_class = 1;
            }
        } else if (strcmp(key, "keyword") == 0) {
            if (strlen(decoded_val) < (size_t)keyword_size) {
                strncpy(keyword_buf, decoded_val, keyword_size - 1);
                keyword_buf[keyword_size - 1] = '\0';
                found_keyword = 1;
            }
        }
    }

    /* class 和 keyword 均为可选，至少需要一个 */
    if (!found_class && !found_keyword) {
        return -1;
    }

    return 0;
}

/* ===== HTML 转义 ====================================================== */

void html_escape(const char *src, char *dst, int dst_size)
{
    int di = 0;
    const char *si;

    if (src == NULL || dst == NULL || dst_size <= 0) {
        return;
    }

    for (si = src; *si != '\0' && di < dst_size - 8; si++) {
        switch (*si) {
        case '&':
            if (di + 5 < dst_size) {
                memcpy(dst + di, "&amp;", 5);
                di += 5;
            }
            break;
        case '<':
            if (di + 4 < dst_size) {
                memcpy(dst + di, "&lt;", 4);
                di += 4;
            }
            break;
        case '>':
            if (di + 4 < dst_size) {
                memcpy(dst + di, "&gt;", 4);
                di += 4;
            }
            break;
        case '"':
            if (di + 6 < dst_size) {
                memcpy(dst + di, "&quot;", 6);
                di += 6;
            }
            break;
        case '\'':
            if (di + 5 < dst_size) {
                memcpy(dst + di, "&#39;", 5);
                di += 5;
            }
            break;
        default:
            dst[di++] = *si;
            break;
        }
    }
    dst[di] = '\0';
}

/* ===== 参数校验 ====================================================== */

int validate_class(const char *class_str)
{
    int len;

    if (class_str == NULL) return 0;

    len = (int)strlen(class_str);
    if (len != CLASS_DIGITS) return 0;

    for (int i = 0; i < len; i++) {
        if (class_str[i] < '0' || class_str[i] > '9') {
            return 0;
        }
    }

    return 1;
}

int validate_keyword(const char *keyword)
{
    int len, i;

    if (keyword == NULL) return 0;

    len = (int)strlen(keyword);
    if (len < 1 || len > MAX_KEYWORD_LEN) return 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)keyword[i];

        /* 拒绝空字节 */
        if (c == 0x00) return 0;

        /* 拒绝控制字符（0x01-0x1F，除了 TAB） */
        if (c < 0x20 && c != '\t') return 0;

        /* 拒绝路径穿越字符 */
        if (c == '/' || c == '\\') return 0;
    }

    /* 拒绝 .. */
    if (strstr(keyword, "..") != NULL) return 0;

    /* UTF-8 校验：允许 0x80-0xFF 的多字节序列头部 */
    /* 只做基本检查，不完整验证 UTF-8 序列 */

    return 1;
}

/* ===== 数据文件查询 ================================================== */

int query_records(const char *class_str, const char *keyword,
                  char *results_buf, int buf_size, int *match_count)
{
    char file_path[256];
    FILE *fp;
    char line[1024];
    int matches = 0;
    int pos = 0;

    *match_count = 0;
    results_buf[0] = '\0';

    if (class_str == NULL || keyword == NULL || results_buf == NULL) {
        return -1;
    }

    /* 路径验证：class 必须是已验证的 4 位数字 */
    snprintf(file_path, sizeof(file_path), "%s/%s.txt", DATA_DIR, class_str);

    /* 安全检查：文件路径必须在 DATA_DIR 内 */
    {
        char resolved_path[512];
        char data_root[256];
        if (realpath(DATA_DIR, data_root) == NULL) {
            log_error("realpath(DATA_DIR) failed");
            return -1;
        }
        /* 尝试解析文件路径；文件可能不存在，这是正常的 */
        if (realpath(file_path, resolved_path) != NULL) {
            size_t root_len = strlen(data_root);
            if (strncmp(resolved_path, data_root, root_len) != 0 ||
                (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0')) {
                log_warning("data path traversal attempt blocked");
                return -2;  /* -2: 路径越界 → 403 */
            }
        }
    }

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        /* 文件不存在 → 向上层报告，由调用者返回 404 */
        return -1;
    }

    /* 逐行扫描，匹配 keyword */
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *tab1, *tab2;
        char student_id[64], name[64], gender[64];

        /* 去掉末尾换行符 */
        {
            size_t ll = strlen(line);
            if (ll > 0 && line[ll - 1] == '\n') line[ll - 1] = '\0';
            if (ll > 1 && line[ll - 2] == '\r') line[ll - 2] = '\0';
        }

        /* 检查是否包含 keyword（大小写敏感）。
         * keyword 为空或 NULL 时匹配所有行。 */
        if (keyword != NULL && keyword[0] != '\0') {
            if (strstr(line, keyword) == NULL) {
                continue;
            }
        }

        /* 解析制表符分隔的字段 */
        tab1 = strchr(line, '\t');
        tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;

        if (tab1 && tab2) {
            int id_len   = (int)(tab1 - line);
            int name_len = (int)(tab2 - tab1 - 1);
            int gd_len   = (int)strlen(tab2 + 1);

            if (id_len >= (int)sizeof(student_id)) id_len = (int)sizeof(student_id) - 1;
            if (name_len >= (int)sizeof(name)) name_len = (int)sizeof(name) - 1;
            if (gd_len >= (int)sizeof(gender)) gd_len = (int)sizeof(gender) - 1;

            memcpy(student_id, line, id_len);
            student_id[id_len] = '\0';
            memcpy(name, tab1 + 1, name_len);
            name[name_len] = '\0';
            memcpy(gender, tab2 + 1, gd_len);
            gender[gd_len] = '\0';
        } else {
            /* 单字段（无制表符），整行作为学号 */
            strncpy(student_id, line, sizeof(student_id) - 1);
            student_id[sizeof(student_id) - 1] = '\0';
            name[0]   = '-';
            name[1]   = '\0';
            gender[0] = '-';
            gender[1] = '\0';
        }

        /* 构建 HTML 表格行（转义所有字段） */
        {
            char esc_id[128], esc_name[128], esc_gender[64];
            html_escape(student_id, esc_id, sizeof(esc_id));
            html_escape(name, esc_name, sizeof(esc_name));
            html_escape(gender, esc_gender, sizeof(esc_gender));

            int written = snprintf(results_buf + pos, buf_size - pos,
                                   "<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                                   esc_id, esc_name, esc_gender);
            if (written > 0 && pos + written < buf_size) {
                pos += written;
            } else {
                break;  /* 缓冲区满 */
            }
            matches++;
        }
    }

    fclose(fp);
    results_buf[pos] = '\0';
    *match_count = matches;
    return 0;
}

/*
 * 在所有 data 目录下的 .txt 文件中搜索 keyword。
 * 用于只指定 keyword 不指定 class 的跨班级查询。
 */
static int query_all_classes(const char *keyword,
                             char *results_buf, int buf_size,
                             int *match_count)
{
    char data_root_abs[256];
    int matches = 0;
    int pos = 0;

    *match_count = 0;
    results_buf[0] = '\0';

    if (keyword == NULL || keyword[0] == '\0') return -1;

    if (realpath(DATA_DIR, data_root_abs) == NULL) return -1;

    /* 简单方案：遍历已知的班级文件 2011~2012 */
    /* 实际项目中可用 opendir/readdir 遍历目录 */
    const char *class_list[] = { "2011", "2012", NULL };
    for (int i = 0; class_list[i] != NULL; i++) {
        char file_path[256];
        FILE *fp;
        char line[1024];

        snprintf(file_path, sizeof(file_path), "%s/%s.txt", DATA_DIR, class_list[i]);

        /* 安全检查 */
        {
            char resolved[512];
            if (realpath(file_path, resolved) != NULL) {
                size_t rl = strlen(data_root_abs);
                if (strncmp(resolved, data_root_abs, rl) != 0 ||
                    (resolved[rl] != '/' && resolved[rl] != '\0')) {
                    continue;  /* 路径越界，跳过 */
                }
            }
        }

        fp = fopen(file_path, "r");
        if (fp == NULL) continue;

        while (fgets(line, sizeof(line), fp) != NULL) {
            char *tab1, *tab2;
            char student_id[64], name[64], gender[64];
            char class_id[8];

            /* 去掉换行 */
            size_t ll = strlen(line);
            if (ll > 0 && line[ll - 1] == '\n') line[ll - 1] = '\0';
            if (ll > 1 && line[ll - 2] == '\r') line[ll - 2] = '\0';

            /* 匹配 keyword */
            if (strstr(line, keyword) == NULL) continue;

            /* 解析制表符分隔 */
            tab1 = strchr(line, '\t');
            tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;

            if (tab1 && tab2) {
                int id_len   = (int)(tab1 - line);
                int name_len = (int)(tab2 - tab1 - 1);
                int gd_len   = (int)strlen(tab2 + 1);

                if (id_len >= (int)sizeof(student_id)) id_len = (int)sizeof(student_id) - 1;
                if (name_len >= (int)sizeof(name)) name_len = (int)sizeof(name) - 1;
                if (gd_len >= (int)sizeof(gender)) gd_len = (int)sizeof(gender) - 1;

                memcpy(student_id, line, id_len);
                student_id[id_len] = '\0';
                memcpy(name, tab1 + 1, name_len);
                name[name_len] = '\0';
                memcpy(gender, tab2 + 1, gd_len);
                gender[gd_len] = '\0';
            } else {
                strncpy(student_id, line, sizeof(student_id) - 1);
                student_id[sizeof(student_id) - 1] = '\0';
                name[0] = '-'; name[1] = '\0';
                gender[0] = '-'; gender[1] = '\0';
            }

            strncpy(class_id, class_list[i], sizeof(class_id) - 1);
            class_id[sizeof(class_id) - 1] = '\0';

            {
                char esc_id[128], esc_name[128], esc_gender[64], esc_class[8];
                html_escape(student_id, esc_id, sizeof(esc_id));
                html_escape(name, esc_name, sizeof(esc_name));
                html_escape(gender, esc_gender, sizeof(esc_gender));
                html_escape(class_id, esc_class, sizeof(esc_class));

                int written = snprintf(results_buf + pos, buf_size - pos,
                    "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                    esc_class, esc_id, esc_name, esc_gender);
                if (written > 0 && pos + written < buf_size) {
                    pos += written;
                } else break;
                matches++;
            }
        }
        fclose(fp);
    }

    results_buf[pos] = '\0';
    *match_count = matches;
    return 0;
}

/* ===== HTML 页面生成 (UESTC 风格) ====================================== */

/*
 * 生成 /search 页面的公共头部（含 CSS）。
 * 写入 html 缓冲区，返回已写入的字节数。
 */
static int write_page_head(char *html, int html_size)
{
    return snprintf(html, html_size,
        "<!DOCTYPE html>\r\n"
        "<html lang=\"zh-CN\">\r\n"
        "<head>\r\n"
        "<meta charset=\"UTF-8\">\r\n"
        "<title>学生信息查询</title>\r\n"
        "<style>\r\n"
        "*{margin:0;padding:0;box-sizing:border-box;}\r\n"
        "body{font-family:\"Microsoft YaHei\",sans-serif;"
        "background:#f8f9fa;color:#1b1f27;}\r\n"
        ".header{background:white;border-bottom:1px solid #e5e7eb;}\r\n"
        ".header-inner{width:1100px;margin:auto;height:90px;"
        "display:flex;align-items:center;justify-content:space-between;}\r\n"
        ".logo-box{display:flex;align-items:center;gap:15px;}\r\n"
        ".logo-box img{height:50px;}\r\n"
        ".logo-box span{color:#2f6db5;font-weight:700;font-size:14px;}\r\n"
        "nav{display:flex;gap:30px;}\r\n"
        "nav a{text-decoration:none;color:#333;font-size:14px;}\r\n"
        "nav a:hover{color:#2f6db5;}\r\n"
        ".container{width:900px;margin:50px auto;}\r\n"
        ".sub-title{color:#2f6db5;font-size:13px;font-weight:700;margin-bottom:10px;}\r\n"
        "h1{font-size:72px;font-weight:800;margin-bottom:20px;}\r\n"
        ".version{color:#6b7280;font-size:30px;margin-bottom:40px;}\r\n"
        "hr{border:none;border-top:1px solid #ddd;margin:35px 0;}\r\n"
        ".form-row{display:flex;align-items:flex-end;gap:16px;}\r\n"
        ".field{display:flex;flex-direction:column;}\r\n"
        ".field label{margin-bottom:8px;font-size:13px;color:#555;}\r\n"
        ".field input{width:160px;height:46px;border:1px solid #cfd4dc;"
        "border-radius:4px;padding:0 12px;font-size:15px;}\r\n"
        ".keyword input{width:350px;}\r\n"
        ".btn-get,.btn-post{height:46px;padding:0 24px;border-radius:4px;"
        "cursor:pointer;font-size:15px;font-weight:600;}\r\n"
        ".btn-get{background:#2f6db5;color:white;border:none;}\r\n"
        ".btn-get:hover{background:#245b9c;}\r\n"
        ".btn-post{background:white;color:#222;border:1px solid #333;}\r\n"
        ".btn-post:hover{background:#f0f0f0;}\r\n"
        ".result-tag{color:#2f6db5;font-size:13px;font-weight:700;margin-bottom:10px;}\r\n"
        "h2{font-size:52px;margin-bottom:20px;}\r\n"
        ".result-info{color:#6b7280;margin-bottom:30px;}\r\n"
        "table{width:100%%;border-collapse:collapse;}\r\n"
        "thead{background:#000;color:white;}\r\n"
        "thead th{text-align:left;padding:14px 18px;}\r\n"
        "tbody td{padding:14px 18px;background:white;}\r\n"
        "tbody tr:not(:last-child) td{border-bottom:1px solid #f0f0f0;}\r\n"
        ".no-result{color:red;font-size:20px;padding:40px 0;}\r\n"
        ".error-title{color:#cc0000;font-size:28px;margin-bottom:20px;}\r\n"
        ".error-msg{color:#666;font-size:18px;margin-bottom:20px;}\r\n"
        "</style>\r\n"
        "</head>\r\n"
        "<body>\r\n");
}

/*
 * 生成公共 header（logo + 导航）。
 */
static int write_header(char *html, int html_size)
{
    return snprintf(html, html_size,
        "<header class=\"header\">\r\n"
        "<div class=\"header-inner\">\r\n"
        "<div class=\"logo-box\">\r\n"
        "<img src=\"/img/logo.png\" alt=\"\">\r\n"
        "<span>DYNAMIC SEARCH</span>\r\n"
        "</div>\r\n"
        "<nav>\r\n"
        "<a href=\"/\">静态首页</a>\r\n"
        "<a href=\"/search\">动态查询</a>\r\n"
        "</nav>\r\n"
        "</div>\r\n"
        "</header>\r\n"
        "<main class=\"container\">\r\n");
}

/*
 * 生成搜索表单（含 GET / POST 两个按钮的完整页面）。
 * 如果已有查询值则预填充输入框。
 */
void generate_search_form_html(char *html, int html_size)
{
    int pos = 0;
    pos += write_page_head(html + pos, html_size - pos);
    pos += write_header(html + pos, html_size - pos);
    pos += snprintf(html + pos, html_size - pos,
        "<div class=\"sub-title\">W3D5 &middot; GET AND POST</div>\r\n"
        "<h1>学生信息查询</h1>\r\n"
        "<div class=\"version\">mini_webserver V1.5</div>\r\n"
        "<hr>\r\n"
        "<section class=\"search-area\">\r\n"
        "<div class=\"form-row\">\r\n"
        "<div class=\"field\">\r\n"
        "<label>班级</label>\r\n"
        "<input type=\"text\" name=\"class\" id=\"class-inp\""
        " placeholder=\"例如 2011\" maxlength=\"4\">\r\n"
        "</div>\r\n"
        "<div class=\"field keyword\">\r\n"
        "<label>关键词</label>\r\n"
        "<input type=\"text\" name=\"keyword\" id=\"keyword-inp\""
        " placeholder=\"姓名或性别\" maxlength=\"64\">\r\n"
        "</div>\r\n"
        "<button class=\"btn-get\" onclick=\"submitGet()\">GET 查询</button>\r\n"
        "<button class=\"btn-post\" onclick=\"submitPost()\">POST 查询</button>\r\n"
        "</div>\r\n"
        "</section>\r\n"
        "<script>\r\n"
        "function submitGet(){"
        "var c=document.getElementById('class-inp').value;"
        "var k=document.getElementById('keyword-inp').value;"
        "window.location.href='/search?class='+encodeURIComponent(c)"
        "+'&keyword='+encodeURIComponent(k);}\r\n"
        "function submitPost(){"
        "var c=document.getElementById('class-inp').value;"
        "var k=document.getElementById('keyword-inp').value;"
        "var f=document.createElement('form');"
        "f.method='POST';f.action='/search';"
        "f.innerHTML='<input name=class value='+c+'>'+"
        "'<input name=keyword value='+k+'>';"
        "f.style.display='none';document.body.appendChild(f);f.submit();}\r\n"
        "</script>\r\n"
        "</main>\r\n"
        "</body>\r\n"
        "</html>");
}

/*
 * 生成查询结果页（表单 + 结果合并）。
 * class_str / keyword 用于预填充表单和显示查询条件。
 */
void generate_result_page_html(const char *class_str, const char *keyword,
                               const char *table_rows, int match_count,
                               int query_mode,
                               char *html, int html_size)
{
    char esc_class[16], esc_keyword[128];
    char info_text[256];
    const char *col_headers;
    int pos = 0;

    html_escape(class_str, esc_class, sizeof(esc_class));
    html_escape(keyword, esc_keyword, sizeof(esc_keyword));

    /* info 文字按模式生成 */
    if (query_mode == 1) {
        /* 仅班级 */
        snprintf(info_text, sizeof(info_text),
                 "班级 %s，共 %d 条记录。", esc_class, match_count);
        col_headers = "<tr><th>学号</th><th>姓名</th><th>性别</th></tr>";
    } else if (query_mode == 2) {
        /* 仅关键词 */
        snprintf(info_text, sizeof(info_text),
                 "关键词 %s，共 %d 条记录。", esc_keyword, match_count);
        col_headers = "<tr><th>班级</th><th>学号</th><th>姓名</th><th>性别</th></tr>";
    } else {
        /* 班级 + 关键词 */
        snprintf(info_text, sizeof(info_text),
                 "班级 %s，关键词 %s，共 %d 条记录。",
                 esc_class, esc_keyword, match_count);
        col_headers = "<tr><th>学号</th><th>姓名</th><th>性别</th></tr>";
    }

    pos += write_page_head(html + pos, html_size - pos);
    pos += write_header(html + pos, html_size - pos);
    pos += snprintf(html + pos, html_size - pos,
        "<div class=\"sub-title\">W3D5 &middot; GET AND POST</div>\r\n"
        "<h1>学生信息查询</h1>\r\n"
        "<div class=\"version\">mini_webserver V1.5</div>\r\n"
        "<hr>\r\n"
        "<section class=\"search-area\">\r\n"
        "<div class=\"form-row\">\r\n"
        "<div class=\"field\">\r\n"
        "<label>班级</label>\r\n"
        "<input type=\"text\" name=\"class\" id=\"class-inp\""
        " value=\"%s\" maxlength=\"4\">\r\n"
        "</div>\r\n"
        "<div class=\"field keyword\">\r\n"
        "<label>关键词</label>\r\n"
        "<input type=\"text\" name=\"keyword\" id=\"keyword-inp\""
        " value=\"%s\" maxlength=\"64\">\r\n"
        "</div>\r\n"
        "<button class=\"btn-get\" onclick=\"submitGet()\">GET 查询</button>\r\n"
        "<button class=\"btn-post\" onclick=\"submitPost()\">POST 查询</button>\r\n"
        "</div>\r\n"
        "</section>\r\n"
        "<hr>\r\n"
        "<section class=\"result\">\r\n"
        "<div class=\"result-tag\">RESULT</div>\r\n"
        "<h2>查询结果</h2>\r\n"
        "<p class=\"result-info\">%s</p>\r\n",
        esc_class, esc_keyword, info_text);

    if (match_count > 0) {
        pos += snprintf(html + pos, html_size - pos,
            "<table>\r\n<thead>\r\n%s\r\n</thead>\r\n"
            "<tbody>\r\n%s</tbody>\r\n</table>\r\n",
            col_headers, table_rows);
    } else {
        pos += snprintf(html + pos, html_size - pos,
            "<div class=\"no-result\">未找到符合条件的记录</div>\r\n");
    }

    pos += snprintf(html + pos, html_size - pos,
        "</section>\r\n"
        "<script>\r\n"
        "function submitGet(){"
        "var c=document.getElementById('class-inp').value;"
        "var k=document.getElementById('keyword-inp').value;"
        "window.location.href='/search?class='+encodeURIComponent(c)"
        "+'&keyword='+encodeURIComponent(k);}\r\n"
        "function submitPost(){"
        "var c=document.getElementById('class-inp').value;"
        "var k=document.getElementById('keyword-inp').value;"
        "var f=document.createElement('form');"
        "f.method='POST';f.action='/search';"
        "f.innerHTML='<input name=class value='+c+'>'+"
        "'<input name=keyword value='+k+'>';"
        "f.style.display='none';document.body.appendChild(f);f.submit();}\r\n"
        "</script>\r\n"
        "</main>\r\n"
        "</body>\r\n"
        "</html>");
}

/*
 * 生成错误页面（UESTC 风格）。
 */
void generate_error_page_html(int status_code, const char *title,
                              const char *message,
                              char *html, int html_size)
{
    char esc_title[128], esc_msg[256];
    int pos = 0;
    html_escape(title, esc_title, sizeof(esc_title));
    html_escape(message, esc_msg, sizeof(esc_msg));

    pos += write_page_head(html + pos, html_size - pos);
    pos += write_header(html + pos, html_size - pos);
    pos += snprintf(html + pos, html_size - pos,
        "<h1 class=\"error-title\">%d %s</h1>\r\n"
        "<p class=\"error-msg\">%s</p>\r\n"
        "<p><a href=\"/search\">返回查询</a></p>\r\n"
        "</main>\r\n"
        "</body>\r\n"
        "</html>",
        status_code, esc_title, esc_msg);
}

/* ===== 发送 HTTP 响应的内部辅助函数 =================================== */

/*
 * 发送响应头 + 响应体（分开发送，避免缓冲区限制）。
 * 返回发送的总字节数。
 */
static int send_response(int client_fd, int status_code, const char *reason,
                         const char *mime, const char *body, int body_len)
{
    char header[512];
    int  header_len;
    int  total = 0;

    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, reason, mime, body_len);

    if (send_all(client_fd, header, (size_t)header_len) != 0) {
        return total;
    }
    total = header_len;

    if (body != NULL && body_len > 0) {
        if (send_all(client_fd, body, (size_t)body_len) != 0) {
            return total;
        }
        total += body_len;
    }

    return total;
}

/* ===== /search 主处理入口 ============================================ */

int handle_search_request(int client_fd, const char *method,
                          const char *query_str,
                          int *status_code, const char **mime_type,
                          int *body_bytes)
{
    char html[MAX_RESULT_HTML];
    char class_buf[8];
    char keyword_buf[128];
    int  html_len;

    *status_code = 500;
    *mime_type   = "text/html; charset=utf-8";
    *body_bytes  = 0;

    /* ---- 情况 1: GET /search（无参数）→ 返回搜索表单 ---- */
    if (strcmp(method, "GET") == 0 &&
        (query_str == NULL || *query_str == '\0')) {
        *status_code = 200;
        generate_search_form_html(html, sizeof(html));
        html_len = (int)strlen(html);
        *body_bytes = html_len;
        send_response(client_fd, 200, "OK",
                      "text/html; charset=utf-8", html, html_len);
        return html_len;
    }

    /* ---- 解析查询参数（class 和 keyword 均为可选） ---- */
    {
        int has_class   = 0;
        int has_keyword = 0;

        if (parse_query_string(query_str,
                               class_buf, sizeof(class_buf),
                               keyword_buf, sizeof(keyword_buf)) != 0) {
            *status_code = 400;
            generate_error_page_html(400, "Bad Request",
                         "参数格式错误。请提供 class 或 keyword 参数。",
                         html, sizeof(html));
            html_len = (int)strlen(html);
            *body_bytes = html_len;
            send_response(client_fd, 400, "Bad Request",
                          "text/html; charset=utf-8", html, html_len);
            log_info("/search parse failed, 400 returned");
            return html_len;
        }

        has_class   = (class_buf[0] != '\0');
        has_keyword = (keyword_buf[0] != '\0');

        /* 校验 class（如果提供） */
        if (has_class && !validate_class(class_buf)) {
            *status_code = 400;
            generate_error_page_html(400, "Bad Request",
                         "班级格式错误",
                         html, sizeof(html));
            html_len = (int)strlen(html);
            *body_bytes = html_len;
            send_response(client_fd, 400, "Bad Request",
                          "text/html; charset=utf-8", html, html_len);
            log_info("/search invalid class, 400 returned");
            return html_len;
        }

        /* 校验 keyword（如果提供） */
        if (has_keyword && !validate_keyword(keyword_buf)) {
            *status_code = 400;
            generate_error_page_html(400, "Bad Request",
                         "关键字格式错误或包含非法字符。",
                         html, sizeof(html));
            html_len = (int)strlen(html);
            *body_bytes = html_len;
            send_response(client_fd, 400, "Bad Request",
                          "text/html; charset=utf-8", html, html_len);
            log_info("/search invalid keyword, 400 returned");
            return html_len;
        }

        /* ---- 四种查询模式 ---- */
        {
            char table_rows[MAX_RESULT_HTML];
            int  match_count = 0;
            int  query_mode;   /* 0=both, 1=class-only, 2=keyword-only */
            int  data_status = 0;  /* 0=ok, -1=404, -2=403 */

            if (has_class && has_keyword) {
                query_mode = 0;
                data_status = query_records(class_buf, keyword_buf,
                                  table_rows, sizeof(table_rows),
                                  &match_count);
            } else if (has_class && !has_keyword) {
                query_mode = 1;
                data_status = query_records(class_buf, "",
                                  table_rows, sizeof(table_rows),
                                  &match_count);
            } else {
                query_mode = 2;
                query_all_classes(keyword_buf,
                                  table_rows, sizeof(table_rows),
                                  &match_count);
            }

            if (data_status != 0) {
                if (data_status == -2) {
                    *status_code = 403;
                    generate_error_page_html(403, "Forbidden",
                                 "拒绝访问：路径越界。",
                                 html, sizeof(html));
                    html_len = (int)strlen(html);
                    *body_bytes = html_len;
                    send_response(client_fd, 403, "Forbidden",
                                  "text/html; charset=utf-8", html, html_len);
                    log_warning("/search data path traversal, 403 returned");
                    return html_len;
                } else {
                    *status_code = 404;
                    generate_error_page_html(404, "Not Found",
                                 "班级数据不存在",
                                 html, sizeof(html));
                    html_len = (int)strlen(html);
                    *body_bytes = html_len;
                    send_response(client_fd, 404, "Not Found",
                                  "text/html; charset=utf-8", html, html_len);
                    log_info("/search data file not found, 404 returned");
                    return html_len;
                }
            }

            /* 生成结果页面 */
            *status_code = 200;
            generate_result_page_html(class_buf, keyword_buf,
                                      table_rows, match_count,
                                      query_mode,
                                      html, sizeof(html));
            html_len = (int)strlen(html);
            *body_bytes = html_len;
            send_response(client_fd, 200, "OK",
                          "text/html; charset=utf-8", html, html_len);

            {
                char log_msg[256];
                snprintf(log_msg, sizeof(log_msg),
                         "/search query: class=%s keyword=%s -> %d matches",
                         has_class ? class_buf : "*",
                         has_keyword ? keyword_buf : "*",
                         match_count);
                log_info(log_msg);
            }
            return html_len;
        }
    }
}
