"""提取列首带类型的 C 函数定义，避免把调用方 if/while 块当作定义。"""
import re


def function(source, name):
    """屏蔽注释和字符串后定位列首函数定义及匹配花括号，返回准确原始函数字节。"""
    signature = (r'^(?!(?:if|while|for|switch|return|else)\b)'
                 r'(?:__attribute__\s*\(\(\s*weak\s*\)\)\s+)?'
                 r'[A-Za-z_][\w \t*]*[ \t*]' + re.escape(name) +
                 r'\([^;{}]*\)\s*\{')
    matches = list(re.finditer(signature, source, re.M))
    if len(matches) != 1:
        raise AssertionError(('production definition count', name, len(matches)))
    start = matches[0].start()
    index = matches[0].end() - 1
    depth = 0
    while index < len(source):
        char = source[index]
        if source.startswith('//', index):
            end = source.find('\n', index + 2)
            index = len(source) if end < 0 else end
        elif source.startswith('/*', index):
            end = source.find('*/', index + 2)
            if end < 0: raise AssertionError('Unterminated comment: '+name)
            index = end + 2
        elif char in ('"', "'"):
            quote = char
            index += 1
            while index < len(source):
                if source[index] == '\\': index += 2
                elif source[index] == quote:
                    index += 1
                    break
                else: index += 1
            else: raise AssertionError('Unterminated literal: '+name)
        else:
            if char == '{': depth += 1
            elif char == '}':
                depth -= 1
                if depth == 0: return source[start:index+1]
            index += 1
    raise AssertionError('Unterminated production definition: '+name)


def self_test():
    """以注释、字符串和相似调用负例核对 C 函数提取边界。"""
    source = '''static void caller(void) {
    if (wanted(1) != 0) { do_something(); }
}
static int wanted(int x);
static int wanted(int x) { /* } */ const char *s="{\\\"}"; // }
    if (x) { return s[0]; } return 0;
}
'''
    extracted = function(source, 'wanted')
    assert extracted.startswith('static int wanted(int x) {')
    assert extracted.endswith('return 0;\n}')
    assert function('const char *ptr(void) {return "}";}', 'ptr').endswith(';}')
    weak='__attribute__((weak)) int decrypt(const char *p)\n{ return p ? 0 : -1; }'
    assert function(weak,'decrypt')==weak
    try: function('__attribute__((weak)) int decrypt(const char *p);','decrypt')
    except AssertionError: pass
    else: raise AssertionError('Attributed prototype accepted as a body')
    for invalid in (source[:source.index('static int wanted(int x) {')], source+extracted):
        try: function(invalid, 'wanted')
        except AssertionError: pass
        else: raise AssertionError('Missing/duplicate definition accepted')


if __name__ == '__main__':
    self_test()
    print('PASS production definition extraction: callers, prototypes, literals, comments, ambiguity')
