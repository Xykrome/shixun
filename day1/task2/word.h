#ifndef WORD_H
#define WORD_H

#define MAX_WORD_LEN 100

/* 读取一个单词到 word 缓冲区，返回单词长度；若读到文件尾则返回 0 */
int read_word(char *word);

#endif