#ifndef TEXTPARSEHELPER_H
#define TEXTPARSEHELPER_H
#include <stdbool.h>

#include "strQueue.h"

#define LINE_SEP "\n"
#define TOKEN_SEP " \t"

void to_lower(char* string);
void break_lines(str_queue_t* lineQueue, char* chunk);
void break_tokens_compress_space(str_queue_t* tokenQueue, char* chunk);


#endif
