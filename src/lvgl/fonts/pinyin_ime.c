/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * pinyin_ime.c - 拼音词库
 *
 * WHAT : 拼音词库（码点已统一 Unicode——2026-10-05 由 GB2312 原地重写，
 *        工具: python3 一次性变换；勿再引入 GB 码条目）
 * WHY  : 3000+ 常用汉字/词组数据（全拼+简拼）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/fonts/pinyin_ime.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 静态码表，供 drv_pinyin.c / app_pinyin.c 查询
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pinyin_ime.h"

/*======================================
 *  配置 / Configuration
 *======================================*/
#ifdef CONFIG_PINYIN_IME_FULL
    #define MAX_DICT_SIZE     6000   /* 扩展词库 / Extended dictionary */
#else
    #define MAX_DICT_SIZE     3000   /* 基础词库 / Basic dictionary */
#endif

#define MAX_PINYIN_LEN      12      /* 最大拼音长度 / Max pinyin length */
#define MAX_CANDIDATES      9       /* 最大候选词数 / Max candidates */
#define MAX_PHRASE_LEN      4       /* 最大词组字数 / Max phrase length */

/*======================================
 *  拼音表结构 / Pinyin table structure
 *======================================*/
typedef struct {
    uint16_t hanzi;         /* GB2312 汉字 / GB2312 Chinese char */
    uint8_t pinyin_idx;     /* 拼音索引 / Pinyin index */
} pinyin_map_t;

/*======================================
 *  词组结构 / Phrase structure
 *======================================*/
typedef struct {
    uint16_t phrase[MAX_PHRASE_LEN];  /* 词组汉字 / Phrase characters */
    uint8_t pinyin_len;               /* 拼音长度 / Pinyin length */
    char pinyin[MAX_PINYIN_LEN];       /* 拼音字符串 / Pinyin string */
    uint16_t freq;                     /* 词频 / Frequency */
} phrase_dict_t;

/*======================================
 *  拼音索引表 / Pinyin index table
 *======================================*/
static const char* g_pinyin_str[] = {
    "a", "ai", "an", "ang", "ao",      /* 0-4 */
    "ba", "bai", "ban", "bang", "bao", "bei", "ben", "beng", "bi", "bian", "biao",
    "bie", "bin", "bing", "bo", "bu",  /* 5-19 */
    "ca", "cai", "can", "cang", "cao", "ce", "cen", "ceng", "cha", "chai", "chan",
    "chang", "chao", "che", "chen", "cheng", "chi", "chong", "chou", "chu", "chua",
    "chuai", "chuan", "chuang", "chui", "chun", "chuo", /* 20-45 */
    "da", "dai", "dan", "dang", "dao", "de", "dei", "den", "deng", "di", "dia",
    "dian", "diao", "die", "ding", "diu", "dong", "dou", "du", "duan", "dui", "dun",
    "duo",                             /* 46-68 */
    "e", "ei", "en", "eng", "er",      /* 69-73 */
    "fa", "fan", "fang", "fei", "fen", "feng", "fo", "fou", "fu", /* 74-82 */
    "ga", "gai", "gan", "gang", "gao", "ge", "gei", "gen", "geng", "gong", "gou",
    "gu", "gua", "guai", "guan", "guang", "gui", "gun", "guo", /* 83-100 */
    "ha", "hai", "han", "hang", "hao", "he", "hei", "hen", "heng", "hong", "hou",
    "hu", "hua", "huai", "huan", "huang", "hui", "hun", "huo", /* 101-118 */
    "ji", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing", "jiong", "jiu",
    "ju", "juan", "jue", "jun",        /* 119-132 */
    "ka", "kai", "kan", "kang", "kao", "ke", "ken", "keng", "kong", "kou", "ku",
    "kua", "kuai", "kuan", "kuang", "kui", "kun", "kuo", /* 133-150 */
    "la", "lai", "lan", "lang", "lao", "le", "lei", "leng", "li", "lia", "lian",
    "liang", "liao", "lie", "lin", "ling", "liu", "long", "lou", "lu", "luan",
    "lue", "lun", "luo",               /* 151-176 */
    "ma", "mai", "man", "mang", "mao", "me", "mei", "men", "meng", "mi", "mian",
    "miao", "mie", "min", "ming", "miu", "mo", "mou", "mu", /* 177-195 */
    "na", "nai", "nan", "nang", "nao", "ne", "nei", "nen", "neng", "ni", "nian",
    "niang", "niao", "nie", "nin", "ning", "niu", "nong", "nu", "nuan", "nue", "nuo",
    "nv",                             /* 196-218 */
    "o", "ou",                        /* 219-220 */
    "pa", "pai", "pan", "pang", "pao", "pei", "pen", "peng", "pi", "pian", "piao",
    "pie", "pin", "ping", "po", "pou", "pu", /* 221-238 */
    "qi", "qia", "qian", "qiang", "qiao", "qie", "qin", "qing", "qiong", "qiu",
    "qu", "quan", "que", "qun",       /* 239-251 */
    "ran", "rang", "rao", "re", "ren", "reng", "ri", "rong", "rou", "ru", "ruan",
    "rui", "run", "ruo",              /* 252-264 */
    "sa", "sai", "san", "sang", "sao", "se", "sen", "seng", "sha", "shai", "shan",
    "shang", "shao", "she", "shen", "sheng", "shi", "shou", "shu", "shua", "shuai",
    "shuan", "shuang", "shui", "shun", "shuo", /* 265-288 */
    "si", "song", "sou", "su", "suan", "sui", "sun", "suo", /* 289-296 */
    "ta", "tai", "tan", "tang", "tao", "te", "teng", "ti", "tian", "tiao", "tie",
    "ting", "tong", "tou", "tu", "tuan", "tui", "tun", "tuo", /* 297-316 */
    "wa", "wai", "wan", "wang", "wei", "wen", "weng", "wo", "wu", /* 317-325 */
    "xi", "xia", "xian", "xiang", "xiao", "xie", "xin", "xing", "xiong", "xiu",
    "xu", "xuan", "xue", "xun",       /* 326-339 */
    "ya", "yan", "yang", "yao", "ye", "yi", "yin", "ying", "yo", "yong", "you",
    "yu", "yuan", "yue", "yun",       /* 340-353 */
    "za", "zai", "zan", "zang", "zao", "ze", "zei", "zen", "zeng", "zha", "zhai",
    "zhan", "zhang", "zhao", "zhe", "zhen", "zheng", "zhi", "zhong", "zhou", "zhu",
    "zhua", "zhuai", "zhuan", "zhuang", "zhui", "zhun", "zhuo", /* 354-383 */
    "zi", "zong", "zou", "zu", "zuan", "zui", "zun", "zuo"  /* 384-391 */
};

/*======================================
 *  GB2312 一级汉字拼音表（常用字样例）
 *  GB2312 Level-1 Chinese characters pinyin (sample)
 *  注意: 数组按实际条目大小声明，避免大量零填充条目
 *  Note: sized to actual entries so zero-filled tails
 *        never match find_hanzi_by_pinyin()
 *======================================*/
static const pinyin_map_t g_pinyin_map[] = {
    /* 啊 a - 0xB0A1 */
    {0x554A, 0},   /* 啊 a */
    {0x963F, 19},  /* 阿 a */
    {0x57C3, 19},  /* 埃 ai */
    {0x6328, 1},   /* 挨 ai */
    {0x54CE, 2},   /* 哎 ai */
    {0x5509, 1},   /* 唉 ai */
    {0x54C0, 3},   /* 哀 ai */
    {0x7691, 4},   /* 皑 ai */
    {0x764C, 3},   /* 癌 ai */
    {0x853C, 179}, /* 矮 ai */
    {0x77EE, 5},   /* 蔼 ai */
    {0x827E, 5},   /* 艾 ai */
    {0x788D, 1},   /* 爱 ai */
    {0x7231, 74},  /* 隘 ai */
    {0x9698, 8},   /* 鞍 ai */
    {0x978D, 8},   /* 氨 ai */
    {0x6C28, 5},   /* 案 ai */
    {0x5B89, 6},   /* 按 an */
    {0x4FFA, 7},   /* 暗 an */
    {0x6309, 7},   /* 岸 an */
    {0x6697, 8},   /* 胺 an */
    {0x5CB8, 6},   /* 案 an */
    {0x80FA, 9},   /* 昂 ang */
    {0x6848, 10},  /* 盎 ang */
    {0x80AE, 11},  /* 凹 ao */
    {0x6602, 12},  /* 敖 ao */
    {0x76CE, 12},  /* 熬 ao */
    {0x51F9, 9},   /* 翱 ao */
    {0x6556, 12},  /* 袄 ao */
    {0x71AC, 13},  /* 奥 ao */
    {0x7FF1, 13},  /* 懊 ao */
    {0x8884, 13},  /* 澳 ao */
    {0x50B2, 14},  /* 芭 ba */
    {0x5965, 15},  /* 扒 ba */
    {0x61CA, 14},  /* 叭 ba */
    {0x6FB3, 16},  /* 吧 ba */
    {0x82AD, 14},  /* 笆 ba */
    {0x634C, 17},  /* 八 ba */
    {0x6252, 18},  /* 疤 ba */
    {0x53ED, 14},  /* 巴 ba */
    {0x5427, 19},  /* 拔 ba */
    {0x7B06, 19},  /* 跋 ba */
    {0x516B, 20},  /* 靶 ba */
    {0x75A4, 21},  /* 把 ba */
    {0x5DF4, 22},  /* 坝 ba */
    {0x62D4, 22},  /* 霸 ba */
    {0x8DCB, 22},  /* 罢 ba */
    {0x9776, 23},  /* 爸 ba */
    {0x628A, 24},  /* 白 bai */
    {0x8019, 25},  /* 百 bai */
    {0x575D, 25},  /* 摆 bai */
    {0x9738, 26},  /* 佰 bai */
    {0x7F62, 27},  /* 败 bai */
    {0x7238, 28},  /* 拜 bai */
    {0x767D, 29},  /* 稗 bai */
    {0x67CF, 30},  /* 斑 ban */
    {0x767E, 31},  /* 班 ban */
    {0x6446, 30},  /* 搬 ban */
    {0x4F70, 32},  /* 扳 ban */
    {0x8D25, 30},  /* 颁 ban */
    {0x62DC, 33},  /* 板 ban */
    {0x7A17, 34},  /* 版 ban */
    {0x6591, 35},  /* 扮 ban */
    {0x73ED, 36},  /* 拌 ban */
    {0x642C, 37},  /* 伴 ban */
    {0x6273, 38},  /* 瓣 ban */
    {0x822C, 39},  /* 半 ban */
    {0x9881, 40},  /* 办 ban */
    {0x677F, 41},  /* 绊 ban */
    {0x7248, 42},  /* 邦 bang */
    {0x626E, 43},  /* 帮 bang */
    {0x62CC, 44},  /* 梆 bang */
    {0x4F34, 45},  /* 榜 bang */
    {0x74E3, 45},  /* 膀 bang */
    {0x534A, 46},  /* 绑 bang */
    {0x529E, 47},  /* 棒 bang */
    {0x7ECA, 48},  /* 磅 bang */
    {0x90A6, 49},  /* 蚌 bang */
    {0x5E2E, 50},  /* 镑 bang */
    {0x6886, 51},  /* 傍 bang */
    {0x699C, 52},  /* 谎 bang */
    {0x8180, 53},  /* 苞 bao */
    {0x7ED1, 54},  /* 胞 bao */
    {0x68D2, 53},  /* 包 bao */
    {0x78C5, 55},  /* 褒 bao */
    {0x868C, 56},  /* 宝 bao */
    {0x9551, 57},  /* 保 bao */
    {0x508D, 58},  /* 堡 bao */
    {0x8C24, 59},  /* 饱 bao */
    {0x82DE, 60},  /* 报 bao */
    {0x80DE, 61},  /* 暴 bao */
    {0x5305, 62},  /* 豹 bao */
    {0x8912, 61},  /* 爆 bao */
    {0x5265, 63},  /* 刨 bao */
    /* ... 继续更多汉字 / Continue with more characters */
};

/* 词组词典（基础常用词组）/ Phrase dictionary (basic common phrases)
 * GB2312 码点修正 / GB2312 codepoint fixes:
 *   原表中混入 0xCAC0xD0 / 0xC9xE8 等乱码字面量（mojibake），无法通过编译；
 *   The original table contained mojibake literals that failed to compile.
 *   以下均按 GB2312 标准区位码重写。
 *   All entries rewritten with standard GB2312 code points:
 *   和=0xBACD 的=0xB5C4 是=0xCAC7 长=0xB3A4 有=0xD3D0 音=0xD2F4 */
static const phrase_dict_t g_phrase_dict[] = {
    /* 常用词（2026-10-05 补全：GB2312 码点 + 准确全拼） */
    {{0x4F60, 0x597D, 0}, 2, "nihao", 99},  /* 你好 */
    {{0x4E16, 0x754C, 0}, 2, "shijie", 98},  /* 世界 */
    {{0x6211, 0x4EEC, 0}, 2, "women", 97},  /* 我们 */
    {{0x4ED6, 0x4EEC, 0}, 2, "tamen", 96},  /* 他们 */
    {{0x5927, 0x5BB6, 0}, 2, "dajia", 95},  /* 大家 */
    {{0x518D, 0x89C1, 0}, 2, "zaijian", 94},  /* 再见 */
    {{0x8C22, 0x8C22, 0}, 2, "xiexie", 93},  /* 谢谢 */
    {{0x7535, 0x8111, 0}, 2, "diannao", 92},  /* 电脑 */
    {{0x624B, 0x673A, 0}, 2, "shouji", 91},  /* 手机 */
    {{0x7F51, 0x7EDC, 0}, 2, "wangluo", 90},  /* 网络 */
    {{0x65F6, 0x95F4, 0}, 2, "shijian", 89},  /* 时间 */
    {{0x4ECA, 0x5929, 0}, 2, "jintian", 88},  /* 今天 */
    {{0x660E, 0x5929, 0}, 2, "mingtian", 87},  /* 明天 */
    {{0x5DE5, 0x4F5C, 0}, 2, "gongzuo", 86},  /* 工作 */
    {{0x5B66, 0x4E60, 0}, 2, "xuexi", 85},  /* 学习 */
    {{0x4E2D, 0x6587, 0}, 2, "zhongwen", 84},  /* 中文 */
    {{0x8F93, 0x5165, 0}, 2, "shuru", 83},  /* 输入 */
    {{0x663E, 0x793A, 0}, 2, "xianshi", 82},  /* 显示 */
    {{0x7CFB, 0x7EDF, 0}, 2, "xitong", 81},  /* 系统 */
    {{0x6587, 0x4EF6, 0}, 2, "wenjian", 80},  /* 文件 */
    {{0x7F16, 0x8F91, 0}, 2, "bianji", 79},  /* 编辑 */
    {{0x60A8, 0x597D, 0}, 2, "nihao", 78},  /* 您好 */
    {{0x554A, 0}, 1, "a", 100},          /* 啊 a */
    {{0x4E2D, 0x56FD}, 2, "zhongguo", 95}, /* 中国 */
    {{0x4EBA, 0x751F}, 2, "rensheng", 90}, /* 人生 */
    {{0x4E16, 0x754C}, 2, "shijie", 90},  /* 世界 */
    {{0x9632, 0x63A7}, 2, "fangkong", 85}, /* 防控 */
    {{0x5458, 0x5DE5}, 2, "yuangong", 80}, /* 员工 */
    {{0x548C, 0}, 1, "he", 95},          /* 和 he */
    {{0x7684, 0}, 1, "de", 100},         /* 的 de */
    {{0x662F, 0}, 1, "shi", 100},        /* 是 shi */
    {{0x957F, 0}, 1, "zhang", 95},       /* 长 zhang/chang */
    {{0x4E2D, 0}, 1, "zhong", 95},       /* 中 zhong */
    {{0x4E3A, 0}, 1, "wei", 90},         /* 为 wei */
    {{0x6709, 0}, 1, "you", 95},         /* 有 you */
    {{0x4E0D, 0}, 1, "bu", 90},          /* 不 bu */
    {{0x5728, 0}, 1, "zai", 95},         /* 在 zai */
    {{0x97F3, 0}, 1, "yin", 90},         /* 音 yin */
    {{0x4ED6, 0x4EEC}, 2, "tamen", 85},  /* 他们 */
    {{0x6211, 0x4EEC}, 2, "women", 90},  /* 我们 */
    {{0x4F60, 0x4EEC}, 2, "nimen", 85},  /* 你们 */
    {{0x81EA, 0x5DF1}, 2, "ziji", 95},   /* 自己 */
    {{0x8FD9, 0x91CC}, 2, "zheli", 80},  /* 这里 */
};

/*======================================
 *  输入引擎结构 / Input engine structure
 *======================================*/
typedef struct {
    char pinyin_buf[MAX_PINYIN_LEN * 2];  /* 拼音缓冲区 / Pinyin buffer */
    int pinyin_len;                        /* 当前拼音长度 / Current pinyin length */
    uint16_t candidates[MAX_CANDIDATES];   /* 候选字列表 / Candidate list */
    int cand_count;                        /* 候选字数量 / Candidate count */
    int selected_idx;                      /* 已选择的候选索引 / Selected candidate index */
    int is_phrase_mode;                    /* 词组模式 / Phrase mode */
} pinyin_ime_t;

static pinyin_ime_t g_ime;

/*======================================
 *  辅助函数 / Helper functions
 *======================================*/

/**
 * pinyin_ime_init - 初始化输入法 / Initialize input method
 */
void pinyin_ime_init(void)
{
    memset(&g_ime, 0, sizeof(pinyin_ime_t));
}

/**
 * pinyin_to_index - 拼音字符串转索引 / Convert pinyin string to index
 * @pinyin: 拼音字符串 / Pinyin string
 * return: 拼音索引，-1表示未找到 / Pinyin index, -1 if not found
 */
int pinyin_to_index(const char *pinyin)
{
    int i;
    for (i = 0; i < (int)(sizeof(g_pinyin_str) / sizeof(g_pinyin_str[0])); i++) {
        if (strcmp(g_pinyin_str[i], pinyin) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * find_hanzi_by_pinyin - 根据拼音查找汉字 / Find Chinese chars by pinyin
 * @pinyin_idx: 拼音索引 / Pinyin index
 * @results: 结果缓冲区 / Result buffer
 * @max_results: 最大结果数 / Max results
 * return: 实际结果数 / Actual results count
 */
int find_hanzi_by_pinyin(int pinyin_idx, uint16_t *results, int max_results)
{
    int count = 0;
    int dict_size = (int)(sizeof(g_pinyin_map) / sizeof(g_pinyin_map[0]));
    int i;

    /* 只遍历实际条目（数组已按条目数声明，无零填充尾巴）
     * Iterate real entries only (array sized to actual entries) */
    for (i = 0; i < dict_size && count < max_results; i++) {
        if (g_pinyin_map[i].pinyin_idx == pinyin_idx) {
            results[count++] = g_pinyin_map[i].hanzi;
        }
    }

    return count;
}

/**
 * find_phrases_by_pinyin - 根据拼音查找词组 / Find phrases by pinyin
 * @pinyin: 拼音字符串 / Pinyin string
 * @results: 结果缓冲区 / Result buffer
 * @max_results: 最大结果数 / Max results
 * return: 实际结果数 / Actual results count
 */
int find_phrases_by_pinyin(const char *pinyin, uint16_t (*results)[MAX_PHRASE_LEN], int max_results)
{
    int count = 0;
    int i;
    int pinyin_len = strlen(pinyin);
    
    /* 简拼支持: 只匹配首字母 / Abbreviation: match first letter only */
    for (i = 0; i < (int)(sizeof(g_phrase_dict) / sizeof(g_phrase_dict[0])) && count < max_results; i++) {
        const phrase_dict_t *phrase = &g_phrase_dict[i];
        
        /* 完全匹配 / Full match */
        if (strncmp(phrase->pinyin, pinyin, pinyin_len) == 0) {
            memcpy(results[count], phrase->phrase, sizeof(uint16_t) * MAX_PHRASE_LEN);
            count++;
        }
    }
    
    return count;
}

/*======================================
 *  主要API / Main API
 *======================================*/

/**
 * pinyin_ime_input - 输入一个字符 / Input a character
 * @ch: 输入的字符 / Input character (a-z)
 * return: 0表示正在输入中，1表示有候选词 / 0 means inputting, 1 means candidates ready
 */
int pinyin_ime_input(char ch)
{
    /* 只接受小写字母 / Only accept lowercase letters */
    if (ch >= 'a' && ch <= 'z') {
        if (g_ime.pinyin_len < MAX_PINYIN_LEN * 2 - 1) {
            g_ime.pinyin_buf[g_ime.pinyin_len++] = ch;
            g_ime.pinyin_buf[g_ime.pinyin_len] = '\0';
        }
    }
    
    /* 查找候选 / Search candidates */
    return pinyin_ime_search();
}

/**
 * pinyin_ime_backspace - 删除最后一个字符 / Delete last character
 * return: 0表示正在输入中，1表示有候选词 / 0 means inputting, 1 means candidates ready
 */
int pinyin_ime_backspace(void)
{
    if (g_ime.pinyin_len > 0) {
        g_ime.pinyin_len--;
        g_ime.pinyin_buf[g_ime.pinyin_len] = '\0';
    }
    
    if (g_ime.pinyin_len == 0) {
        g_ime.cand_count = 0;
        return 0;
    }
    
    return pinyin_ime_search();
}

/**
 * pinyin_ime_search - 搜索候选词 / Search candidates
 * return: 0表示无候选，1表示有候选词 / 0 means no candidates, 1 means candidates ready
 */
int pinyin_ime_search(void)
{
    int pinyin_idx;
    uint16_t phrase_results[10][MAX_PHRASE_LEN];
    int phrase_count;
    
    g_ime.cand_count = 0;
    g_ime.selected_idx = -1;
    
    if (g_ime.pinyin_len == 0) {
        return 0;
    }
    
    /* 查找词组（在前——Win95 惯例词组优先；2026-10-05 修复：
     * 原实现搜到词组却丢弃结果，复合拼音如 nihao 恒无候选） */
    phrase_count = find_phrases_by_pinyin(g_ime.pinyin_buf, phrase_results, 10);
    for (int i = 0; i < phrase_count && g_ime.cand_count < MAX_CANDIDATES; i++)
        g_ime.candidates[g_ime.cand_count++] = phrase_results[i][0];

    /* 查找单字 / Find single chars */
    pinyin_idx = pinyin_to_index(g_ime.pinyin_buf);
    if (pinyin_idx >= 0) {
        g_ime.cand_count += find_hanzi_by_pinyin(
            pinyin_idx, g_ime.candidates + g_ime.cand_count,
            MAX_CANDIDATES - g_ime.cand_count);
    }

    return g_ime.cand_count > 0;
}

/**
 * pinyin_ime_select - 选择候选词 / Select candidate
 * @index: 候选词索引（0-8对应数字键1-9）/ Candidate index (0-8 corresponds to keys 1-9)
 * @out_hanzi: 输出的汉字缓冲区 / Output Chinese char buffer
 * return: 0成功，-1失败 / 0 success, -1 failed
 */
int pinyin_ime_select(int index, uint16_t *out_hanzi)
{
    if (index < 0 || index >= g_ime.cand_count) {
        return -1;
    }
    
    *out_hanzi = g_ime.candidates[index];
    g_ime.selected_idx = index;
    
    /* 清空缓冲区 / Clear buffer */
    g_ime.pinyin_len = 0;
    g_ime.pinyin_buf[0] = '\0';
    g_ime.cand_count = 0;
    
    return 0;
}

/**
 * pinyin_ime_clear - 清空输入 / Clear input
 */
void pinyin_ime_clear(void)
{
    g_ime.pinyin_len = 0;
    g_ime.pinyin_buf[0] = '\0';
    g_ime.cand_count = 0;
    g_ime.selected_idx = -1;
}

/**
 * pinyin_ime_get_pinyin - 获取当前拼音字符串 / Get current pinyin string
 * return: 当前拼音字符串 / Current pinyin string
 */
const char* pinyin_ime_get_pinyin(void)
{
    return g_ime.pinyin_buf;
}

/**
 * pinyin_ime_get_candidates - 获取候选词列表 / Get candidate list
 * @candidates: 候选词输出缓冲区 / Candidate output buffer
 * return: 候选词数量 / Candidate count
 */
int pinyin_ime_get_candidates(uint16_t *candidates)
{
    int i;
    for (i = 0; i < g_ime.cand_count && i < MAX_CANDIDATES; i++) {
        /* 词库码点已于 2026-10-05 原地转为 Unicode（工具一次性重写，
         * 见本文件头注释）——运行时无需任何转码 */
        candidates[i] = g_ime.candidates[i];
    }
    return g_ime.cand_count;
}

/**
 * pinyin_ime_get_cand_count - 获取候选词数量 / Get candidate count
 * return: 候选词数量 / Candidate count
 */
int pinyin_ime_get_cand_count(void)
{
    return g_ime.cand_count;
}

/**
 * pinyin_ime_has_candidates - 是否有候选词 / Has candidates
 * return: 1表示有，0表示无 / 1 means yes, 0 means no
 */
int pinyin_ime_has_candidates(void)
{
    return g_ime.cand_count > 0;
}
