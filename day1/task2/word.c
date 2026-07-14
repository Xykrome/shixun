#include "word.h"
#include <stdio.h>
#include <ctype.h>

int read_word(char *word) {
    int c, len = 0;
    // 跳过空白字符（空格、制表符、换行等）
    while ((c = getchar()) != EOF && isspace(c)) {
        // 跳过
    }
    if (c == EOF) {
        return 0;  // 没有单词
    }
    // 将第一个非空白字符存入
    word[len++] = c;
    // 继续读取直到遇到空白或文件尾，但最多 MAX_WORD_LEN-1 个字符
    while (len < MAX_WORD_LEN - 1 && (c = getchar()) != EOF && !isspace(c)) {
        word[len++] = c;
    }
    word[len] = '\0';
    return len;
}