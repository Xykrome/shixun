#include "line.h"
#include <stdio.h>
#include <string.h>

static char line[MAX_LINE_LEN + 1];   // 当前行缓冲区
static int line_len = 0;              // 当前行的字符数（不含末尾空格）
static int word_count = 0;            // 当前行已有的单词数

void clear_line() {
    line[0] = '\0';
    line_len = 0;
    word_count = 0;
}

int add_word(const char *word) {
    int word_len = strlen(word);
    // 如果行不为空，需要加一个空格分隔
    int need = (line_len == 0) ? word_len : (line_len + 1 + word_len);
    if (need > MAX_LINE_LEN) {
        return 0;   // 放不下
    }
    // 添加单词
    if (line_len > 0) {
        strcat(line, " ");
        line_len++;
    }
    strcat(line, word);
    line_len += word_len;
    word_count++;
    return 1;
}

void write_line() {
    if (word_count == 0) return;  // 空行不输出
    
    // 计算需要添加的总空格数
    int total_spaces = MAX_LINE_LEN - line_len;
    // 单词之间的间隙数
    int gaps = word_count - 1;
    
    if (gaps == 0) {
        // 只有一个单词，右对齐？实际要求左对齐，我们直接输出
        printf("%s\n", line);
    } else {
        // 每个间隙至少加 1 个空格，剩余的从左边开始分配
        int extra = total_spaces / gaps;
        int remainder = total_spaces % gaps;
        
        // 将 line 拆成单词，重新拼接
        char *words[100];
        int count = 0;
        char *token = strtok(line, " ");
        while (token != NULL) {
            words[count++] = token;
            token = strtok(NULL, " ");
        }
        // 重新构建输出
        for (int i = 0; i < count; i++) {
            printf("%s", words[i]);
            if (i < count - 1) {
                // 输出空格：基础空格 + 额外分配
                int spaces = 1 + extra + (i < remainder ? 1 : 0);
                for (int s = 0; s < spaces; s++) putchar(' ');
            }
        }
        putchar('\n');
    }
    clear_line();
}

void write_line_no_adjust() {
    if (line_len > 0) {
        printf("%s\n", line);
        clear_line();
    }
}

int is_line_empty() {
    return line_len == 0;
}