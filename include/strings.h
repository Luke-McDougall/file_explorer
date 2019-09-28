#include "types.h"
typedef struct
{
    char *start;
    unsigned int capacity;
    unsigned int length;
} String;

String* string_new(unsigned int);
String* string_from(const char*);
void string_concat(String *str1, String *str2);
int string_contains(String*, const char*);
void string_to_lowercase(String*);
String* string_copy(String*);
b32 string_compare(String*, String*);
void string_cstring(String*, char*, size_t);
void string_replace(String*, char*, size_t);
void string_push(String*, char);
void string_push_str(String*, char*, size_t);
void string_pop(String*);
void string_free(String*);
