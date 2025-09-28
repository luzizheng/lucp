# .cmake-format.py
def configure(config):
    # 缩进宽度
    config.indent = 4
    # 命令名称大小写（LOWER, UPPER, AS_IS）
    config.command_case = 'UPPER'
    # 列表换行策略
    config.line_width = 120
    # 括号内换行
    config.bracket_padding = True