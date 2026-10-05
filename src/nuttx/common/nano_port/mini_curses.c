/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * nano_port/mini_curses.c - mini-curses 实现（ANSI 转义直出）
 *
 * WHAT : GNU nano 8.4 所需 ncurses 子集的完整实现
 * WHY  : NuttX 无 ncurses；AV 控制台（cvbs_console）与 VT100 串口
 *        终端都讲 ANSI CSI——虚拟屏 + diff 刷新即可全屏编辑
 * WHO  : deps/nano/src/*.c（经 nano_port/curses.h）
 * WHERE: esp32-retro-ws/src/nuttx/common/nano_port/mini_curses.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 单元格 = {UTF-32 码点, 属性}，宽字符占双列（第二列标
 *        WCONT 标志）；doupdate 比对虚拟屏/上屏，最小转义输出；
 *        wgetch = termios raw 读 + 转义序列解码（箭头/翻页/
 *        Home/End/删除/F1-F4/Ctrl+方向）；LINES/COLS 取自
 *        TIOCGWINSZ（cvbscon 报 13×40，串口 24×80）
 */

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>

#include "curses.h"
#include "term.h"

/*==========================
 *  虚拟屏
 *==========================*/

/* 宽字符后续列标记（码点值用不到 0x110000 以上） */
#define CELL_WCONT   0x80000000u

typedef struct
{
    unsigned int ch;     /* UTF-32 码点（或 CELL_WCONT） */
    unsigned char attr;
}
cell_t;

WINDOW *stdscr = NULL;
WINDOW *curscr = NULL;   /* full_refresh() 目标 */
int LINES = 24;
int COLS  = 80;

static cell_t *g_virt = NULL;      /* 虚拟屏（应用写入） */
static cell_t *g_prev = NULL;      /* 上屏（已输出） */
static unsigned char g_attr_cur = 0;   /* 硬件当前属性 */
static bool g_raw_on = false;
static struct termios g_tio_saved;
static int g_escdelay_ms = 50;     /* ESC 判定窗口 */
static int g_unget = -1;           /* ungetch 单槽 */
static int g_cursor_y = 0, g_cursor_x = 0;   /* 硬件光标 */
static bool g_cursor_visible = true;
static WINDOW *g_last_win = NULL;  /* 最近刷新窗口（光标落点） */

#define CELL_AT(buf, y, x)  ((buf)[(size_t)(y) * COLS + (x)])

static inline int umin(int a, int b) { return a < b ? a : b; }

/*==========================
 *  终端字节输出
 *==========================*/

static void emit(const char *s)
{
    fputs(s, stdout);
}

static void emitf(const char *fmt, int a, int b)
{
    printf(fmt, a, b);
}

static void flush_out(void)
{
    fflush(stdout);
}

/*==========================
 *  生命周期
 *==========================*/

static bool winsize_query(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return true;
    }
    return false;
}

static void tty_raw_enter(void)
{
    struct termios tio;

    if (tcgetattr(STDIN_FILENO, &g_tio_saved) != 0)
        return;

    tio = g_tio_saved;
    tio.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    tio.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR | ISTRIP);
    tio.c_oflag &= ~(tcflag_t)(OPOST);
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    g_raw_on = true;
}

static void tty_raw_leave(void)
{
    if (g_raw_on) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_tio_saved);
        g_raw_on = false;
    }
}

WINDOW *initscr(void)
{
    int rows = 24, cols = 80;

    tty_raw_enter();
    winsize_query(&rows, &cols);

    LINES = rows;
    COLS = cols;

    g_virt = malloc(sizeof(cell_t) * (size_t)LINES * COLS);
    g_prev = malloc(sizeof(cell_t) * (size_t)LINES * COLS);
    if (g_virt == NULL || g_prev == NULL) {
        free(g_virt);
        free(g_prev);
        g_virt = g_prev = NULL;
        return NULL;
    }

    for (int i = 0; i < LINES * COLS; i++) {
        g_virt[i].ch = ' ';
        g_virt[i].attr = 0;
        g_prev[i].ch = 0;       /* 强制首帧全刷 */
        g_prev[i].attr = 0xff;
    }

    stdscr = newwin(LINES, COLS, 0, 0);
    curscr = newwin(LINES, COLS, 0, 0);
    g_last_win = stdscr;

    emit("\x1b[2J\x1b[H");          /* 清屏归位 */
    emit("\x1b[?25h");              /* 显示光标 */
    g_attr_cur = 0;
    g_cursor_y = g_cursor_x = 0;
    flush_out();
    return stdscr;
}

int endwin(void)
{
    emit("\x1b[0m");                /* 复位属性 */
    emit("\x1b[?25h");              /* 光标可见 */
    printf("\x1b[%d;1H", LINES);    /* 光标落底行 */
    emit("\x1b[K");
    flush_out();

    tty_raw_leave();

    free(g_virt);
    free(g_prev);
    g_virt = g_prev = NULL;
    delwin(stdscr);
    stdscr = NULL;
    return OK;
}

WINDOW *newwin(int nlines, int ncols, int begy, int begx)
{
    WINDOW *w = calloc(1, sizeof(WINDOW));

    if (w == NULL)
        return NULL;

    if (nlines <= 0)
        nlines = LINES - begy;
    if (ncols <= 0)
        ncols = COLS - begx;

    w->begy = begy;
    w->begx = begx;
    w->maxy = umin(nlines, LINES - begy);
    w->maxx = umin(ncols, COLS - begx);
    return w;
}

int delwin(WINDOW *win)
{
    if (win == NULL || win == stdscr) {
        if (win == stdscr) {
            free(win);
            stdscr = NULL;
        }
        return OK;
    }
    free(win);
    return OK;
}

/*==========================
 *  UTF-8 编解码 + 宽度
 *==========================*/

/* 码点 → 显示列宽（CJK/全角=2；NuttX wcwidth 的本地保守版） */
static int utf8_width(unsigned int cp)
{
    if (cp < 0x20)
        return 1;
    if (cp == 0)
        return 1;

    /* 组合用区 / 零宽 */
    if ((cp >= 0x0300 && cp <= 0x036f) ||
        (cp >= 0x200b && cp <= 0x200f) || cp == 0xfeff)
        return 0;

    /* 中日韩谚文全角区 */
    if ((cp >= 0x1100 && cp <= 0x115f) ||
        (cp >= 0x2e80 && cp <= 0xa4cf) ||
        (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe30 && cp <= 0xfe6f) ||
        (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6) ||
        (cp >= 0x1f300 && cp <= 0x1f64f) ||
        (cp >= 0x1f900 && cp <= 0x1f9ff) ||
        (cp >= 0x20000 && cp <= 0x3fffd))
        return 2;

    return 1;
}

/* 解一个 UTF-8 字符；返回码点，*adv = 字节数 */
static unsigned int utf8_decode(const char *s, int *adv)
{
    unsigned char c = (unsigned char)s[0];

    if (c < 0x80) {
        *adv = 1;
        return c;
    }
    if ((c & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        *adv = 2;
        return ((unsigned)c & 0x1f) << 6 | ((unsigned char)s[1] & 0x3f);
    }
    if ((c & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        *adv = 3;
        return ((unsigned)c & 0x0f) << 12 |
               ((unsigned char)s[1] & 0x3f) << 6 |
               ((unsigned char)s[2] & 0x3f);
    }
    if ((c & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 &&
        (s[3] & 0xc0) == 0x80) {
        *adv = 4;
        return ((unsigned)c & 0x07) << 18 |
               ((unsigned char)s[1] & 0x3f) << 12 |
               ((unsigned char)s[2] & 0x3f) << 6 |
               ((unsigned char)s[3] & 0x3f);
    }
    *adv = 1;
    return (unsigned char)'?';
}

/* 码点 → UTF-8 字节；返回长度 */
static int utf8_encode(unsigned int cp, char out[5])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

/*==========================
 *  窗口写入（落到虚拟屏）
 *==========================*/

int wmove(WINDOW *win, int y, int x)
{
    if (win == NULL || y < 0 || x < 0 || y >= win->maxy || x >= win->maxx)
        return ERR;
    win->cury = y;
    win->curx = x;
    return OK;
}

static void cell_write(WINDOW *win, unsigned int cp)
{
    int gy = win->begy + win->cury;
    int gx = win->begx + win->curx;
    int w = utf8_width(cp);

    if (gy < 0 || gy >= LINES || gx < 0 || gx >= COLS)
        return;

    CELL_AT(g_virt, gy, gx).ch = cp;
    CELL_AT(g_virt, gy, gx).attr = (unsigned char)win->attr;

    /* 宽字符：占第二列；若下一列越界则截断显示 */
    if (w == 2 && gx + 1 < COLS) {
        CELL_AT(g_virt, gy, gx + 1).ch = CELL_WCONT;
        CELL_AT(g_virt, gy, gx + 1).attr = (unsigned char)win->attr;
    }

    win->curx += (w > 0) ? w : 1;
    if (win->curx >= win->maxx)
        win->curx = win->maxx - 1;
}

int waddch(WINDOW *win, unsigned int ch)
{
    if (win == NULL)
        return ERR;
    cell_write(win, ch & 0x1fffff);
    return OK;
}

int waddnstr(WINDOW *win, const char *str, int n)
{
    if (win == NULL || str == NULL)
        return ERR;

    int i = 0;
    while ((n < 0 || i < n) && str[i] != '\0') {
        int adv;
        unsigned int cp = utf8_decode(&str[i], &adv);
        if (n >= 0 && i + adv > n)
            break;
        cell_write(win, cp);
        i += adv;
    }
    return OK;
}

int waddstr(WINDOW *win, const char *str)
{
    return waddnstr(win, str, -1);
}

int wclrtoeol(WINDOW *win)
{
    if (win == NULL)
        return ERR;

    int gy = win->begy + win->cury;
    for (int x = win->curx; x < win->maxx; x++) {
        if (gy >= 0 && gy < LINES) {
            CELL_AT(g_virt, gy, win->begx + x).ch = ' ';
            CELL_AT(g_virt, gy, win->begx + x).attr = 0;
        }
    }
    return OK;
}

int wclrtobot(WINDOW *win)
{
    if (win == NULL)
        return ERR;

    for (int y = win->cury; y < win->maxy; y++)
        for (int x = (y == win->cury ? win->curx : 0); x < win->maxx; x++) {
            int gy = win->begy + y;
            int gx = win->begx + x;
            if (gy < LINES && gx < COLS) {
                CELL_AT(g_virt, gy, gx).ch = ' ';
                CELL_AT(g_virt, gy, gx).attr = 0;
            }
        }
    return OK;
}

int werase(WINDOW *win)
{
    if (win == NULL)
        return ERR;
    for (int y = 0; y < win->maxy; y++)
        for (int x = 0; x < win->maxx; x++) {
            int gy = win->begy + y;
            int gx = win->begx + x;
            if (gy >= 0 && gy < LINES && gx >= 0 && gx < COLS) {
                CELL_AT(g_virt, gy, gx).ch = ' ';
                CELL_AT(g_virt, gy, gx).attr = 0;
            }
        }
    return OK;
}

int wclear(WINDOW *win)
{
    return werase(win);
}

int winch(WINDOW *win)
{
    if (win == NULL)
        return ERR;
    return (int)CELL_AT(g_virt, win->begy + win->cury,
                        win->begx + win->curx).ch;
}

int wattroff(WINDOW *win, int attrs)
{
    if (win == NULL)
        return ERR;
    win->attr &= ~attrs;
    return OK;
}

int wattron(WINDOW *win, int attrs)
{
    if (win == NULL)
        return ERR;
    win->attr |= attrs;
    return OK;
}

int wattrset(WINDOW *win, int attrs)
{
    if (win == NULL)
        return ERR;
    win->attr = attrs;
    return OK;
}

int scrollok(WINDOW *win, bool bf)
{
    if (win == NULL)
        return ERR;
    win->scroll_ok = bf;
    return OK;
}

int leaveok(WINDOW *win, bool bf)
{
    (void)win;
    (void)bf;
    return OK;
}

int clearok(WINDOW *win, bool bf)
{
    (void)win;
    if (bf && g_prev != NULL) {
        for (int i = 0; i < LINES * COLS; i++)
            g_prev[i].ch = 0;
    }
    return OK;
}

int touchwin(WINDOW *win)
{
    return clearok(win, true);
}

/*==========================
 *  刷新
 *==========================*/

static void attr_apply(unsigned char want)
{
    if (want == g_attr_cur)
        return;

    emit("\x1b[0m");
    if (want & A_BOLD)
        emit("\x1b[1m");
    if (want & A_REVERSE)
        emit("\x1b[7m");
    if (want & A_UNDERLINE)
        emit("\x1b[4m");
    g_attr_cur = want;
}

static void cursor_goto(int y, int x)
{
    if (y == g_cursor_y) {
        if (x == g_cursor_x + 1) {
            emit("\x1b[C");
            g_cursor_x = x;
            return;
        }
        if (x == g_cursor_x) {
            return;
        }
    }
    emitf("\x1b[%d;%dH", y + 1, x + 1);
    g_cursor_y = y;
    g_cursor_x = x;
}

int wnoutrefresh(WINDOW *win)
{
    if (win == NULL)
        return ERR;
    g_last_win = win;
    return OK;
}

int doupdate(void)
{
    char buf[5];

    if (g_virt == NULL)
        return ERR;

    for (int y = 0; y < LINES; y++) {
        for (int x = 0; x < COLS; x++) {
            cell_t *v = &CELL_AT(g_virt, y, x);
            cell_t *p = &CELL_AT(g_prev, y, x);

            if (v->ch == p->ch && v->attr == p->attr)
                continue;

            cursor_goto(y, x);
            attr_apply(v->attr);

            if (v->ch == CELL_WCONT) {
                /* 宽字符后半：跟随前格（输出空格会破坏前格显示，
                 * 但前面主格已更新覆盖两列，这里跳过不输出） */
                buf[0] = ' ';
                buf[1] = 0;
            } else {
                int n = utf8_encode(v->ch & ~CELL_WCONT, buf);
                buf[n] = 0;
            }
            fputs(buf, stdout);
            g_cursor_x++;

            *p = *v;
        }
    }

    /* 光标落最近刷新窗口的光标位 */
    if (g_last_win != NULL) {
        int cy = g_last_win->begy + g_last_win->cury;
        int cx = g_last_win->begx + g_last_win->curx;
        if (cy >= 0 && cy < LINES && cx >= 0 && cx < COLS)
            cursor_goto(cy, cx);
    }

    flush_out();
    return OK;
}

int wrefresh(WINDOW *win)
{
    if (wnoutrefresh(win) == ERR)
        return ERR;
    if (win == curscr && g_prev != NULL) {
        for (int i = 0; i < LINES * COLS; i++)
            g_prev[i].ch = 0;       /* 强制整屏重画 */
    }
    return doupdate();
}

/*==========================
 *  输入
 *==========================*/

int keypad(WINDOW *win, bool bf)
{
    if (win == NULL)
        return ERR;
    win->keypad_ok = bf;
    return OK;
}

int nodelay(WINDOW *win, bool bf)
{
    if (win == NULL)
        return ERR;
    win->nodelay_ok = bf;
    return OK;
}

void timeout(int tenths)
{
    /* 全局超时：映射到 stdscr 的 halfdelay 语义 */
    if (stdscr != NULL)
        stdscr->delay_tenths = tenths;
}

int halfdelay(int tenths)
{
    if (stdscr == NULL || tenths <= 0)
        return ERR;
    stdscr->delay_tenths = tenths;
    return OK;
}

int nocbreak(void)  { return OK; }
int cbreak(void)    { return OK; }
int noecho(void)    { return OK; }
int echo(void)      { return OK; }
int nl(void)        { return OK; }
int nonl(void)      { return OK; }

int raw(void)
{
    if (!g_raw_on)
        tty_raw_enter();
    return OK;
}

int noraw(void)
{
    return OK;
}

int curs_set(int visibility)
{
    bool want = visibility != 0;

    if (want != g_cursor_visible) {
        emit(want ? "\x1b[?25h" : "\x1b[?25l");
        flush_out();
        g_cursor_visible = want;
    }
    return OK;
}

int flushinp(void)
{
    tcflush(STDIN_FILENO, TCIFLUSH);
    return OK;
}

int typeahead(int fd)
{
    (void)fd;
    return OK;
}

int meta(WINDOW *win, bool bf)
{
    (void)win;
    (void)bf;
    return OK;
}

int set_escdelay(int ms)
{
    g_escdelay_ms = ms;
    return OK;
}

int notimeout(WINDOW *win, bool bf)
{
    (void)win;
    (void)bf;
    return OK;
}

int ungetch(int ch)
{
    g_unget = ch;
    return OK;
}

/* 带超时的单字节读：0=无数据，-1=错，>0=字节 */
static int read_byte_timed(int timeout_ms)
{
    struct pollfd pfd;
    unsigned char c;
    int pr;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0)
        return 0;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;
    return c;
}

/* ESC 后续参数读（短超时） */
static int read_byte_esc(void)
{
    return read_byte_timed(g_escdelay_ms);
}

int wgetch(WINDOW *win)
{
    int c;

    if (win == NULL)
        return ERR;

    if (g_unget >= 0) {
        c = g_unget;
        g_unget = -1;
        return c;
    }

    int timeout_ms = -1;                    /* 阻塞 */
    if (win->nodelay_ok)
        timeout_ms = 0;
    else if (win->delay_tenths > 0)
        timeout_ms = win->delay_tenths * 100;

    c = read_byte_timed(timeout_ms);
    if (c <= 0)
        return ERR;

    if (c != 0x1b)
        return c;

    /* ESC 序列解码 */
    int c2 = read_byte_esc();
    if (c2 <= 0)
        return 0x1b;                        /* 独立 ESC（nano Meta） */

    if (c2 == 'O' || c2 == '[') {
        int c3 = read_byte_esc();
        if (c3 <= 0)
            return 0x1b;

        if (c2 == 'O') {
            switch (c3) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            case 'P': return KEY_F(1);
            case 'Q': return KEY_F(2);
            case 'R': return KEY_F(3);
            case 'S': return KEY_F(4);
            default:  return 0x1b;
            }
        }

        /* c2 == '['：可能带数字参数 + 可选修饰符 */
        int param = -1;
        int param2 = -1;
        while (c3 >= '0' && c3 <= '9') {
            if (param < 0)
                param = c3 - '0';
            else
                param = param * 10 + (c3 - '0');
            c3 = read_byte_esc();
            if (c3 <= 0)
                return 0x1b;
        }
        if (c3 == ';') {
            param2 = 0;
            c3 = read_byte_esc();
            while (c3 >= '0' && c3 <= '9') {
                param2 = param2 * 10 + (c3 - '0');
                c3 = read_byte_esc();
                if (c3 <= 0)
                    return 0x1b;
            }
        }

        bool modify = (param2 >= 5);        /* Ctrl 修饰 */

        switch (c3) {
        case 'A': return modify ? KEY_SR : KEY_UP;
        case 'B': return modify ? KEY_SF : KEY_DOWN;
        case 'C': return KEY_SRIGHT;        /* Shift/Ctrl+右 = 下一词 */
        case 'D': return KEY_SLEFT;         /* Shift/Ctrl+左 = 上一词 */
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'Z': return KEY_BTAB;
        case '~': {
            if (param2 >= 5) {
                /* 修饰 + 数字键 */
                switch (param) {
                case 1: case 7: return KEY_SHOME;
                case 4: case 8: return KEY_SEND;
                case 3: return KEY_SDC;
                case 5: return KEY_SPREVIOUS;
                case 6: return KEY_SNEXT;
                default: return 0x1b;
                }
            }
            switch (param) {
            case 1: case 7: return KEY_HOME;
            case 2: return KEY_IC;
            case 3: return KEY_DC;
            case 4: case 8: return KEY_END;
            case 5: return KEY_PPAGE;
            case 6: return KEY_NPAGE;
            case 11: return KEY_F(1);
            case 12: return KEY_F(2);
            case 13: return KEY_F(3);
            case 14: return KEY_F(4);
            case 15: return KEY_F(5);
            case 17: return KEY_F(6);
            case 18: return KEY_F(7);
            case 19: return KEY_F(8);
            case 20: return KEY_F(9);
            case 21: return KEY_F(10);
            case 23: return KEY_F(11);
            case 24: return KEY_F(12);
            default: return 0x1b;
            }
        }
        default:
            return 0x1b;
        }
    }

    /* ESC + 普通键 = nano Meta 组合：返回 ESC，后键重注入 */
    g_unget = c2;
    return 0x1b;
}

/*==========================
 *  printf 族 / 延时
 *==========================*/

#include <stdarg.h>

static int vprint_cells(WINDOW *win, const char *fmt, va_list ap)
{
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (n < 0)
        return ERR;
    waddstr(win, buf);
    return n;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...)
{
    va_list ap;
    int r;

    if (wmove(win, y, x) == ERR)
        return ERR;
    va_start(ap, fmt);
    r = vprint_cells(win, fmt, ap);
    va_end(ap);
    return r;
}

int wprintw(WINDOW *win, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vprint_cells(win, fmt, ap);
    va_end(ap);
    return r;
}

int mvprintw(int y, int x, const char *fmt, ...)
{
    va_list ap;
    int r;

    if (wmove(stdscr, y, x) == ERR)
        return ERR;
    va_start(ap, fmt);
    r = vprint_cells(stdscr, fmt, ap);
    va_end(ap);
    return r;
}

int printw(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vprint_cells(stdscr, fmt, ap);
    va_end(ap);
    return r;
}

int napms(int ms)
{
    usleep(ms * 1000);
    return OK;
}

int isendwin(void)
{
    return g_virt == NULL;
}

int beep(void)
{
    emit("\a");
    flush_out();
    return OK;
}

int define_key(const char *sequence, int symbol)
{
    (void)sequence;
    (void)symbol;
    return OK;      /* 序列已由 wgetch 解码器覆盖 */
}

int wredrawln(WINDOW *win, int start, int n)
{
    if (win == NULL || g_prev == NULL)
        return ERR;
    for (int y = start; y < start + n; y++) {
        int gy = win->begy + y;
        if (gy < 0 || gy >= LINES)
            continue;
        for (int x = 0; x < win->maxx && win->begx + x < COLS; x++)
            CELL_AT(g_prev, gy, win->begx + x).ch = 0;
    }
    return OK;
}

int wscrl(WINDOW *win, int n)
{
    if (win == NULL || !win->scroll_ok)
        return ERR;

    /* 虚拟屏窗口区域整块搬移 + 旧屏失效（强制重画） */
    for (int y = (n > 0 ? 0 : -n); y < win->maxy; y++) {
        int sy = win->begy + y + n;
        int dy = win->begy + y;
        if (sy < 0 || sy >= LINES || dy < 0 || dy >= LINES)
            continue;
        memmove(&CELL_AT(g_virt, dy, win->begx),
                &CELL_AT(g_virt, sy, win->begx),
                sizeof(cell_t) * (size_t)win->maxx);
    }
    if (n > 0) {
        for (int y = win->maxy - n; y < win->maxy; y++) {
            int gy = win->begy + y;
            if (gy < 0 || gy >= LINES)
                continue;
            for (int x = 0; x < win->maxx && win->begx + x < COLS; x++) {
                CELL_AT(g_virt, gy, win->begx + x).ch = ' ';
                CELL_AT(g_virt, gy, win->begx + x).attr = 0;
            }
        }
    }
    return wredrawln(win, 0, win->maxy);
}

int mvwaddch(WINDOW *win, int y, int x, unsigned int ch)
{
    if (wmove(win, y, x) == ERR)
        return ERR;
    return waddch(win, ch);
}

int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n)
{
    if (wmove(win, y, x) == ERR)
        return ERR;
    return waddnstr(win, str, n);
}

/*==========================
 *  颜色 / 鼠标 / term 桩
 *==========================*/

int start_color(void)            { return OK; }
int has_colors(void)             { return FALSE; }
int use_default_colors(void)     { return OK; }
int init_pair(int p, int f, int b)
{
    (void)p; (void)f; (void)b;
    return OK;
}

bool wenclose(const WINDOW *win, int y, int x)
{
    if (win == NULL)
        return false;
    return y >= win->begy && y < win->begy + win->maxy &&
           x >= win->begx && x < win->begx + win->maxx;
}

unsigned long mousemask(unsigned long newmask, unsigned long *oldmask)
{
    (void)newmask;
    if (oldmask != NULL)
        *oldmask = 0;
    return 0;
}

int mouseinterval(int interval)
{
    (void)interval;
    return OK;
}

int getmouse(void *event)
{
    (void)event;
    return ERR;
}

bool wmouse_trafo(const WINDOW *win, int *py, int *px, bool to_screen)
{
    (void)win;
    (void)py;
    (void)px;
    (void)to_screen;
    return false;
}

const char *tigetstr(const char *capname)
{
    (void)capname;
    return NULL;
}

int tigetnum(const char *capname)
{
    (void)capname;
    return -1;
}

const char *tgetstr(const char *id, char **area)
{
    (void)id;
    (void)area;
    return NULL;
}

int tigetflag(const char *capname)
{
    (void)capname;
    return -1;
}
