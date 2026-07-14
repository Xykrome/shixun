#include <stdio.h>
#include <stdlib.h>
#include "word.h"
#include "line.h"

int main() {
    char word[MAX_WORD_LEN];
    clear_line();

    while (1) {
        int len = read_word(word);
        if (len == 0) {
            // 文件结束，输出最后一行（不调整）
            write_line_no_adjust();
            break;
        }
        // 尝试添加单词，如果放不下则先输出当前行并清空，再添加
        if (!add_word(word)) {
            write_line();        // 输出调整后的当前行
            // 清空后，将当前单词加入新行
            clear_line();
            add_word(word);
        }
    }
    return 0;
}
// test