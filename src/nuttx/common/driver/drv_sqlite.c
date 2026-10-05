/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_sqlite.c - SQLite 驱动
 *
 * WHAT : SQLite 驱动
 * WHY  : 数据库 CLI 与 GUI 共用的执行通道
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_sqlite.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : sqlite3 API 封装 + 结果回调
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

/*======================================
 *  SQLite 支持 / SQLite Support
 *======================================*/
#ifdef CONFIG_UTILS_SQLITE
#include <sqlite3.h>
#define HAVE_SQLITE 1
#else
#define HAVE_SQLITE 0
/* 未启用 SQLite 时给 sqlite3 一个不透明占位类型，使下方函数签名
 * 仍然可编译（函数体内部已有 #if HAVE_SQLITE 分支）
 * opaque placeholder so prototypes still compile when disabled */
typedef struct sqlite3_stub_s sqlite3;
#pragma message "SQLite not configured, CLI will show informational messages"
#endif

/*======================================
 *  常量定义 / Constants
 *======================================*/
#define MAX_ROWS       1000    /* 最大行数 / Max rows to display */
#define MAX_COLS       16     /* 最大列数 / Max columns */
#define MAX_COL_WIDTH  64     /* 最大列宽 / Max column width */
#define MAX_ROW_BUF    8192   /* 行数据缓冲区 / Row data buffer */
#define MAX_PATH       256    /* 路径最大长度 / Max path length */
#define SEP_LINE       "----------------------------------------------------"

/*======================================
 *  内部函数声明 / Internal Declarations
 *======================================*/
static int sqlite_cli_main(int argc, char **argv);
static int cmd_help(FILE *out);
static char *trim(char *s);
static void truncate_str(const char *src, char *dest, int maxlen);
/* do_select/do_modify 由无条件的 do_sql() 调用，声明保持无条件
 * （未启用 SQLite 时 sqlite3 为上方占位类型）
 * do_select/do_modify stay unconditional: called by do_sql() */
static int do_select(sqlite3 *db, const char *sql, FILE *out);
static int do_modify(sqlite3 *db, const char *sql, FILE *out);
#if HAVE_SQLITE
/* sqlite3 类型仅在启用 SQLite 时存在（原声明无条件展开导致编译失败）
 * sqlite3 type only exists when SQLite is compiled in */
static int do_sql(sqlite3 *db, const char *sql, FILE *out);
static int cmd_tables(sqlite3 *db, FILE *out);
static int cmd_schema(sqlite3 *db, const char *table, FILE *out);
static int cmd_export_csv(sqlite3 *db, const char *sql, const char *path, FILE *out);
static int cmd_dump(sqlite3 *db, const char *path, FILE *out);
static int sqlite_cli_export_csv(sqlite3 *db, const char *sql, const char *path);
/* 原 sqlite_cli_export_db 声明从未有对应定义（公开版本叫
 * sqlite_export_db），删除死声明 / dropped dead declaration */
#endif

/*======================================
 *  命令列表 / Command List
 *======================================*/

typedef struct {
    const char *name;           /* 命令名称 / Command name */
    const char *short_desc;     /* 简短描述 / Short description */
    const char *usage;           /* 使用方法 / Usage */
} cmd_info_t;

static const cmd_info_t g_cmds[] = {
    {"help",    "Show this help message",           "sqlite .help"},
    {"tables",  "List all tables in the database",  "sqlite /path/db.db .tables"},
    {"schema",  "Show CREATE statement for table", "sqlite /path/db.db .schema table_name"},
    {"export",  "Export query result to CSV",       "sqlite /path/db.db .export csv /path/out.csv [sql]"},
    {".dump",   "Dump entire database to file",     "sqlite /path/db.db .dump /path/out.db"},
    {".quit",   "Quit sqlite CLI",                  "sqlite .quit"},
    {"SELECT",  "Execute SELECT query",             "sqlite /path/db.db \"SELECT * FROM t\""},
    {"INSERT",  "Execute INSERT statement",        "sqlite /path/db.db \"INSERT INTO t VALUES(...)\""},
    {"UPDATE",  "Execute UPDATE statement",        "sqlite /path/db.db \"UPDATE t SET ...\""},
    {"DELETE",  "Execute DELETE statement",        "sqlite /path/db.db \"DELETE FROM t\""},
    {"CREATE",  "Execute CREATE TABLE",            "sqlite /path/db.db \"CREATE TABLE t (...)\""},
    {"DROP",    "Execute DROP TABLE",              "sqlite /path/db.db \"DROP TABLE t\""},
};

static const int g_cmd_count = sizeof(g_cmds) / sizeof(g_cmds[0]);

/*======================================
 *  主入口 / Main Entry Point
 *======================================*/

#ifdef CONFIG_SYSTEM_NSH
/**
 * sqlite_main - NSH 命令入口 / NSH command entry
 * argc: 参数数量 / Argument count
 * argv: 参数列表 / Argument list
 * return: 0 成功 / 0 success
 */
int sqlite_main(int argc, char **argv)
{
    return sqlite_cli_main(argc, argv);
}
#endif

/**
 * sqlite_cli_main - CLI 主函数 / CLI main function
 */
static int sqlite_cli_main(int argc, char **argv)
{
#if !HAVE_SQLITE
    printf("SQLite not available in this build.\n");
    printf("Enable CONFIG_UTILS_SQLITE in menuconfig to use SQLite.\n");
    (void)argc; (void)argv;
    return 0;
#else
    char db_path[MAX_PATH] = {0};
    const char *sql = NULL;
    bool is_cmd = false;
    bool is_dump = false;
    char dump_path[MAX_PATH] = {0};
    sqlite3 *db = NULL;
    int rc;
    int ret = 0;

    /* 解析参数 / Parse arguments */
    if (argc < 2) {
        cmd_help(stdout);
        return 0;
    }

    /* 第一个参数是数据库路径或命令 / First arg is DB path or command */
    if (argv[1][0] == '.') {
        /* 直接命令模式 / Direct command mode */
        is_cmd = true;
        sql = argv[1];
    } else if (strcmp(argv[1], ".quit") == 0 || strcmp(argv[1], ".exit") == 0) {
        printf("Goodbye!\n");
        return 0;
    } else if (strcmp(argv[1], ".help") == 0) {
        cmd_help(stdout);
        return 0;
    } else {
        /* 数据库路径模式 / DB path mode */
        strncpy(db_path, argv[1], sizeof(db_path) - 1);

        if (argc >= 3) {
            sql = argv[2];

            /* 点命令按 NSH 的 argc/argv 分开投递（不再是单字符串），
             * 子参数取 argv[3]/argv[4]...
             * dot commands arrive as separate NSH args */
            if (strcmp(sql, ".tables") == 0) {
                is_cmd = true;
            }
            /* .schema [table] 命令 / .schema [table] command */
            else if (strcmp(sql, ".schema") == 0) {
                is_cmd = true;
            }
            /* .export csv <path> [sql] 命令 / .export command */
            else if (strcmp(sql, ".export") == 0) {
                is_cmd = true;
            }
            /* .dump <path> 命令 / .dump command */
            else if (strcmp(sql, ".dump") == 0) {
                is_cmd = true;
                is_dump = true;
                if (argc >= 4) {
                    strncpy(dump_path, argv[3], sizeof(dump_path) - 1);
                }
            }
        }
    }

    /* 打开数据库 / Open database */
    if (db_path[0] != '\0') {
        rc = sqlite3_open(db_path, &db);
        if (rc != SQLITE_OK) {
            printf("Error: Cannot open database: %s\n", sqlite3_errmsg(db));
            if (db) sqlite3_close(db);
            return 1;
        }
        syslog(LOG_INFO, "SQLite CLI: opened %s\n", db_path);
    }

    /* 执行命令 / Execute command */
    if (is_cmd) {
        if (strcmp(sql, ".tables") == 0) {
            if (db) {
                ret = cmd_tables(db, stdout);
            } else {
                printf("Error: No database specified.\n");
                ret = 1;
            }
        } else if (strcmp(sql, ".schema") == 0) {
            if (db) {
                /* 表名来自 argv[3] / table name is argv[3] */
                const char *table = (argc >= 4) ? argv[3] : NULL;
                ret = cmd_schema(db, table, stdout);
            } else {
                printf("Error: No database specified.\n");
                ret = 1;
            }
        } else if (strcmp(sql, ".export") == 0) {
            if (db) {
                /* .export csv <path> [sql]：各参数独立 argv
                 * argv[3]=csv argv[4]=path argv[5]=sql */
                if (argc >= 5 && strcmp(argv[3], "csv") == 0) {
                    const char *query_sql = "SELECT * FROM sqlite_master";
                    if (argc >= 6) {
                        query_sql = argv[5];
                    }
                    ret = sqlite_cli_export_csv(db, query_sql, argv[4]);
                } else {
                    printf("Usage: .export csv /path/to/file.csv [sql]\n");
                    ret = 1;
                }
            } else {
                printf("Error: No database specified.\n");
                ret = 1;
            }
        } else if (is_dump) {
            if (db) {
                ret = cmd_dump(db, dump_path, stdout);
            } else {
                printf("Error: No database specified.\n");
                ret = 1;
            }
        } else if (strcmp(sql, ".help") == 0) {
            ret = cmd_help(stdout);
        } else {
            printf("Error: Unknown command '%s'\n", sql);
            ret = 1;
        }
    } else if (sql != NULL) {
        if (!db) {
            /* 无数据库句柄直接 prepare 会空指针崩溃 / guard NULL db */
            printf("Error: No database specified.\n");
            ret = 1;
        } else {
            ret = do_sql(db, sql, stdout);
        }
    } else {
        cmd_help(stdout);
    }

    if (db) {
        sqlite3_close(db);
    }

    return ret;
#endif /* HAVE_SQLITE */
}

/*======================================
 *  SQL 执行 / SQL Execution
 *======================================*/

/**
 * do_sql - 执行 SQL 语句 / Execute SQL statement
 */
static int do_sql(sqlite3 *db, const char *sql, FILE *out)
{
    char *trimmed = trim((char *)sql);
    if (trimmed[0] == '\0') {
        return 0;
    }

    if (strncasecmp(trimmed, "SELECT", 6) == 0 ||
        strncasecmp(trimmed, "PRAGMA", 6) == 0 ||
        strncasecmp(trimmed, "EXPLAIN", 7) == 0) {
        return do_select(db, trimmed, out);
    } else {
        return do_modify(db, trimmed, out);
    }
}

/**
 * do_select - 执行 SELECT 查询 / Execute SELECT query
 */
static int do_select(sqlite3 *db, const char *sql, FILE *out)
{
#if HAVE_SQLITE
    sqlite3_stmt *stmt = NULL;
    int rc;
    int cols;
    int rows = 0;
    int *col_widths;
    char line_buf[MAX_ROW_BUF];

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(out, "SQL Error: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    cols = sqlite3_column_count(stmt);
    if (cols > MAX_COLS) cols = MAX_COLS;

    /* 计算列宽 / Calculate column widths */
    col_widths = (int *)calloc(cols, sizeof(int));
    if (!col_widths) {
        sqlite3_finalize(stmt);
        return 1;
    }

    /* 初始化列宽为标题长度 / Init with header length */
    for (int i = 0; i < cols; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        col_widths[i] = strlen(name ? name : "");
    }

    /* 第一遍：计算每列最大宽度 / First pass: calculate max width per column */
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        rows++;
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            int len = 0;
            char buf[64];

            if (type == SQLITE_INTEGER) {
                snprintf(buf, sizeof(buf), "%lld", (long long)sqlite3_column_int64(stmt, i));
                len = strlen(buf);
            } else if (type == SQLITE_FLOAT) {
                snprintf(buf, sizeof(buf), "%.6g", sqlite3_column_double(stmt, i));
                len = strlen(buf);
            } else if (type == SQLITE_NULL) {
                len = 4; /* "NULL" */
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                len = text ? (int)strlen((const char *)text) : 0;
            }

            if (len > MAX_COL_WIDTH) len = MAX_COL_WIDTH;
            if (len > col_widths[i]) col_widths[i] = len;
        }
    }

    /* 重置语句以再次遍历 / Reset to iterate again */
    sqlite3_reset(stmt);

    /* 打印表头 / Print header */
    fprintf(out, "%s\n", SEP_LINE);
    for (int i = 0; i < cols; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        fprintf(out, "%-*s", col_widths[i], name ? name : "");
        if (i < cols - 1) fprintf(out, " | ");
    }
    fprintf(out, "\n");

    /* 打印分隔线 / Print separator */
    for (int i = 0; i < cols; i++) {
        for (int j = 0; j < col_widths[i]; j++) fprintf(out, "-");
        if (i < cols - 1) fprintf(out, "-+-");
    }
    fprintf(out, "\n");

    /* 打印数据行 / Print data rows */
    rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        rows++;
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            char buf[MAX_COL_WIDTH + 1];

            if (type == SQLITE_INTEGER) {
                snprintf(buf, sizeof(buf), "%lld", (long long)sqlite3_column_int64(stmt, i));
            } else if (type == SQLITE_FLOAT) {
                snprintf(buf, sizeof(buf), "%.6g", sqlite3_column_double(stmt, i));
            } else if (type == SQLITE_NULL) {
                snprintf(buf, sizeof(buf), "%s", "NULL");
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                if (text) {
                    truncate_str((const char *)text, buf, MAX_COL_WIDTH);
                } else {
                    snprintf(buf, sizeof(buf), "%s", "NULL");
                }
            }

            fprintf(out, "%-*s", col_widths[i], buf);
            if (i < cols - 1) fprintf(out, " | ");
        }
        fprintf(out, "\n");
    }

    fprintf(out, "%s\n", SEP_LINE);
    fprintf(out, "(%d rows returned)\n", rows);

    sqlite3_finalize(stmt);
    free(col_widths);
    syslog(LOG_INFO, "SQLite: SELECT returned %d rows\n", rows);
    return 0;
#else
    (void)db; (void)sql; (void)out;
    return 1;
#endif
}

/**
 * do_modify - 执行 INSERT/UPDATE/DELETE / Execute INSERT/UPDATE/DELETE
 */
static int do_modify(sqlite3 *db, const char *sql, FILE *out)
{
#if HAVE_SQLITE
    char *err_msg = NULL;
    int rc;

    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(out, "SQL Error: %s\n", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
        return 1;
    }

    int changes = sqlite3_changes(db);
    fprintf(out, "Query executed successfully.\n");
    fprintf(out, "%d row(s) affected.\n", changes);
    syslog(LOG_INFO, "SQLite: %d rows affected\n", changes);
    return 0;
#else
    (void)db; (void)sql; (void)out;
    return 1;
#endif
}

/*======================================
 *  .tables 命令 / .tables Command
 *======================================*/

static int cmd_tables(sqlite3 *db, FILE *out)
{
#if HAVE_SQLITE
    sqlite3_stmt *stmt = NULL;
    int rc;
    int count = 0;

    rc = sqlite3_prepare_v2(db,
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(out, "Error: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    fprintf(out, "Tables in database:\n");
    fprintf(out, "%s\n", SEP_LINE);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        if (name) {
            fprintf(out, "  %s\n", name);
            count++;
        }
    }
    fprintf(out, "%s\n", SEP_LINE);
    fprintf(out, "(%d tables)\n", count);

    sqlite3_finalize(stmt);
    return 0;
#else
    (void)db; (void)out;
    return 1;
#endif
}

/*======================================
 *  .schema 命令 / .schema Command
 *======================================*/

static int cmd_schema(sqlite3 *db, const char *table, FILE *out)
{
#if HAVE_SQLITE
    sqlite3_stmt *stmt = NULL;
    char sql[512];
    int rc;
    int count = 0;

    if (table && strlen(table) > 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT sql FROM sqlite_master WHERE type='table' AND name=?");
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT sql FROM sqlite_master WHERE type='table' ORDER BY name");
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(out, "Error: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    /* 绑定表名参数防止SQL注入 / Bind table name to prevent SQL injection */
    if (table && strlen(table) > 0) {
        sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
    }

    fprintf(out, "Schema:\n");
    fprintf(out, "%s\n", SEP_LINE);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *sql_text = sqlite3_column_text(stmt, 0);
        if (sql_text) {
            fprintf(out, "%s;\n\n", sql_text);
            count++;
        }
    }

    if (count == 0) {
        fprintf(out, "(no schema found)\n");
    }

    fprintf(out, "%s\n", SEP_LINE);

    sqlite3_finalize(stmt);
    return 0;
#else
    (void)db; (void)table; (void)out;
    return 1;
#endif
}

/*======================================
 *  .export csv 命令 / .export csv Command
 *======================================*/

static int sqlite_cli_export_csv(sqlite3 *db, const char *sql, const char *path)
{
#if HAVE_SQLITE
    FILE *fp;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int cols;
    int rows = 0;

    fp = fopen(path, "w");
    if (!fp) {
        printf("Error: Cannot open file '%s' for writing: %s\n",
               path, strerror(errno));
        return 1;
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("SQL Error: %s\n", sqlite3_errmsg(db));
        fclose(fp);
        return 1;
    }

    cols = sqlite3_column_count(stmt);
    if (cols > MAX_COLS) cols = MAX_COLS;

    /* 写表头 / Write header */
    for (int i = 0; i < cols; i++) {
        const char *name = (const char *)sqlite3_column_name(stmt, i);
        if (i > 0) fprintf(fp, ",");
        fprintf(fp, "\"%s\"", name ? name : "");
    }
    fprintf(fp, "\n");

    /* 写数据 / Write data */
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        rows++;
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            if (i > 0) fprintf(fp, ",");

            if (type == SQLITE_INTEGER) {
                fprintf(fp, "%lld", (long long)sqlite3_column_int64(stmt, i));
            } else if (type == SQLITE_FLOAT) {
                fprintf(fp, "%.6g", sqlite3_column_double(stmt, i));
            } else if (type == SQLITE_NULL) {
                fprintf(fp, "");
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                if (text) {
                    /* CSV 转义 / CSV escape */
                    const char *s = (const char *)text;
                    int need_quote = 0;
                    while (*s) {
                        if (*s == '"' || *s == ',' || *s == '\n' || *s == '\r') {
                            need_quote = 1;
                            break;
                        }
                        s++;
                    }
                    if (need_quote) fprintf(fp, "\"");
                    s = (const char *)text;
                    while (*s) {
                        if (*s == '"') fprintf(fp, "\"\"");
                        else fprintf(fp, "%c", *s);
                        s++;
                    }
                    if (need_quote) fprintf(fp, "\"");
                }
            }
        }
        fprintf(fp, "\n");
    }

    sqlite3_finalize(stmt);
    fclose(fp);

    printf("Exported %d rows to CSV: %s\n", rows, path);
    syslog(LOG_INFO, "SQLite CLI: exported %d rows to CSV: %s\n", rows, path);
    return 0;
#else
    (void)db; (void)sql; (void)path;
    return 1;
#endif
}

#if HAVE_SQLITE
/**
 * cmd_export_csv - 导出 CSV 命令实现 / Export CSV command implementation
 */
static int cmd_export_csv(sqlite3 *db, const char *sql, const char *path, FILE *out)
{
    (void)out;
    return sqlite_cli_export_csv(db, sql, path);
}
#endif

/*======================================
 *  .dump 命令 / .dump Command
 *======================================*/

static int cmd_dump(sqlite3 *db, const char *path, FILE *out)
{
#if HAVE_SQLITE
    sqlite3 *dst_db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int count = 0;
    char *err_msg = NULL;

    if (path == NULL || path[0] == '\0') {
        fprintf(out, "Error: No output path specified.\n");
        return 1;
    }

    /* 使用 SQLite 的 backup API 或复制数据库 / Use SQLite backup API or copy DB */
    /* 简化实现：使用 VACUUM INTO 或手动复制 / Simplified: use VACUUM INTO or manual copy */
    fprintf(out, "Dumping database to: %s\n", path);

    /* 创建备份 / Create backup */
    rc = sqlite3_open(path, &dst_db);
    if (rc != SQLITE_OK) {
        fprintf(out, "Error: Cannot create output database: %s\n",
                sqlite3_errmsg(dst_db));
        if (dst_db) sqlite3_close(dst_db);
        return 1;
    }

    /* 获取所有表数据 / Get all table data */
    rc = sqlite3_prepare_v2(db,
        "SELECT name, sql FROM sqlite_master WHERE type='table' OR type='index'",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(out, "Error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(dst_db);
        return 1;
    }

    /* 复制表结构和数据 / Copy table structure and data */
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        const unsigned char *create_sql = sqlite3_column_text(stmt, 1);

        if (create_sql && name) {
            /* 执行建表 / Execute CREATE */
            rc = sqlite3_exec(dst_db, (const char *)create_sql, NULL, NULL, &err_msg);
            if (rc != SQLITE_OK) {
                /* 忽略已存在的错误 / Ignore "already exists" errors */
                if (err_msg && strstr(err_msg, "already exists") == NULL) {
                    syslog(LOG_ERR, "SQLite dump: create table error: %s\n", err_msg);
                }
                sqlite3_free(err_msg);
                err_msg = NULL;
            }

            /* 复制数据 / Copy data */
            char select_sql[MAX_PATH * 2];
            snprintf(select_sql, sizeof(select_sql),
                     "SELECT * FROM \"%s\"", name);

            sqlite3_stmt *data_stmt = NULL;
            rc = sqlite3_prepare_v2(db, select_sql, -1, &data_stmt, NULL);
            if (rc == SQLITE_OK) {
                char insert_sql[MAX_PATH * 4];
                int cols = sqlite3_column_count(data_stmt);

                while (sqlite3_step(data_stmt) == SQLITE_ROW) {
                    snprintf(insert_sql, sizeof(insert_sql), "INSERT INTO \"%s\" VALUES (", name);
                    char val_buf[256];
                    for (int i = 0; i < cols; i++) {
                        if (i > 0) strncat(insert_sql, ",", sizeof(insert_sql) - strlen(insert_sql) - 1);
                        int type = sqlite3_column_type(data_stmt, i);
                        if (type == SQLITE_NULL) {
                            strncat(insert_sql, "NULL", sizeof(insert_sql) - strlen(insert_sql) - 1);
                        } else if (type == SQLITE_INTEGER) {
                            snprintf(val_buf, sizeof(val_buf), "%lld",
                                    (long long)sqlite3_column_int64(data_stmt, i));
                            strncat(insert_sql, val_buf, sizeof(insert_sql) - strlen(insert_sql) - 1);
                        } else if (type == SQLITE_FLOAT) {
                            snprintf(val_buf, sizeof(val_buf), "%.6g",
                                    sqlite3_column_double(data_stmt, i));
                            strncat(insert_sql, val_buf, sizeof(insert_sql) - strlen(insert_sql) - 1);
                        } else {
                            const unsigned char *text = sqlite3_column_text(data_stmt, i);
                            strncat(insert_sql, "'", sizeof(insert_sql) - strlen(insert_sql) - 1);
                            if (text) {
                                /* 简单转义 / Simple escape */
                                const char *s = (const char *)text;
                                while (*s && strlen(insert_sql) < sizeof(insert_sql) - 2) {
                                    if (*s == '\'') {
                                        strncat(insert_sql, "''", sizeof(insert_sql) - strlen(insert_sql) - 1);
                                    } else {
                                        char tb[2] = {*s, 0};
                                        strncat(insert_sql, tb, sizeof(insert_sql) - strlen(insert_sql) - 1);
                                    }
                                    s++;
                                }
                            }
                            strncat(insert_sql, "'", sizeof(insert_sql) - strlen(insert_sql) - 1);
                        }
                    }
                    strncat(insert_sql, ")", sizeof(insert_sql) - strlen(insert_sql) - 1);

                    rc = sqlite3_exec(dst_db, insert_sql, NULL, NULL, &err_msg);
                    if (rc != SQLITE_OK) {
                        sqlite3_free(err_msg);
                        err_msg = NULL;
                    } else {
                        count++;
                    }
                }
                sqlite3_finalize(data_stmt);
            }
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(dst_db);

    fprintf(out, "Dump complete: %d rows copied to %s\n", count, path);
    syslog(LOG_INFO, "SQLite CLI: dumped %d rows to: %s\n", count, path);
    return 0;
#else
    (void)db; (void)path; (void)out;
    return 1;
#endif
}

/*======================================
 *  SQLite 数据库导出函数 / SQLite DB Export Functions
 *======================================*/

#if HAVE_SQLITE
/**
 * sqlite_export_csv - 导出查询结果到 CSV (公共接口) / Export query to CSV (public API)
 * @db: SQLite 数据库句柄 / SQLite database handle
 * @query: SQL 查询语句 / SQL query
 * @out_path: 输出文件路径 / Output file path
 * return: 0 成功 / 0 success
 */
int sqlite_export_csv(void *db, const char *query, const char *out_path)
{
    return sqlite_cli_export_csv((sqlite3 *)db, query, out_path);
}

/**
 * sqlite_export_db - 导出查询结果到新 SQLite 数据库 (公共接口)
 * Export query results to new SQLite database (public API)
 * @src_db: 源数据库句柄 / Source database handle
 * @query: SQL 查询语句 / SQL query
 * @out_path: 输出文件路径 / Output file path
 * return: 0 成功 / 0 success
 */
int sqlite_export_db(void *src_db, const char *query, const char *out_path)
{
#if HAVE_SQLITE
    sqlite3 *src = (sqlite3 *)src_db;
    sqlite3 *dst = NULL;
    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *ins_stmt = NULL;
    int rc;
    int cols;
    int rows = 0;
    char create_sql[MAX_PATH * 2];
    char insert_sql[MAX_PATH * 4];

    rc = sqlite3_open(out_path, &dst);
    if (rc != SQLITE_OK) {
        syslog(LOG_ERR, "SQLite export_db: failed to create %s: %s\n",
               out_path, sqlite3_errmsg(dst));
        return -1;
    }

    /* 准备 SELECT 语句 / Prepare SELECT statement */
    rc = sqlite3_prepare_v2(src, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        syslog(LOG_ERR, "SQLite export_db: prepare failed: %s\n", sqlite3_errmsg(src));
        sqlite3_close(dst);
        return -1;
    }

    cols = sqlite3_column_count(stmt);
    if (cols > MAX_COLS) cols = MAX_COLS;

    /* 生成建表 SQL / Generate CREATE TABLE SQL */
    snprintf(create_sql, sizeof(create_sql), "CREATE TABLE export_data (");
    for (int i = 0; i < cols; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        if (i > 0) strncat(create_sql, ", ", sizeof(create_sql) - strlen(create_sql) - 1);
        strncat(create_sql, "\"", sizeof(create_sql) - strlen(create_sql) - 1);
        strncat(create_sql, name ? name : "col", sizeof(create_sql) - strlen(create_sql) - 1);
        strncat(create_sql, "\" TEXT", sizeof(create_sql) - strlen(create_sql) - 1);
    }
    strncat(create_sql, ")", sizeof(create_sql) - strlen(create_sql) - 1);

    rc = sqlite3_exec(dst, create_sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        syslog(LOG_ERR, "SQLite export_db: create table failed\n");
        sqlite3_finalize(stmt);
        sqlite3_close(dst);
        return -1;
    }

    /* 准备插入语句 / Prepare INSERT statement */
    snprintf(insert_sql, sizeof(insert_sql), "INSERT INTO export_data VALUES (");
    for (int i = 0; i < cols; i++) {
        if (i > 0) strncat(insert_sql, ",?", sizeof(insert_sql) - strlen(insert_sql) - 1);
        else strncat(insert_sql, "?", sizeof(insert_sql) - strlen(insert_sql) - 1);
    }
    strncat(insert_sql, ")", sizeof(insert_sql) - strlen(insert_sql) - 1);

    rc = sqlite3_prepare_v2(dst, insert_sql, -1, &ins_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_close(dst);
        return -1;
    }

    /* 执行查询并插入 / Execute query and insert */
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        sqlite3_reset(ins_stmt);
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_NULL) {
                sqlite3_bind_null(ins_stmt, i + 1);
            } else if (type == SQLITE_INTEGER) {
                sqlite3_bind_int64(ins_stmt, i + 1, sqlite3_column_int64(stmt, i));
            } else if (type == SQLITE_FLOAT) {
                sqlite3_bind_double(ins_stmt, i + 1, sqlite3_column_double(stmt, i));
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                sqlite3_bind_text(ins_stmt, i + 1, (const char *)text, -1, SQLITE_TRANSIENT);
            }
        }
        sqlite3_step(ins_stmt);
        rows++;
    }

    sqlite3_finalize(stmt);
    sqlite3_finalize(ins_stmt);
    sqlite3_close(dst);

    syslog(LOG_INFO, "SQLite export_db: exported %d rows to %s\n", rows, out_path);
    return 0;
#else
    (void)src_db; (void)query; (void)out_path;
    return -1;
#endif
}
#endif /* HAVE_SQLITE */

/*======================================
 *  帮助命令 / Help Command
 *======================================*/

static int cmd_help(FILE *out)
{
    fprintf(out, "\n");
    fprintf(out, "SQLite CLI - NuttX SQLite Database Tool\n");
    fprintf(out, "%s\n", SEP_LINE);
    fprintf(out, "Usage: sqlite <database> [command|sql]\n");
    fprintf(out, "\n");
    fprintf(out, "Commands:\n");

    for (int i = 0; i < g_cmd_count; i++) {
        fprintf(out, "  %-12s %s\n", g_cmds[i].name, g_cmds[i].short_desc);
    }

    fprintf(out, "\n");
    fprintf(out, "Examples:\n");
    fprintf(out, "  sqlite /sd0/data.db .tables\n");
    fprintf(out, "  sqlite /sd0/data.db .schema mytable\n");
    fprintf(out, "  sqlite /sd0/data.db \"SELECT * FROM users LIMIT 10\"\n");
    fprintf(out, "  sqlite /sd0/data.db .export csv /sd0/out.csv \"SELECT * FROM users\"\n");
    fprintf(out, "  sqlite /sd0/data.db .dump /sd0/backup.db\n");
    fprintf(out, "\n");
    fprintf(out, "Notes:\n");
    fprintf(out, "  - Max %d rows displayed per query\n", MAX_ROWS);
    fprintf(out, "  - Max %d columns displayed per query\n", MAX_COLS);
    fprintf(out, "  - Column values truncated at %d characters\n", MAX_COL_WIDTH);
    fprintf(out, "\n");

    return 0;
}

/*======================================
 *  工具函数 / Utility Functions
 *======================================*/

/**
 * trim - 去除字符串首尾空白 / Trim leading/trailing whitespace
 */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    return s;
}

/**
 * truncate_str - 截断字符串到最大长度 / Truncate string to max length
 */
static void truncate_str(const char *src, char *dest, int maxlen)
{
    int len = strlen(src);
    if (len <= maxlen) {
        strcpy(dest, src);
        return;
    }

    strncpy(dest, src, maxlen - 3);
    dest[maxlen - 3] = '\0';
    strcat(dest, "...");
}

/*======================================
 *  公共 API (供其他模块调用) / Public API (for other modules)
 *======================================*/

#if HAVE_SQLITE
/**
 * sqlite_cli - 公共 SQLite CLI 接口 / Public SQLite CLI interface
 * @db_path: 数据库路径 / Database path
 * @sql: SQL 语句或命令 / SQL statement or command
 * return: 0 成功 / 0 success
 *
 * 示例 / Example:
 *   sqlite_cli("/sd0/data.db", "SELECT * FROM t");
 *   sqlite_cli("/sd0/data.db", ".tables");
 */
int sqlite_cli(const char *db_path, const char *sql)
{
    sqlite3 *db = NULL;
    int ret;

    if (db_path && db_path[0]) {
        if (sqlite3_open(db_path, &db) != SQLITE_OK) {
            if (db)
                sqlite3_close(db);
            return -1;
        }
    }

    /* db==NULL 时 do_sql→prepare 会解引用空句柄 / guard NULL db */
    if (!db || !sql || !sql[0]) {
        if (db)
            sqlite3_close(db);
        return -EINVAL;
    }

    ret = do_sql(db, sql, stdout);

    if (db) {
        sqlite3_close(db);
    }

    return ret;
}
#endif /* HAVE_SQLITE */
