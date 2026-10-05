/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * nano_port/curses.h - mini-curses 公开接口（ncurses 子集）
 *
 * WHAT : GNU nano 8.4 实际用到的 curses 表面（~35 函数 + 键码 + 属性）
 * WHY  : NuttX 无 ncurses；垫片直接打 ANSI 转义到 stdout，
 *        对 /dev/cvbscon（AV 控制台，CSI 子集）与 VT100 串口终端通用
 * WHO  : deps/nano/src/*.c
 * WHERE: retro-ws/src/nuttx/common/nano_port/curses.h
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 实现 mini_curses.c（虚拟屏 diff 刷新 + termios raw +
 *        转义序列解码 + UTF-8 宽字符感知单元格）
 */

#ifndef NANO_PORT_CURSES_H
#define NANO_PORT_CURSES_H

/* ncurses 惯例：curses.h 自带 stdio/stdarg（nano 依赖此行为） */
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#undef  OK
#undef  ERR
#define OK    0
#define ERR  (-1)

typedef struct window_s
{
    int  begy, begx;        /* 窗口在屏上的原点 */
    int  maxy, maxx;        /* 窗口尺寸（行列） */
    int  cury, curx;        /* 窗口内光标 */
    unsigned char attr;     /* 当前写入属性（wattron/off 累积） */
    bool scroll_ok;
    bool keypad_ok;
    bool nodelay_ok;
    int  delay_tenths;      /* halfdelay 半秒十分位 */
}
WINDOW;

extern WINDOW *stdscr;
extern WINDOW *curscr;   /* 全屏重绘目标（full_refresh 用） */
extern int LINES;
extern int COLS;

/* 属性（bit 位，可叠加） */
#define A_NORMAL     0x00u
#define A_BOLD       0x01u
#define A_REVERSE    0x02u
#define A_STANDOUT   A_REVERSE
#define A_UNDERLINE  0x04u
#define A_DIM        A_BOLD
#define A_ATTRIBUTES 0xffu
#define A_CHARTEXT   0x00u

/* 颜色：单色控制台——对子为空实现 */
#define COLOR_PAIR(n)     0
#define PAIR_NUMBER(a)    0

/* 键码（与 ncurses 数值一致，nano 内部表依赖） */
#define KEY_CODE_YES  0400
#define KEY_DOWN      0402
#define KEY_UP        0403
#define KEY_LEFT      0404
#define KEY_RIGHT     0405
#define KEY_HOME      0406
#define KEY_BACKSPACE 0407
#define KEY_F0        0410
#define KEY_F(n)      (KEY_F0 + (n))
#define KEY_EOL       0517
#define KEY_SF        0520
#define KEY_SR        0521
#define KEY_NPAGE     0522
#define KEY_PPAGE     0523
#define KEY_ENTER     0527
#define KEY_A1        0534
#define KEY_A3        0535
#define KEY_B2        0536
#define KEY_C1        0537
#define KEY_C3        0540
#define KEY_BTAB      0541
#define KEY_BEG       0542
#define KEY_CANCEL    0543
#define KEY_END       0550
#define KEY_DC        0512
#define KEY_IC        0513
#define KEY_SBEG      0573
#define KEY_SCANCEL   0574
#define KEY_SDC       0600
#define KEY_SEND      0603
#define KEY_SHOME     0610
#define KEY_SIC       0611
#define KEY_SLEFT     0612
#define KEY_SNEXT     0615
#define KEY_SPREVIOUS 0617
#define KEY_SRIGHT    0623
#define KEY_SSUSPEND  0626
#define KEY_SUSPEND   0630
#define KEY_MOUSE     0631
#define KEY_RESIZE    0632
#define KEY_MAX       0777

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* ---- 生命周期 ---- */
WINDOW *initscr(void);
int     endwin(void);
WINDOW *newwin(int nlines, int ncols, int begy, int begx);
int     delwin(WINDOW *win);

/* ---- 输出 ---- */
int     wmove(WINDOW *win, int y, int x);
int     waddch(WINDOW *win, unsigned int ch);
int     waddstr(WINDOW *win, const char *str);
int     waddnstr(WINDOW *win, const char *str, int n);
int     wclrtoeol(WINDOW *win);
int     wclrtobot(WINDOW *win);
int     werase(WINDOW *win);
int     wclear(WINDOW *win);
int     winch(WINDOW *win);
int     wattroff(WINDOW *win, int attrs);
int     wattron(WINDOW *win, int attrs);
int     wattrset(WINDOW *win, int attrs);
int     scrollok(WINDOW *win, bool bf);
int     leaveok(WINDOW *win, bool bf);
int     clearok(WINDOW *win, bool bf);
int     touchwin(WINDOW *win);

/* ---- 刷新 ---- */
int     wrefresh(WINDOW *win);
int     wnoutrefresh(WINDOW *win);
int     doupdate(void);

/* ---- 输入 ---- */
int     wgetch(WINDOW *win);
int     ungetch(int ch);
int     keypad(WINDOW *win, bool bf);
int     nodelay(WINDOW *win, bool bf);
void    timeout(int tenths);
int     halfdelay(int tenths);
int     nocbreak(void);
int     cbreak(void);
int     noecho(void);
int     echo(void);
int     nl(void);
int     nonl(void);
int     raw(void);
int     noraw(void);
int     curs_set(int visibility);
int     flushinp(void);
int     typeahead(int fd);
int     meta(WINDOW *win, bool bf);
int     set_escdelay(int ms);
int     wscrl(WINDOW *win, int n);
int     wredrawln(WINDOW *win, int start, int n);
int     mvwaddch(WINDOW *win, int y, int x, unsigned int ch);
int     mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n);
int     isendwin(void);
int     beep(void);
int     define_key(const char *sequence, int symbol);
int     notimeout(WINDOW *win, bool bf);

/* ---- 颜色/杂项（空实现或保守实现）---- */
int     start_color(void);
int     has_colors(void);
int     use_default_colors(void);
int     init_pair(int pair, int f, int b);
bool    wenclose(const WINDOW *win, int y, int x);

/* ---- 鼠标（无鼠标——空实现）---- */
typedef struct mmask_t
{
    unsigned long mask;
}
mmask_t;

unsigned long mousemask(unsigned long newmask, unsigned long *oldmask);
int  mouseinterval(int interval);
int  getmouse(void *event);
bool wmouse_trafo(const WINDOW *win, int *py, int *px, bool to_screen);

/* ---- printf 族 + 延时 ---- */
int     mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...);
int     wprintw(WINDOW *win, const char *fmt, ...);
int     mvprintw(int y, int x, const char *fmt, ...);
int     printw(const char *fmt, ...);
int     napms(int ms);

/* ---- stdscr 便捷宏（nano 偶用）---- */
#define move(y, x)          wmove(stdscr, y, x)
#define addstr(s)           waddstr(stdscr, s)
#define mvaddstr(y, x, s)   (wmove(stdscr, y, x) == ERR ? ERR : waddstr(stdscr, s))
#define mvwaddstr(w, y, x, s) (wmove(w, y, x) == ERR ? ERR : waddstr(w, s))
#define refresh()           wrefresh(stdscr)
#define getch()             wgetch(stdscr)

#endif /* NANO_PORT_CURSES_H */
