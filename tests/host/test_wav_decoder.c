/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * test_wav_decoder.c - WAV 解码器宿主单测
 *
 * WHAT : wav_open/wav_read/wav_convert_to_stereo16/wav_seek/wav_get_info
 *        的正常路径、蜕变关系与拒绝路径机器化验证
 * WHY  : 应用/系统分离重构（2026-10-06）后 wav_decoder.c 随 player
 *        .rpk 包构建、不再链进固件——需要独立宿主测试守护其行为
 * WHO  : tests/host/run_all.sh 调度
 * WHERE: retro-ws/tests/host/test_wav_decoder.c（被测件
 *        retro-ws/src/lvgl/audio/wav_decoder.c）
 * WHEN : 2026-10-06 新增
 * HOW  : 内存构造最小合法 WAV 写临时文件后整文件喂入（解码器为
 *        FILE* 流式 API）；按 ai-code-testing 分层覆盖——
 *        L3 单元/蜕变：头字段解析断言、数据字节守恒
 *        （Σwav_read == data_size == samples×channels×bits/8）、
 *        帧守恒（out_samples == data_size/(ch×bits/8)）、
 *        输出字节守恒（out_bytes == out_samples×4）、seek 对合性
 *        （已读+剩余 == data_size）；差分：8bit 偏移转换对朴素
 *        参考实现逐值比对；L4 拒绝路径：坏魔数/截断头/缺 data
 *        chunk/非 PCM 压缩格式/位深/声道数/block_align=0 → 精确
 *        错误码（发现并修复过截断头死循环：fseek 清 EOF 标志）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdbool.h>

#include "test_framework.h"

/*==== 被测 API 声明（wav_decoder.c 无独立头文件，与 app_player.c
 * 的 extern 声明方式一致 / DUT has no header; mirror app_player.c）====*/

extern int    wav_open(const char *path);
extern int    wav_read(void *buf, int len);
extern int    wav_convert_to_stereo16(const void *in_buf, void *out_buf,
                                      int in_len);
extern int    wav_seek(float percent);
extern void   wav_get_info(uint32_t *sample_rate, uint16_t *channels,
                           uint16_t *bits_per_sample, uint32_t *data_size,
                           uint32_t *duration_ms);
extern float  wav_get_position(void);
extern const char *wav_get_filename(void);
extern bool   wav_is_open(void);
extern bool   wav_at_end(void);
extern void   wav_close(void);
extern void   wav_get_duration_str(char *buf, int bufsiz);
extern void   wav_get_current_time_str(char *buf, int bufsiz);

/*==== WAV 样本构造器 / WAV fixture builder ====*/

static char g_dir[128];   /* 临时样本目录 / temp fixture dir */

static void wr16(FILE *f, unsigned v)
{
    uint8_t b[2] = { (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff) };
    fwrite(b, 1, 2, f);
}

static void wr32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8),
                     (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

/*
 * make_wav - 构造一个最小合法/故意畸形的 WAV 样本
 * WHAT : 按 RIFF(WAVE) -> [JUNK(奇数长)] -> fmt(16/18 字节) -> data
 *        顺序写文件；fmt/data 字段全部可注入
 * WHY  : 测试需要精确控制每个头字段以覆盖解析分支与拒绝路径
 * HOW  : 各参数直接写入对应 chunk 字段；返回前 fclose
 *   path        - 输出文件路径
 *   fmt_tag     - 音频格式_tag（1=PCM，6=A-law，3=float 用作拒绝样本）
 *   channels    - 声道数（1/2 合法，其余拒绝）
 *   rate        - 采样率 Hz
 *   bits        - 位深（8/16 合法，其余拒绝）
 *   block_align - 块对齐字节（0 拒绝；合法值 = ch×bits/8）
 *   data        - PCM 数据（NULL 时写 0 字节数据段）
 *   data_len    - PCM 数据长度
 *   junk        - 是否在 fmt 前插入 5 字节奇数长 JUNK chunk
 *   fmt18       - fmt chunk 是否带 cbSize 扩展字段（chunk_size=18）
 */
static void make_wav(const char *path, unsigned fmt_tag, unsigned channels,
                     unsigned rate, unsigned bits, unsigned block_align,
                     const void *data, unsigned data_len, int junk, int fmt18)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "fixture: cannot write %s\n", path);
        exit(1);
    }

    uint32_t fmt_size = fmt18 ? 18 : 16;
    uint32_t body = (junk ? 8 + 5 + 1 : 0) + 8 + fmt_size + 8 + data_len;

    fwrite("RIFF", 1, 4, f);
    wr32(f, body);                 /* file size - 8 */
    fwrite("WAVE", 1, 4, f);

    if (junk) {
        fwrite("JUNK", 1, 4, f);
        wr32(f, 5);                /* 奇数长 + 1 字节 word 对齐垫 */
        fwrite("12345", 1, 5, f);
        fputc(0, f);
    }

    fwrite("fmt ", 1, 4, f);
    wr32(f, fmt_size);
    wr16(f, fmt_tag);              /* audio_format */
    wr16(f, channels);
    wr32(f, rate);
    wr32(f, rate * channels * bits / 8);  /* byte_rate（守恒式） */
    wr16(f, block_align);
    wr16(f, bits);
    if (fmt18)
        wr16(f, 0);                /* cbSize 扩展 */

    fwrite("data", 1, 4, f);
    wr32(f, data_len);
    if (data && data_len)
        fwrite(data, 1, data_len, f);

    fclose(f);
}

static void fixture_path(char *buf, int bufsiz, const char *name)
{
    snprintf(buf, bufsiz, "%s/%s", g_dir, name);
}

/*
 * read_all - 分块读完整个 data chunk 并校验字节守恒
 * WHAT : 以 7 字节奇数块长反复 wav_read 直到 EOF
 * WHY  : 蜕变关系 MR1——Σ分块读取 == data_size，且内容逐字节等于源
 * HOW  : 返回累计字节数；内容写入 out（可为 NULL 跳过比对）
 */
static int read_all(uint8_t *out, int out_cap, const uint8_t *src,
                    uint32_t data_size)
{
    uint8_t chunk[7];
    uint32_t total = 0;
    int n;

    while ((n = wav_read(chunk, sizeof(chunk))) > 0) {
        CHECK(n <= (int)sizeof(chunk));
        if (out && total + (uint32_t)n <= (uint32_t)out_cap) {
            memcpy(out + total, chunk, n);
            if (src)
                CHECK(memcmp(chunk, src + total, n) == 0);
        }
        total += (uint32_t)n;
    }

    /* MR1 数据字节守恒 / byte conservation */
    CHECK_EQ_INT(total, (long)data_size);
    CHECK_EQ_INT(wav_read(chunk, sizeof(chunk)), 0);   /* EOF 再读为 0 */
    CHECK(wav_at_end());
    return (int)total;
}

/*==== 1. 正常路径 A：48kHz/16bit/单声道 / happy path A ====*/

static void test_mono16_48k(void)
{
    char path[192];
    enum { FRAMES_A = 32 };
    static const uint16_t samples[FRAMES_A] = {
        0x0000, 0x7FFF, 0x8000, 0xFFFF, 0x1234, 0xEDCB, 0x00FF, 0xFF00,
        0x0001, 0x7FFE, 0x8001, 0xFFFE, 0x4321, 0xBEEF, 0x0F0F, 0xF0F0,
        0x5555, 0xAAAA, 0x1111, 0xEEEE, 0x2222, 0xDDDD, 0x3333, 0xCCCC,
        0x6666, 0x9999, 0x7777, 0x8888, 0x0A0A, 0xF5F5, 0x3C3C, 0xC3C3,
    };
    uint8_t pcm[FRAMES_A * 2];
    uint8_t got[FRAMES_A * 2];
    int16_t stereo[FRAMES_A * 2];
    uint32_t sr, dsz, dur;
    uint16_t ch, bps;

    for (int i = 0; i < FRAMES_A; i++) {
        pcm[i * 2] = (uint8_t)(samples[i] & 0xff);
        pcm[i * 2 + 1] = (uint8_t)(samples[i] >> 8);
    }

    fixture_path(path, sizeof(path), "mono16_48k.wav");
    make_wav(path, 1, 1, 48000, 16, 2, pcm, sizeof(pcm), 0, 0);

    CHECK_EQ_INT(wav_open(path), 0);
    CHECK(wav_is_open());
    CHECK_STR_EQ(wav_get_filename(), path);

    /* 头字段解析断言 / header field assertions */
    wav_get_info(&sr, &ch, &bps, &dsz, &dur);
    CHECK_EQ_INT(sr, 48000);
    CHECK_EQ_INT(ch, 1);
    CHECK_EQ_INT(bps, 16);
    CHECK_EQ_INT(dsz, (long)sizeof(pcm));
    /* 蜕变：data_size == samples×channels×bits/8 */
    CHECK_EQ_INT(dsz, (long)(FRAMES_A * 1 * 16 / 8));

    /* MR1 字节守恒 + 内容逐字节一致（7 字节奇数块） */
    CHECK_EQ_INT(read_all(got, sizeof(got), pcm, sizeof(pcm)),
                 (long)sizeof(pcm));
    CHECK(memcmp(got, pcm, sizeof(pcm)) == 0);
    CHECK(wav_get_position() > 0.999f && wav_get_position() <= 1.0f);

    /* mono16 -> stereo16：帧守恒 + L==R 复制 + 输出字节守恒 */
    memset(stereo, 0xA5, sizeof(stereo));
    CHECK_EQ_INT(wav_convert_to_stereo16(pcm, stereo, sizeof(pcm)),
                 FRAMES_A);
    for (int i = 0; i < FRAMES_A; i++) {
        CHECK_EQ_INT(stereo[i * 2], (long)(int16_t)samples[i]);      /* L */
        CHECK_EQ_INT(stereo[i * 2 + 1], (long)(int16_t)samples[i]);  /* R */
    }
    /* MR3 输出字节守恒：out_samples×4（2ch×16bit）不超过输出缓冲 */
    CHECK((long)FRAMES_A * 4 <= (long)sizeof(stereo));

    /* 非法参数守护 / argument guard */
    CHECK_EQ_INT(wav_convert_to_stereo16(NULL, stereo, 10), 0);
    CHECK_EQ_INT(wav_convert_to_stereo16(pcm, NULL, 10), 0);
    CHECK_EQ_INT(wav_convert_to_stereo16(pcm, stereo, 0), 0);
    CHECK_EQ_INT(wav_convert_to_stereo16(pcm, stereo, -5), 0);

    wav_close();
    CHECK(!wav_is_open());

    /* close 后状态机归零 / post-close state */
    CHECK_EQ_INT(wav_read(got, sizeof(got)), -ENODEV);
    CHECK_EQ_INT(wav_seek(0.5f), -ENODEV);

    /* 二次 close 幂等 / double close is safe */
    wav_close();
    CHECK(!wav_is_open());
}

/*==== 2. 正常路径 B：22.05kHz/8bit/立体声 + JUNK/fmt18 变体 ====*/

static void test_stereo8_22k(void)
{
    char path[192];
    enum { FRAMES_B = 22050 };            /* 2 秒 @ 44100 B/s */
    static uint8_t pcm[FRAMES_B * 2];
    uint8_t got[FRAMES_B * 2];
    int16_t stereo[FRAMES_B * 2];
    uint32_t sr, dsz, dur;
    uint16_t ch, bps;
    char buf[16];

    /* 伪随机但确定的 PCM（8bit 无符号，中心 128） */
    unsigned seed = 12345;
    for (size_t i = 0; i < sizeof(pcm); i++) {
        seed = seed * 1103515245 + 12345;
        pcm[i] = (uint8_t)(seed >> 16);
    }

    fixture_path(path, sizeof(path), "stereo8_22k.wav");
    make_wav(path, 1, 2, 22050, 8, 2, pcm, sizeof(pcm), 1, 1);

    CHECK_EQ_INT(wav_open(path), 0);

    wav_get_info(&sr, &ch, &bps, &dsz, &dur);
    CHECK_EQ_INT(sr, 22050);
    CHECK_EQ_INT(ch, 2);
    CHECK_EQ_INT(bps, 8);
    CHECK_EQ_INT(dsz, (long)sizeof(pcm));
    /* 蜕变：data_size == samples×channels×bits/8 */
    CHECK_EQ_INT(dsz, (long)(FRAMES_B * 2 * 8 / 8));
    /* 时长守恒：duration_ms == data_size×1000/byte_rate（44100B÷44100B/s=1s） */
    CHECK_EQ_INT(dur, 1000);
    wav_get_duration_str(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "00:01");

    /* MR1 字节守恒（JUNK 奇数跳垫 + fmt18 对齐分支均被穿过） */
    CHECK_EQ_INT(read_all(got, sizeof(got), pcm, sizeof(pcm)),
                 (long)sizeof(pcm));

    /* 8bit 立体声转换：差分对拍——朴素参考实现逐值比对 */
    CHECK_EQ_INT(wav_convert_to_stereo16(pcm, stereo, sizeof(pcm)),
                 FRAMES_B);
    for (int i = 0; i < FRAMES_B; i++) {
        int16_t l = (int16_t)(((int16_t)pcm[i * 2] - 128) * 256);
        int16_t r = (int16_t)(((int16_t)pcm[i * 2 + 1] - 128) * 256);
        CHECK_EQ_INT(stereo[i * 2], l);
        CHECK_EQ_INT(stereo[i * 2 + 1], r);
    }

    /* MR4 seek 对合性：已读 + 剩余 == data_size */
    CHECK_EQ_INT(wav_seek(0.5f), 0);
    float pos = wav_get_position();
    CHECK(pos > 0.49f && pos < 0.51f);
    CHECK_EQ_INT(read_all(NULL, 0, NULL, sizeof(pcm) / 2),
                 (long)(sizeof(pcm) / 2));       /* 剩余一半 */

    wav_seek(0.0f);
    wav_get_current_time_str(buf, sizeof(buf));
    CHECK_STR_EQ(buf, "00:00");
    CHECK_EQ_INT(read_all(NULL, 0, NULL, sizeof(pcm)),
                 (long)sizeof(pcm));             /* 回头再读全量 */

    /* 边界 clamp：越界百分比不炸 / out-of-range clamp */
    CHECK_EQ_INT(wav_seek(-0.5f), 0);
    CHECK_EQ_INT(wav_seek(2.0f), 0);
    CHECK(wav_at_end());

    wav_close();
}

/*==== 3. 拒绝路径 / rejection paths ====*/

static void test_reject(void)
{
    char path[192];
    uint8_t tiny[8] = { 0 };

    /* 坏 RIFF 魔数 / bad RIFF magic */
    fixture_path(path, sizeof(path), "bad_riff.wav");
    make_wav(path, 1, 1, 48000, 16, 2, tiny, sizeof(tiny), 0, 0);
    /* 覆写魔数 / patch magic bytes */
    {
        FILE *f = fopen(path, "r+b");
        fwrite("RIFX", 1, 4, f);
        fclose(f);
    }
    CHECK_EQ_INT(wav_open(path), -EINVAL);
    CHECK(!wav_is_open());

    /* 坏 WAVE 魔数 / bad WAVE magic */
    fixture_path(path, sizeof(path), "bad_wave.wav");
    make_wav(path, 1, 1, 48000, 16, 2, tiny, sizeof(tiny), 0, 0);
    {
        FILE *f = fopen(path, "r+b");
        fseek(f, 8, SEEK_SET);
        fwrite("WAVX", 1, 4, f);
        fclose(f);
    }
    CHECK_EQ_INT(wav_open(path), -EINVAL);
    CHECK(!wav_is_open());

    /* 截断头：只有 12 字节 RIFF/WAVE 前缀 / truncated header */
    fixture_path(path, sizeof(path), "trunc_head.wav");
    {
        FILE *f = fopen(path, "wb");
        fwrite("RIFF", 1, 4, f);
        wr32(f, 4);
        fwrite("WAVE", 1, 4, f);
        fclose(f);
    }
    CHECK_EQ_INT(wav_open(path), -EINVAL);
    CHECK(!wav_is_open());

    /* 截断：有 fmt 无 data chunk（曾死循环，回归守护） */
    fixture_path(path, sizeof(path), "no_data.wav");
    {
        FILE *f = fopen(path, "wb");
        fwrite("RIFF", 1, 4, f);
        wr32(f, 28);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        wr32(f, 16);
        wr16(f, 1); wr16(f, 1); wr32(f, 48000);
        wr32(f, 96000); wr16(f, 2); wr16(f, 16);
        fclose(f);
    }
    CHECK_EQ_INT(wav_open(path), -EINVAL);
    CHECK(!wav_is_open());

    /* 非 PCM 压缩格式：A-law / μ-law / IEEE float → -ENOSYS */
    fixture_path(path, sizeof(path), "alaw.wav");
    make_wav(path, 6, 1, 8000, 8, 1, tiny, sizeof(tiny), 0, 0);
    CHECK_EQ_INT(wav_open(path), -ENOSYS);

    fixture_path(path, sizeof(path), "mulaw.wav");
    make_wav(path, 7, 1, 8000, 8, 1, tiny, sizeof(tiny), 0, 0);
    CHECK_EQ_INT(wav_open(path), -ENOSYS);

    fixture_path(path, sizeof(path), "float.wav");
    make_wav(path, 3, 2, 44100, 32, 8, tiny, sizeof(tiny), 0, 0);
    CHECK_EQ_INT(wav_open(path), -ENOSYS);

    /* 不支持位深 24bit → -ENOSYS */
    fixture_path(path, sizeof(path), "bits24.wav");
    make_wav(path, 1, 2, 48000, 24, 6, tiny, sizeof(tiny), 0, 0);
    CHECK_EQ_INT(wav_open(path), -ENOSYS);

    /* 不支持声道数 4 → -ENOSYS */
    fixture_path(path, sizeof(path), "quad.wav");
    make_wav(path, 1, 4, 48000, 16, 8, tiny, sizeof(tiny), 0, 0);
    CHECK_EQ_INT(wav_open(path), -ENOSYS);

    /* block_align=0（wav_seek 除零风险）→ -EINVAL */
    fixture_path(path, sizeof(path), "align0.wav");
    make_wav(path, 1, 1, 48000, 16, 0, tiny, sizeof(tiny), 0, 0);
    CHECK_EQ_INT(wav_open(path), -EINVAL);

    /* 不存在的文件 / nonexistent path */
    fixture_path(path, sizeof(path), "ghost.wav");
    CHECK_EQ_INT(wav_open(path), -ENOENT);

    /* NULL 路径 / NULL path */
    CHECK_EQ_INT(wav_open(NULL), -EINVAL);

    /* 全部失败后仍处于关闭态 / still closed after failures */
    CHECK(!wav_is_open());
    CHECK(wav_at_end());
}

/*==== 4. 初始状态 + 复用 / initial state & instance reuse ====*/

static void test_initial_state(void)
{
    uint8_t buf[4];
    uint32_t sr, dsz, dur;
    uint16_t ch, bps;

    /* 进程初始（未打开）/ pristine state before any open */
    CHECK(!wav_is_open());
    CHECK_EQ_INT(wav_read(buf, sizeof(buf)), -ENODEV);
    CHECK_EQ_INT(wav_seek(0.5f), -ENODEV);
    CHECK(wav_at_end());
    CHECK(wav_get_position() == 0.0f);
    wav_get_info(&sr, &ch, &bps, &dsz, &dur);
    CHECK_EQ_INT(sr, 0);

    /* 单实例复用：A 打开→关闭→B 打开仍正确 / reuse global instance */
    char pa[192], pb[192];
    uint8_t da[4] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t db[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

    fixture_path(pa, sizeof(pa), "reuse_a.wav");
    fixture_path(pb, sizeof(pb), "reuse_b.wav");
    make_wav(pa, 1, 1, 48000, 16, 2, da, 4, 0, 0);
    make_wav(pb, 1, 1, 8000, 8, 1, db, 4, 0, 0);

    CHECK_EQ_INT(wav_open(pa), 0);
    wav_close();
    CHECK_EQ_INT(wav_open(pb), 0);
    wav_get_info(&sr, &ch, &bps, &dsz, &dur);
    CHECK_EQ_INT(sr, 8000);      /* 确认读到的是 B 的头 / really file B */
    CHECK_EQ_INT(ch, 1);
    CHECK_EQ_INT(bps, 8);
    CHECK_EQ_INT(dsz, 4);

    /* 打开状态下再 open 另一文件：自动关闭旧的 / auto-close on reopen */
    CHECK_EQ_INT(wav_open(pa), 0);
    wav_get_info(&sr, &ch, &bps, &dsz, &dur);
    CHECK_EQ_INT(sr, 48000);
    CHECK_EQ_INT(dsz, 4);
    wav_close();
}

int main(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/retro_test_wav_XXXXXX");
    if (mkdtemp(g_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    test_initial_state();
    test_mono16_48k();
    test_stereo8_22k();
    test_reject();

    /* 清理样本 / remove fixtures */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warn: fixture cleanup failed: %s\n", g_dir);

    wav_close();   /* 兜底：不留 FILE* 给 ASAN */

    TEST_REPORT("test_wav_decoder");
}
