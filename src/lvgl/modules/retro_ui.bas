REM retro_ui.bas - my-basic 绑定 / my-basic Bindings
REM
REM 为 my-basic 脚本提供 LVGL UI 接口调用能力
REM Provides LVGL UI interface for my-basic scripts
REM
REM 用法说明 / Usage:
REM   在使用前，需要先加载 C 端集成模块:
REM   Before use, load the C integration module:
REM
REM   1. 确保 retro_ui_bas.c 已编译到固件中
REM      Ensure retro_ui_bas.c is compiled into firmware
REM
REM   2. 在 BASIC 程序中声明外部函数:
REM      Declare external functions in your BASIC program:
REM      DECLARE FUNCTION retro_ui_msgbox(title$, msg$)
REM      DECLARE FUNCTION retro_ui_input$(title$, prompt$, bufsize)
REM      DECLARE FUNCTION retro_ui_list(title$, prompt$, item_count)
REM      DECLARE FUNCTION retro_ui_confirm(title$, msg$)
REM      DECLARE SUB retro_ui_status(msg$)
REM      DECLARE SUB retro_ui_progress(value, max)
REM
REM   3. 然后就可以调用了:
REM      Then you can call them:
REM      CALL retro_ui_msgbox("提示", "操作完成!")
REM      CALL retro_ui_status("正在处理...")
REM
REM ========================================
REM  示例程序 / Example Program
REM ========================================

    DECLARE FUNCTION retro_ui_msgbox(title$, msg$)
    DECLARE FUNCTION retro_ui_input$(title$, prompt$, bufsize)
    DECLARE FUNCTION retro_ui_list(title$, prompt$, item_count)
    DECLARE FUNCTION retro_ui_confirm(title$, msg$)
    DECLARE SUB retro_ui_status(msg$)
    DECLARE SUB retro_ui_progress(value, max)

    REM 初始化 / Initialize
    PRINT "Retro UI Demo Start"

    REM 示例1: 消息框 / Example 1: Message Box
    CALL retro_ui_msgbox("欢迎", "Hello from Retro UI!")
    PRINT "After msgbox"

    REM 示例2: 确认对话框 / Example 2: Confirm Dialog
    LET ok = retro_ui_confirm("确认", "确定要继续吗?")
    IF ok = 1 THEN
        PRINT "User clicked Yes"
    ELSE
        PRINT "User clicked No"
    END IF

    REM 示例3: 状态栏消息 / Example 3: Status Bar Message
    CALL retro_ui_status("正在加载...")
    PRINT "Status shown"

    REM 示例4: 进度条 / Example 4: Progress Bar
    FOR i = 0 TO 100 STEP 10
        CALL retro_ui_progress(i, 100)
        PRINT "Progress: "; i; "%"
    NEXT i

    REM 示例5: 输入对话框 / Example 5: Input Dialog
    LET name$ = retro_ui_input$("输入", "请输入您的名字:", 32)
    IF name$ <> "" THEN
        PRINT "Hello, "; name$
    ELSE
        PRINT "Input cancelled"
    END IF

    REM 示例6: 列表选择 / Example 6: List Selection
    REM 注意: 列表需要 C 端配合传递选项数组
    REM Note: List requires C-side option array passing
    LET choice = retro_ui_list("选择", "选择一个选项:", 3)
    IF choice >= 0 THEN
        PRINT "Selected index: "; choice
    ELSE
        PRINT "Selection cancelled"
    END IF

    REM 完成 / Done
    CALL retro_ui_msgbox("完成", "Demo finished!")
    PRINT "Demo end"

REM ========================================
REM  API 参考 / API Reference
REM ========================================
REM
REM  SUB retro_ui_msgbox(title$, msg$)
REM  --------------------------------
REM  显示一个简单的消息框
REM  Shows a simple message box
REM  参数:
REM    title$ - 窗口标题 / Window title
REM    msg$   - 消息内容 / Message content
REM  返回: 无 / Return: None
REM
REM  FUNCTION retro_ui_input$(title$, prompt$, bufsize)
REM  -----------------------------------------------
REM  显示一个输入对话框
REM  Shows an input dialog
REM  参数:
REM    title$   - 窗口标题 / Window title
REM    prompt$  - 提示文字 / Prompt text
REM    bufsize  - 输入缓冲区大小 / Input buffer size
REM  返回: 用户输入的字符串，取消返回空 / User input string, empty on cancel
REM
REM  FUNCTION retro_ui_list(title$, prompt$, item_count)
REM  --------------------------------------------
REM  显示一个列表选择对话框
REM  Shows a list selection dialog
REM  参数:
REM    title$      - 窗口标题 / Window title
REM    prompt$     - 提示文字 / Prompt text
REM    item_count  - 选项数量 / Number of items
REM  返回: 选择的索引 (0-based)，取消返回 -1 / Selected index (0-based), -1 on cancel
REM
REM  FUNCTION retro_ui_confirm(title$, msg$)
REM  ------------------------------------
REM  显示一个 Yes/No 确认对话框
REM  Shows a Yes/No confirm dialog
REM  参数:
REM    title$ - 窗口标题 / Window title
REM    msg$   - 消息内容 / Message content
REM  返回: 1=Yes, 0=No
REM
REM  SUB retro_ui_status(msg$)
REM  -------------------------
REM  显示一个临时状态栏消息（3秒自动消失）
REM  Shows a temporary status bar message (auto-dismiss after 3s)
REM  参数:
REM    msg$ - 状态消息 / Status message
REM  返回: 无 / Return: None
REM
REM  SUB retro_ui_progress(value, max)
REM  --------------------------------
REM  更新进度条显示
REM  Updates progress bar display
REM  参数:
REM    value - 当前值 / Current value
REM    max   - 最大值 / Maximum value
REM  返回: 无 / Return: None
REM
