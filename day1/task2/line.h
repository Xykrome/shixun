#ifndef LINE_H
#define LINE_H

#define MAX_LINE_LEN 60   // 每行最大字符数

/* 清空行缓冲区 */
void clear_line();

/* 向行缓冲区添加一个单词，如果加入后超过限制则返回 0，否则返回 1 */
int add_word(const char *word);

/* 输出当前行，并调整（在单词间加空格使长度达到 MAX_LINE_LEN）*/
void write_line();

/* 输出当前行，但不调整（用于最后一行）*/
void write_line_no_adjust();

/* 检查当前行是否为空 */
int is_line_empty();

#endif