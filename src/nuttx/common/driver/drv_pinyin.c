/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_pinyin.c - 拼音输入驱动（CLI 行内 IME）
 *
 * WHAT : 拼音输入驱动（CLI 行内 IME）
 * WHY  : 纯 CLI/安全模式下的中文输入（REQUIREMENTS 2.1.3，ime 命令待接）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_pinyin.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 行内编辑 + 词库匹配 + 候选选择
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

/*======================================
 *  配置 / Configuration
 *======================================*/
/* 原来是 #error（CONFIG_PINYIN_CLI 任何配置里都没有定义，必然编译失败），
 * 改为默认启用的优雅降级宏 / graceful default instead of hard #error */
#ifndef CONFIG_RETRO_PINYIN_CLI
#  define CONFIG_RETRO_PINYIN_CLI 1
#endif

#if CONFIG_RETRO_PINYIN_CLI

#define MAX_INPUT_LEN     128     /* 最大输入长度 / Max input length */
#define MAX_DISPLAY_LEN   64      /* 最大显示长度 / Max display length */
#define MAX_CANDIDATES    9       /* 最大候选词数 / Max candidates */
#define MAX_CAND_BYTES    8       /* 单个 UTF-8 候选最大字节 / max UTF-8 bytes */

/*======================================
 *  常用词组词典（简化版）/ Common phrase dictionary (simplified)
 *======================================*/
typedef struct {
    const char *pinyin;      /* 拼音 / Pinyin */
    const char *phrase;      /* 词组 / Phrase */
    uint16_t freq;           /* 词频 / Frequency */
} phrase_entry_t;

static const phrase_entry_t g_phrases[] = {
    /* 常用两字词 / Common 2-char phrases */
    {"zhongguo", "中国", 100},
    {"rensheng", "人生", 95},
    {"shijie", "世界", 95},
    {"shehui", "社会", 90},
    {"tianxia", "天下", 85},
    {"zhonghua", "中华", 90},
    {"renmin", "人民", 95},
    {"gongzuo", "工作", 90},
    {"shenghuo", "生活", 90},
    {"xuexi", "学习", 90},
    {"fazhan", "发展", 85},
    {"jihui", "机会", 85},
    {"wenti", "问题", 85},
    {"fangfa", "方法", 80},
    {"chengguo", "成果", 80},
    {"jiazhi", "价值", 85},
    {"mubiao", "目标", 85},
    {"jindu", "进度", 80},
    {"zhiliang", "质量", 85},
    {"xiaoguo", "效果", 85},
    
    /* 常用三字词 / Common 3-char phrases */
    {"zhongguoren", "中国人", 90},
    {"xiaohuoban", "小伙伴", 85},
    {"zhinengji", "智能机", 80},
    {"weilai", "未来", 85},
    {"bushi", "不是", 90},
    {"zhidao", "知道", 90},
    {"zhengzai", "正在", 85},
    {"zhongyu", "终于", 80},
    {"zhunque", "准确", 85},
    {"jixiangwu", "吉祥物", 75},
    
    /* 系统相关 / System related */
    {"xitong", "系统", 90},
    {"peizhi", "配置", 85},
    {"wenjian", "文件", 90},
    {"mulu", "目录", 85},
    {"shezhi", "设置", 90},
    {"kaishi", "开始", 85},
    {"jieshu", "结束", 80},
    {"baocun", "保存", 85},
    {"duqu", "读取", 80},
    {"shanchu", "删除", 85},
    {"chuangjian", "创建", 80},
    {"bianji", "编辑", 85},
    {"fuzhi", "复制", 85},
    {"jiantie", "剪切", 80},
    {"zhantie", "粘贴", 85},
    {"tihuanda", "替换", 80},
    {"sousuo", "搜索", 85},
    
    /* 状态反馈 / Status feedback */
    {"chenggong", "成功", 95},
    {"shibai", "失败", 95},
    {"cuowu", "错误", 90},
    {"jinggao", "警告", 85},
    {"tishi", "提示", 85},
    {"zhengque", "正确", 90},
    {"wancheng", "完成", 90},
    {"zhongzhi", "终止", 80},
    {"quxiao", "取消", 85},
    {"queren", "确认", 85},
};

/*======================================
 *  单字词典（简化GB2312一级汉字）/ Single char dictionary
 *======================================*/
typedef struct {
    const char pinyin[8];   /* 拼音 / Pinyin */
    const char chars[48];   /* 汉字列表（13 字 ×3 字节 UTF-8 + NUL；
                                原 32 会截断长列表）/ Char list (UTF-8) */
} char_dict_t;

static const char_dict_t g_char_dict[] = {
    {"a",     "啊阿吖嗄"},
    {"ai",    "爱矮挨哎碍癌艾唉矮艾蔼隘"},
    {"an",    "安案按暗岸俺胺鞍"},
    {"ang",   "昂盎"},
    {"ao",    "奥傲熬凹敖澳嚣"},
    {"ba",    "把八巴拔吧坝霸罢芭扒叭笆疤"},
    {"bai",   "白百摆柏败拜"},
    {"ban",   "办半班板版颁扮拌伴瓣绊斑"},
    {"bang",  "帮邦棒膀绑磅镑傍梆"},
    {"bao",   "保报包暴爆胞宝抱饱豹刨苞薄"},
    {"bei",   "被北备背杯贝倍碑卑悲臂"},
    {"ben",   "本苯笨奔"},
    {"beng",  "蹦绷泵崩蚌"},
    {"bi",    "比笔闭鼻碧必毕臂避壁弊"},
    {"bian",  "边便变编遍辩贬辨卞"},
    {"biao",  "表标彪膘"},
    {"bie",   "别憋瘪"},
    {"bin",   "宾滨彬濒"},
    {"bing",  "并病冰兵柄丙秉"},
    {"bo",    "博波拨伯玻搏薄泊柏"},
    {"bu",    "不布步补部埠怖"},
    {"ca",    "擦"},
    {"cai",   "才材财彩采菜蔡"},
    {"can",   "参餐残灿惨"},
    {"cang",  "藏仓沧"},
    {"cao",   "草操曹糙"},
    {"ce",    "测册策侧厕"},
    {"ceng",  "层蹭曾"},
    {"cha",   "查察差茶插叉茬"},
    {"chai",  "拆柴"},
    {"chan",  "产颤铲禅"},
    {"chang", "长场常唱厂畅昌"},
    {"chao",  "超朝潮炒吵抄"},
    {"che",   "车彻扯"},
    {"chen",  "称陈沉晨臣衬"},
    {"cheng", "成城程承呈乘撑澄橙"},
    {"chi",   "吃持迟尺赤池耻齿"},
    {"chong", "重冲充崇虫"},
    {"chou",  "抽愁丑臭仇绸"},
    {"chu",   "出处初础除储楚触"},
    {"chuan", "传船川串"},
    {"chuang","创床闯"},
    {"chui",  "吹垂炊锤"},
    {"chun",  "春纯唇淳"},
    {"chuo",  "戳"},
    {"ci",    "此次词辞刺慈磁"},
    {"cong",  "从丛匆聪"},
    {"cou",   "凑"},
    {"cu",    "粗促醋簇"},
    {"cuan",  "窜攒"},
    {"cui",   "催崔脆翠"},
    {"cun",   "村存寸"},
    {"cuo",   "错措挫搓"},
    {"da",    "大达打答"},
    {"dai",   "带代待袋呆歹逮"},
    {"dan",   "但单担蛋淡胆诞"},
    {"dang",  "当档挡"},
    {"dao",   "到道导刀倒岛盗捣"},
    {"de",    "的得德"},
    {"deng",  "等灯登邓"},
    {"di",    "地第低底敌迪笛"},
    {"dian",  "点电店殿淀滇"},
    {"diao",  "掉吊钓调雕"},
    {"die",   "跌爹碟蝶"},
    {"ding",  "定顶订钉"},
    {"diu",   "丢"},
    {"dong",  "动东冬懂洞冻"},
    {"dou",   "都斗豆逗陡"},
    {"du",    "度读渡毒独堵杜"},
    {"duan",  "段短端断锻"},
    {"dui",   "对队堆兑"},
    {"dun",   "吨顿盾钝炖"},
    {"duo",   "多夺朵躲"},
    {"e",     "额恶饿鹅俄"},
    {"en",    "恩"},
    {"er",    "而二尔儿耳"},
    {"fa",    "发法罚乏伐"},
    {"fan",   "反饭番翻凡烦"},
    {"fang",  "方放房访防纺"},
    {"fei",   "非飞费肥"},
    {"fen",   "分份粉纷芬"},
    {"feng",  "风封丰峰锋"},
    {"fo",    "佛"},
    {"fou",   "否"},
    {"fu",    "服副府福父付妇负复傅"},
    {"ga",    "噶嘎"},
    {"gai",   "该改盖概钙"},
    {"gan",   "干赶感敢杆"},
    {"gang",  "刚港钢纲"},
    {"gao",   "高搞稿告"},
    {"ge",    "个各哥歌革格"},
    {"geng",  "更耕根羹"},
    {"gong",  "工公功共供"},
    {"gou",   "够狗沟钩"},
    {"gu",    "古估骨故固顾"},
    {"gua",   "挂刮瓜卦"},
    {"guai",  "怪乖拐"},
    {"guan",  "关观官管馆"},
    {"guang", "光广逛"},
    {"gui",   "贵归规柜龟"},
    {"gun",   "滚棍"},
    {"guo",   "过国果裹"},
    {"ha",    "哈"},
    {"hai",   "还海害骸"},
    {"han",   "汉喊含寒韩旱"},
    {"hang",  "行航夯"},
    {"hao",   "好号浩耗"},
    {"he",    "和河何合核喝荷"},
    {"hei",   "黑嘿"},
    {"hen",   "很恨狠"},
    {"heng",  "横恒哼"},
    {"hong",  "红洪宏虹鸿"},
    {"hou",   "后候厚侯"},
    {"hu",    "户湖呼互胡虎"},
    {"hua",   "化话花华划"},
    {"huai",  "坏怀淮拐"},
    {"huan",  "还环换唤患"},
    {"huang", "黄慌晃荒皇"},
    {"hui",   "会回挥汇毁辉"},
    {"hun",   "混婚魂浑"},
    {"huo",   "或活火获货祸"},
    {"ji",    "及机基记计己级极技际"},
    {"jia",   "家加假价架驾嘉"},
    {"jian",  "见间件建键坚监简减检"},
    {"jiang", "将讲江奖疆酱蒋"},
    {"jiao",  "教交较角脚郊焦"},
    {"jie",   "结接街节姐解界借洁杰"},
    {"jin",   "进今仅金近尽紧劲"},
    {"jing",  "经精京境景竞净静"},
    {"jiong", "窘炯"},
    {"jiu",   "就九久酒救旧舅"},
    {"ju",    "具据句举巨局聚"},
    {"juan",  "卷倦绢圈眷"},
    {"jue",   "决绝角觉掘"},
    {"jun",   "军均君菌俊峻"},
    {"ka",    "咖卡咯"},
    {"kai",   "开揩凯"},
    {"kan",   "看砍坎刊勘"},
    {"kang",  "抗康慷"},
    {"kao",   "考靠烤"},
    {"ke",    "可科刻克客课"},
    {"ken",   "肯恳啃"},
    {"keng",  "坑吭"},
    {"kong",  "空控孔恐"},
    {"kou",   "口扣抠"},
    {"ku",    "苦库哭酷"},
    {"kua",   "跨夸垮挎"},
    {"kuai",  "快块会计"},
    {"kuan",  "宽款"},
    {"kuang", "况矿框狂"},
    {"kui",   "亏愧葵魁"},
    {"kun",   "困昆坤"},
    {"kuo",   "扩阔廓"},
    {"la",    "拉啦辣腊"},
    {"lai",   "来赖莱"},
    {"lan",   "兰蓝篮栏澜烂"},
    {"lang",  "浪郎朗榔"},
    {"lao",   "老劳姥捞"},
    {"le",    "了乐勒"},
    {"lei",   "类累泪雷垒"},
    {"leng",  "冷愣棱"},
    {"li",    "里理力立李历例利"},
    {"lia",   "俩"},
    {"lian",  "连联脸练恋莲"},
    {"liang", "两亮粮凉梁"},
    {"liao",  "了料辽疗廖"},
    {"lie",   "列烈猎劣"},
    {"lin",   "林临邻"},
    {"ling",  "另零领令灵"},
    {"liu",   "六流留刘柳"},
    {"long",  "龙隆拢笼"},
    {"lou",   "楼搂娄"},
    {"lu",    "路陆露鲁卢炉"},
    {"luan",  "乱卵"},
    {"lue",   "略掠"},
    {"lun",   "论轮伦仑"},
    {"luo",   "落罗洛逻"},
    {"ma",    "妈马吗麻码玛"},
    {"mai",   "买卖麦迈"},
    {"man",   "满慢漫曼"},
    {"mang",  "忙盲茫莽"},
    {"mao",   "毛冒贸帽猫"},
    {"me",    "么"},
    {"mei",   "没每美妹"},
    {"men",   "们门闷"},
    {"meng",  "梦蒙盟猛"},
    {"mi",    "米密迷蜜"},
    {"mian",  "面棉免眠"},
    {"miao",  "秒苗描妙"},
    {"mie",   "灭蔑"},
    {"min",   "民抿敏"},
    {"ming",  "名明命鸣"},
    {"miu",   "谬"},
    {"mo",    "莫末模摸墨"},
    {"mou",   "某谋牟"},
    {"mu",    "目木母墓幕"},
    {"na",    "那拿纳娜哪"},
    {"nai",   "乃奶耐"},
    {"nan",   "南男难"},
    {"nang",  "囊"},
    {"nao",   "脑恼闹"},
    {"ne",    "呢哪"},
    {"nei",   "内馁"},
    {"nen",   "嫩"},
    {"neng",  "能"},
    {"ni",    "你尼泥拟"},
    {"nian",  "年念碾"},
    {"niang", "娘酿"},
    {"niao",  "鸟尿"},
    {"nie",   "捏聂"},
    {"nin",   "您"},
    {"ning",  "宁凝拧"},
    {"niu",   "牛扭纽"},
    {"nong",  "农浓弄"},
    {"nu",    "努奴怒"},
    {"nuan",  "暖"},
    {"nue",   "虐疟"},
    {"nuo",   "诺挪懦"},
    {"nv",    "女"},
    {"o",     "哦噢"},
    {"ou",    "欧偶呕"},
    {"pa",    "怕爬帕"},
    {"pai",   "派排拍牌"},
    {"pan",   "盘叛判盼"},
    {"pang",  "旁胖庞"},
    {"pao",   "跑炮泡"},
    {"pei",   "配陪培赔"},
    {"pen",   "喷盆"},
    {"peng",  "朋棚彭蓬"},
    {"pi",    "批皮屁啤"},
    {"pian",  "片偏篇骗"},
    {"piao",  "票飘漂"},
    {"pie",   "撇瞥"},
    {"pin",   "品贫拼"},
    {"ping",  "平评屏乒"},
    {"po",    "破迫泼"},
    {"pou",   "剖"},
    {"pu",    "普扑铺葡"},
    {"qi",    "起期其七气企器"},
    {"qia",   "恰洽掐"},
    {"qian",  "前千签欠浅迁"},
    {"qiang", "强墙抢"},
    {"qiao",  "桥乔侨巧"},
    {"qie",   "且切茄窃"},
    {"qin",   "亲琴秦勤"},
    {"qing",  "请青轻清情晴"},
    {"qiong", "穷琼"},
    {"qiu",   "求球秋丘"},
    {"qu",    "去区取曲趣"},
    {"quan",  "全权泉券"},
    {"que",   "却缺确"},
    {"qun",   "群裙"},
    {"ran",   "然燃染"},
    {"rang",  "让嚷"},
    {"rao",   "绕饶"},
    {"re",    "热惹"},
    {"ren",   "人任认仁刃"},
    {"reng",  "扔仍"},
    {"ri",    "日"},
    {"rong",  "容荣融绒"},
    {"rou",   "肉柔揉"},
    {"ru",    "如入儒乳"},
    {"ruan",  "软"},
    {"rui",   "瑞锐"},
    {"run",   "润闰"},
    {"ruo",   "若弱"},
    {"sa",    "撒洒萨"},
    {"sai",   "赛腮鳃"},
    {"san",   "三散伞"},
    {"sang",  "桑嗓丧"},
    {"sao",   "扫骚嫂"},
    {"se",    "色涩瑟"},
    {"sen",   "森"},
    {"seng",  "僧"},
    {"sha",   "沙杀啥纱"},
    {"shai",  "晒筛"},
    {"shan",  "山闪衫善扇"},
    {"shang", "上商尚赏"},
    {"shao",  "少绍烧稍"},
    {"she",   "社设舌射舍"},
    {"shen",  "什深身神审沈"},
    {"sheng", "生声升胜"},
    {"shi",   "是时实事十室"},
    {"shou",  "手首守受授"},
    {"shu",   "书树数术属"},
    {"shua",  "刷耍"},
    {"shuai", "摔帅率"},
    {"shuan", "栓拴"},
    {"shuang","双霜爽"},
    {"shui",  "水税睡"},
    {"shun",  "顺瞬"},
    {"shuo",  "说硕朔"},
    {"si",    "四思斯死司"},
    {"song",  "送松宋"},
    {"sou",   "搜艘"},
    {"su",    "速素诉苏塑"},
    {"suan",  "算酸蒜"},
    {"sui",   "岁随碎虽"},
    {"sun",   "孙损笋"},
    {"suo",   "所索缩琐"},
    {"ta",    "他她它踏塔"},
    {"tai",   "太台抬态"},
    {"tan",   "谈探坦叹炭"},
    {"tang",  "糖堂唐汤"},
    {"tao",   "讨套逃桃淘"},
    {"te",    "特"},
    {"teng",  "疼腾藤"},
    {"ti",    "体题提替踢"},
    {"tian",  "天田甜填"},
    {"tiao",  "条调跳挑"},
    {"tie",   "铁贴帖"},
    {"ting",  "听停庭"},
    {"tong",  "同通痛统"},
    {"tou",   "头投透"},
    {"tu",    "土图途突"},
    {"tuan",  "团揣"},
    {"tui",   "推腿退"},
    {"tun",   "囤吞臀"},
    {"tuo",   "托脱拓拖"},
    {"wa",    "挖瓦蛙洼"},
    {"wai",   "外歪"},
    {"wan",   "完万晚碗弯顽"},
    {"wang",  "往王网望忘"},
    {"wei",   "为位未围伟卫微味"},
    {"wen",   "文问闻温蚊"},
    {"weng",  "翁嗡"},
    {"wo",    "我握卧沃"},
    {"wu",    "无五屋物务误"},
    {"xi",    "西系统喜希析席习"},
    {"xia",   "下夏吓峡侠"},
    {"xian",  "现先县线限鲜"},
    {"xiang", "向想相象像"},
    {"xiao",  "小校笑效消销肖"},
    {"xie",   "些写谢协血"},
    {"xin",   "新心信辛欣"},
    {"xing",  "行型形星醒"},
    {"xiong", "熊胸雄凶"},
    {"xiu",   "修秀休宿"},
    {"xu",    "需续许虚需"},
    {"xuan",  "选宣旋玄"},
    {"xue",   "学雪血"},
    {"xun",   "讯迅寻训循"},
    {"ya",    "压呀押鸭牙芽"},
    {"yan",   "研究验眼严言延沿"},
    {"yang",  "样阳洋养氧"},
    {"yao",   "要药约腰摇"},
    {"ye",    "也业页夜野"},
    {"yi",    "一以已意义"},
    {"yin",   "因银音引印饮"},
    {"ying",  "应英影营迎映"},
    {"yo",    "哟"},
    {"yong",  "用永涌泳勇"},
    {"you",   "有由又友右"},
    {"yu",    "于与雨语育预"},
    {"yuan",  "元原园圆员"},
    {"yue",   "月越乐约跃"},
    {"yun",   "运云匀孕"},
    {"za",    "杂咱砸"},
    {"zai",   "在再载灾"},
    {"zan",   "咱暂赞"},
    {"zang",  "脏葬"},
    {"zao",   "早造遭糟枣"},
    {"ze",    "则责择泽"},
    {"zei",   "贼"},
    {"zen",   "怎"},
    {"zeng",  "增曾"},
    {"zha",   "扎炸诈闸"},
    {"zhai",  "宅债斋"},
    {"zhan",  "站占战展盏"},
    {"zhang", "长张章掌涨"},
    {"zhao",  "找着照赵"},
    {"zhe",   "这者著浙"},
    {"zhen",  "真针镇震"},
    {"zheng", "正政整争证"},
    {"zhi",   "之支只知制直"},
    {"zhong", "中重众终钟"},
    {"zhou",  "周州洲轴"},
    {"zhu",   "主著住注助柱"},
    {"zhua",  "抓爪"},
    {"zhuai", "拽"},
    {"zhuan", "转专赚传"},
    {"zhuang","装庄撞壮"},
    {"zhui",  "追坠缀"},
    {"zhun",  "准"},
    {"zhuo",  "桌捉卓灼"},
    {"zi",    "子字自资"},
    {"zong",  "总宗综纵"},
    {"zou",   "走奏揍"},
    {"zu",    "组族足租"},
    {"zuan",  "钻攥"},
    {"zui",   "最嘴罪"},
    {"zun",   "尊遵"},
    {"zuo",   "做作坐左座"},
};

/*======================================
 *  CLI 输入状态结构 / CLI Input State Structure
 *======================================*/
typedef struct {
    char input_buf[MAX_INPUT_LEN];   /* 输入缓冲区 / Input buffer */
    int input_len;                    /* 当前输入长度 / Current input length */
    char pinyin_buf[MAX_INPUT_LEN];   /* 拼音缓冲区 / Pinyin buffer */
    int pinyin_len;                   /* 拼音长度 / Pinyin length */
    char display_buf[MAX_DISPLAY_LEN]; /* 显示缓冲区 / Display buffer */
    int mode;                         /* 0=英文, 1=中文 / 0=EN, 1=ZH */
    int cursor_pos;                   /* 光标位置 / Cursor position */
} cli_pinyin_state_t;

static cli_pinyin_state_t g_cli_state;

/*======================================
 *  内部函数 / Internal Functions
 *======================================*/

/*
 * 功能描述 / WHAT: 计算 UTF-8 序列长度 / Length of one UTF-8 sequence
 * WHY : 单字词典是 UTF-8（汉字 3 字节），按字节迭代会把一个汉字拆成
 *       3 个残缺候选 / char dict is UTF-8; byte stepping splits hanzi
 * WHO : search_chars / cli 候选逻辑
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_pinyin.c
 * WHEN : 2026-10-04 新增
 * HOW  : 依据首字节前导 1 的个数判断（RFC 3629）
 * 返回值 / Return: 1-4；非法首字节按 1 处理
 */
static int utf8_seq_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;  /* 非法首字节 / invalid lead byte */
}

/**
 * search_phrases - 按全拼前缀搜索词组 / Search phrases by full-pinyin prefix
 * @pinyin: 拼音字符串 / Pinyin string
 * @results: 结果缓冲区（最多9个）/ Result buffer (max 9)
 * @max_results: 最大结果数 / Max results
 * return: 实际结果数 / Actual results count
 */
static int search_phrases(const char *pinyin, const char *results[], int max_results)
{
    int count = 0;
    int i;
    int p_len = strlen(pinyin);

    for (i = 0; i < (int)(sizeof(g_phrases) / sizeof(g_phrases[0])) && count < max_results; i++) {
        if (strncmp(g_phrases[i].pinyin, pinyin, p_len) == 0) {
            results[count++] = g_phrases[i].phrase;
        }
    }

    return count;
}

/*
 * 功能描述 / WHAT: 按拼音前缀搜索单字候选 / Search single hanzi candidates
 * WHY : 候选必须按完整 UTF-8 序列拷贝（原实现逐字节拷贝产生残缺字节）
 * WHO : cli_show_candidates / cli_pinyin_input
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_pinyin.c
 * WHEN : 2026-10-04 重写（UTF-8 安全 + 去掉恒等 expand_abbr 映射）
 * HOW  : 遍历词典命中项，utf8_seq_len 步进，整字 memcpy 进 results[][]
 * 返回值 / Return: 实际候选数 / number of candidates
 */
static int search_chars(const char *pinyin,
                        char results[][MAX_CAND_BYTES], int max_results)
{
    int count = 0;
    int i;
    int p_len = strlen(pinyin);

    for (i = 0; i < (int)(sizeof(g_char_dict) / sizeof(g_char_dict[0])) && count < max_results; i++) {
        if (strncmp(g_char_dict[i].pinyin, pinyin, p_len) == 0) {
            const char *chars = g_char_dict[i].chars;
            int j = 0;

            while (chars[j] != '\0' && count < max_results) {
                int len = utf8_seq_len((unsigned char)chars[j]);
                if (chars[j + len] != '\0' && len > 1) {
                    /* 尾部截断的序列，丢弃 / truncated tail, drop */
                    break;
                }

                /* 去重 / dedup by full string */
                int k;
                int dup = 0;
                for (k = 0; k < count; k++) {
                    if (strncmp(results[k], chars + j, (size_t)len) == 0 &&
                        strlen(results[k]) == (size_t)len) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) {
                    memcpy(results[count], chars + j, (size_t)len);
                    results[count][len] = '\0';
                    count++;
                }
                j += len;
            }
        }
    }

    return count;
}

/*
 * 功能描述 / WHAT: 向输入缓冲追加 UTF-8 文本（带边界检查）
 * WHY : 原实现逐字节写入无容量检查，多字词可写穿 input_buf
 * WHO : cli_pinyin_input 的候选确认路径
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_pinyin.c
 * WHEN : 2026-10-04 新增
 * HOW  : memcpy 定长 + 显式容量判断
 * 返回值 / Return: OK；-ENOSPC 缓冲已满
 */
static int cli_append_text(const char *s)
{
    int n = 0;

    while (s[n] != '\0')
        n++;

    if (g_cli_state.input_len + n >= MAX_INPUT_LEN)
        return -ENOSPC;

    memcpy(&g_cli_state.input_buf[g_cli_state.input_len], s, (size_t)n);
    g_cli_state.input_len += n;
    g_cli_state.input_buf[g_cli_state.input_len] = '\0';
    return OK;
}

/**
 * cli_refresh_line - 刷新输入行显示 / Refresh input line display
 */
static void cli_refresh_line(void)
{
    int i;
    char *p;
    
    /* 清屏并回车到行首 / Clear screen and return to line start */
    printf("\r\x1B[K");  /* CR + clear line */
    
    /* 显示模式 / Show mode */
    if (g_cli_state.mode == 1) {
        printf("[中] ");
    } else {
        printf("[英] ");
    }
    
    /* 显示当前输入 / Show current input */
    if (g_cli_state.mode == 1 && g_cli_state.pinyin_len > 0) {
        /* 中文模式：显示拼音和候选 / Chinese mode: show pinyin and candidates */
        printf("%s", g_cli_state.input_buf);
        printf("\x1B[32m%s\x1B[0m", g_cli_state.pinyin_buf);
    } else {
        /* 英文模式 / English mode */
        printf("%s", g_cli_state.input_buf);
    }
    
    /* 重新显示光标 / Redisplay cursor */
    printf("\r");
    
    /* 移动光标到正确位置 / Move cursor to correct position */
    if (g_cli_state.mode == 1 && g_cli_state.pinyin_len > 0) {
        printf("\x1B[%dC", 5 + g_cli_state.input_len);  /* 5 for "[中] " */
    } else {
        printf("\x1B[%dC", 5 + g_cli_state.input_len);
    }
    
    fflush(stdout);
}

/**
 * cli_show_candidates - 显示候选词 / Show candidates
 */
static void cli_show_candidates(void)
{
    const char *phrase_results[MAX_CANDIDATES];
    char char_results[MAX_CANDIDATES][MAX_CAND_BYTES];
    int phrase_count;
    int char_count;
    char buf[32];
    int i;

    /* 直接用已输入的全拼前缀匹配（首字母输入自然退化为简拼），
     * 原实现只取首字母做恒等映射，多字母拼音永远匹配不上
     * match the typed full-pinyin prefix directly */
    phrase_count = search_phrases(g_cli_state.pinyin_buf, phrase_results,
                                  MAX_CANDIDATES);
    char_count = search_chars(g_cli_state.pinyin_buf, char_results,
                              MAX_CANDIDATES - phrase_count);

    if (phrase_count == 0 && char_count == 0) {
        printf("\r\n\x1B[31m无候选词\x1B[0m\r\n");
        return;
    }

    /* 显示候选词 / Show candidates */
    printf("\r\n候选: ");

    for (i = 0; i < phrase_count; i++) {
        snprintf(buf, sizeof(buf), "%d.%s ", i + 1, phrase_results[i]);
        printf("\x1B[33m%s\x1B[0m", buf);
    }

    for (i = 0; i < char_count; i++) {
        snprintf(buf, sizeof(buf), "%d.%s ",
                 phrase_count + i + 1, char_results[i]);
        printf("\x1B[36m%s\x1B[0m", buf);
    }

    printf("\r\n");
}

/*======================================
 *  主要API / Main API
 *======================================*/

/**
 * cli_pinyin_init - 初始化 CLI 拼音输入 / Initialize CLI pinyin input
 */
void cli_pinyin_init(void)
{
    memset(&g_cli_state, 0, sizeof(g_cli_state));
    g_cli_state.mode = 1;  /* 默认中文模式 / Default Chinese mode */
}

/**
 * cli_pinyin_reset - 重置输入状态 / Reset input state
 */
void cli_pinyin_reset(void)
{
    g_cli_state.input_len = 0;
    g_cli_state.pinyin_len = 0;
    g_cli_state.input_buf[0] = '\0';
    g_cli_state.pinyin_buf[0] = '\0';
    g_cli_state.cursor_pos = 0;
}

/**
 * cli_pinyin_toggle_mode - 切换中英文模式 / Toggle Chinese/English mode
 */
void cli_pinyin_toggle_mode(void)
{
    g_cli_state.mode = 1 - g_cli_state.mode;
    cli_pinyin_reset();

    /* 2026-10-05：模式提示改由 cvbs_ime 状态条显示（"拼音[..]"/
     * "英文直通"），不再 printf 打屏（CCDOS 式形态下打屏会干扰
     * 正文区滚动） */
}

/**
 * cli_pinyin_set_mode - 设置输入模式 / Set input mode
 * @mode: 0=英文, 1=中文 / 0=EN, 1=ZH
 */
void cli_pinyin_set_mode(int mode)
{
    g_cli_state.mode = (mode != 0) ? 1 : 0;
    cli_pinyin_reset();
}

/**
 * cli_pinyin_get_mode - 获取当前模式 / Get current mode
 * return: 0=英文, 1=中文 / 0=EN, 1=ZH
 */
int cli_pinyin_get_mode(void)
{
    return g_cli_state.mode;
}

/**
 * cli_pinyin_input - 处理键盘输入 / Handle keyboard input
 * @ch: 输入字符 / Input character
 * return: 0=已处理, 1=需要显示候选, -1=未识别键 / 0=handled, 1=show candidates, -1=unrecognized
 */
int cli_pinyin_input(int ch)
{
    char tmp[8];
    
    if (g_cli_state.mode == 0) {
        /* 英文模式 / English mode */
        if (ch >= 0x20 && ch < 0x7F && g_cli_state.input_len < MAX_INPUT_LEN - 1) {
            /* 可打印字符 / Printable character */
            memmove(&g_cli_state.input_buf[g_cli_state.cursor_pos + 1],
                    &g_cli_state.input_buf[g_cli_state.cursor_pos],
                    g_cli_state.input_len - g_cli_state.cursor_pos);
            g_cli_state.input_buf[g_cli_state.cursor_pos] = (char)ch;
            g_cli_state.input_len++;
            g_cli_state.input_buf[g_cli_state.input_len] = '\0';
            g_cli_state.cursor_pos++;
        } else if (ch == '\b') {
            /* Backspace / Backspace */
            if (g_cli_state.cursor_pos > 0) {
                memmove(&g_cli_state.input_buf[g_cli_state.cursor_pos - 1],
                        &g_cli_state.input_buf[g_cli_state.cursor_pos],
                        g_cli_state.input_len - g_cli_state.cursor_pos);
                g_cli_state.input_len--;
                g_cli_state.input_buf[g_cli_state.input_len] = '\0';
                g_cli_state.cursor_pos--;
            }
        } else if (ch == '\r' || ch == '\n') {
            /* Enter - 返回输入内容 / Enter - return input */
            return 1;  /* 输入完成 / Input complete */
        } else if (ch == 0x1B) {
            /* Esc - 取消输入 / Esc - cancel input */
            cli_pinyin_reset();
        }
        
        return 0;
    }
    
    /* 中文模式 / Chinese mode */
    if (ch >= 'a' && ch <= 'z') {
        /* 拼音输入 / Pinyin input */
        if (g_cli_state.pinyin_len < MAX_INPUT_LEN - 1) {
            g_cli_state.pinyin_buf[g_cli_state.pinyin_len++] = (char)ch;
            g_cli_state.pinyin_buf[g_cli_state.pinyin_len] = '\0';
            return 1;  /* 需要显示候选 / Need to show candidates */
        }
        
    } else if (ch >= '1' && ch <= '9') {
        /* 候选选择 / Candidate selection */
        int idx = ch - '1';
        const char *phrase_results[MAX_CANDIDATES];
        char char_results[MAX_CANDIDATES][MAX_CAND_BYTES];
        int phrase_count;

        /* 与显示时同样的全拼前缀搜索 / same search as display path */
        phrase_count = search_phrases(g_cli_state.pinyin_buf, phrase_results,
                                      MAX_CANDIDATES);
        int char_count = search_chars(g_cli_state.pinyin_buf, char_results,
                                      MAX_CANDIDATES - phrase_count);

        /* 2026-10-05 修复：选字后只清拼音、保留 input_buf——原 reset()
         * 把刚选上屏的字一起清掉（选字即丢字） */
        if (idx < phrase_count) {
            cli_append_text(phrase_results[idx]);
            g_cli_state.pinyin_buf[0] = '\0';
            g_cli_state.pinyin_len = 0;
            return 0;

        } else if (idx < phrase_count + char_count) {
            cli_append_text(char_results[idx - phrase_count]);
            g_cli_state.pinyin_buf[0] = '\0';
            g_cli_state.pinyin_len = 0;
            return 0;
        }

    } else if (ch == '\b') {
        /* Backspace / Backspace */
        if (g_cli_state.pinyin_len > 0) {
            g_cli_state.pinyin_len--;
            g_cli_state.pinyin_buf[g_cli_state.pinyin_len] = '\0';
            return 1;  /* 需要刷新候选 / Need to refresh candidates */
        } else if (g_cli_state.input_len > 0) {
            g_cli_state.input_len--;
            g_cli_state.input_buf[g_cli_state.input_len] = '\0';
        }
        
    } else if (ch == '\r' || ch == '\n') {
        /* Enter - 如果有拼音未确认，取首个候选上屏（带边界检查）
         * Enter - commit first candidate if pinyin is pending */
        if (g_cli_state.pinyin_len > 0) {
            char char_results[MAX_CANDIDATES][MAX_CAND_BYTES];
            int char_count = search_chars(g_cli_state.pinyin_buf,
                                          char_results, MAX_CANDIDATES);

            if (char_count > 0) {
                cli_append_text(char_results[0]);
            }
        }
        /* 2026-10-05 修复：Enter 只清拼音、保留整行文本——
         * 原 reset() 连已确认行一起清，行编辑模型被破坏 */
        g_cli_state.pinyin_buf[0] = '\0';
        g_cli_state.pinyin_len = 0;
        return 0;  /* 行完成（文本保留在 input_buf 供调用方取） */
        
    } else if (ch == 0x1B) {
        /* Esc - 取消拼音输入 / Esc - cancel pinyin input */
        cli_pinyin_reset();
        return 0;
        
    } else if (ch == ' ') {
        /* 空格 - 确认第一个候选 / Space - confirm first candidate */
        if (g_cli_state.pinyin_len > 0) {
            const char *phrase_results[MAX_CANDIDATES];
            char char_results[MAX_CANDIDATES][MAX_CAND_BYTES];
            int phrase_count;

            phrase_count = search_phrases(g_cli_state.pinyin_buf,
                                          phrase_results, MAX_CANDIDATES);
            int char_count = search_chars(g_cli_state.pinyin_buf, char_results,
                                          MAX_CANDIDATES - phrase_count);

            if (phrase_count > 0) {
                cli_append_text(phrase_results[0]);
            } else if (char_count > 0) {
                cli_append_text(char_results[0]);
            }

            g_cli_state.input_buf[g_cli_state.input_len] = '\0';
            cli_pinyin_reset();
        }
        return 0;
    }
    
    return 0;
}

/**
 * cli_pinyin_get_input - 获取当前输入内容 / Get current input content
 * return: 输入字符串 / Input string
 */
/* WHAT : 当前拼音串（无输入返回空串；cvbs_ime 状态条显示用） */
const char* cli_pinyin_get_pinyin(void)
{
    return g_cli_state.pinyin_buf;
}

const char* cli_pinyin_get_input(void)
{
    return g_cli_state.input_buf;
}

/**
 * cli_pinyin_get_input_len - 获取输入长度 / Get input length
 * return: 输入长度 / Input length
 */
int cli_pinyin_get_input_len(void)
{
    return g_cli_state.input_len;
}

/**
 * cli_pinyin_redraw - 重绘输入行 / Redraw input line
 */
void cli_pinyin_redraw(void)
{
    cli_refresh_line();
}

/**
 * cli_pinyin_print_candidates - 打印候选词列表 / Print candidate list
 */
void cli_pinyin_print_candidates(void)
{
    if (g_cli_state.mode == 0 || g_cli_state.pinyin_len == 0) {
        return;
    }
    
    cli_show_candidates();
}

/*======================================
 *  命令行输入函数 / CLI Input Functions
 *======================================*/

/**
 * cli_getline - 获取一行输入（支持拼音）/ Get a line of input (with pinyin support)
 * @buf: 输出缓冲区 / Output buffer
 * @len: 缓冲区长度 / Buffer length
 * return: 实际读取的字符数 / Actual characters read
 */
int cli_getline(char *buf, int len)
{
    int ch;
    int ret;
    
    if (buf == NULL || len <= 0) {
        return -1;
    }
    
    cli_pinyin_init();
    cli_refresh_line();
    
    while (1) {
        ch = getchar();
        if (ch == EOF || ch < 0) {
            buf[0] = '\0';
            return -1;
        }
        
        ret = cli_pinyin_input(ch);
        
        if (ret == 0) {
            cli_refresh_line();
        } else if (ret == 1) {
            cli_show_candidates();
            cli_refresh_line();
        }
        
        /* 检查是否输入完成 / Check if input is complete */
        if ((ch == '\r' || ch == '\n') && g_cli_state.pinyin_len == 0) {
            break;
        }
    }
    
    /* 复制结果 / Copy result */
    strncpy(buf, g_cli_state.input_buf, len - 1);
    buf[len - 1] = '\0';
    
    printf("\r\n");  /* 换行 / Newline */
    
    return strlen(buf);
}

/*
 * WHAT : 当前拼音的候选列表导出（词组在前 + 单字在后合并编号）
 * WHY  : cvbs_ime 状态条需要拼 "拼音[ni] 1你 2尼..." 文本（2026-10-05）
 * HOW  : 词组指针指向词库常量；单字写入静态 UTF-8 缓冲——调用方
 *        立即消费；无拼音时返回 0
 */
int cli_pinyin_candidates(const char *out[], int max)
{
    static char char_buf[MAX_CANDIDATES][MAX_CAND_BYTES];
    const char *phrase_results[MAX_CANDIDATES];
    int phrase_count, char_count, n = 0, i;

    if (g_cli_state.pinyin_len == 0 || out == NULL || max <= 0)
        return 0;

    phrase_count = search_phrases(g_cli_state.pinyin_buf, phrase_results,
                                  MAX_CANDIDATES);
    char_count = search_chars(g_cli_state.pinyin_buf, char_buf,
                              MAX_CANDIDATES - phrase_count);

    for (i = 0; i < phrase_count && n < max; i++)
        out[n++] = phrase_results[i];
    for (i = 0; i < char_count && n < max; i++)
        out[n++] = char_buf[i];
    return n;
}

#endif /* CONFIG_RETRO_PINYIN_CLI */
