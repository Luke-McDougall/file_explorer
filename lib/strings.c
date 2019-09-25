#include <stdlib.h>
#include "../include/strings.h"

String*
string_new(unsigned int capacity)
{
    String *str = (String*)malloc(sizeof(String));
    str -> start = (char*)malloc(sizeof(char) * capacity);
    str -> capacity = capacity;
    str -> length = 0;
    return str;
}

String*
string_from(const char *c_str)
{
    int length = 0;
    String *str = (String*)malloc(sizeof(String));
    while(c_str[length] != '\0') {length++;}
    str -> capacity = 2 * length; 
    str -> length = length;
    str -> start = (char*)malloc(sizeof(char) * str -> capacity);
    for(int i = 0; i < length; i++)
    {
        str -> start[i] = c_str[i];
    }
    return str;
}

void
string_concat(String *str1, String *str2)
{
    int length1 = str1->length, length2 = str2->length;
    int total_length = length1 + length2;
    if(total_length <= str1->capacity)
    {
        for(int i = length1; i < total_length; i++)
        {
            str1->start[i] = str2->start[i - length1];
        }
        str1->length = total_length;
    }
    else
    {
        char *new_str = (char*)malloc(sizeof(char) * total_length * 2);
        for(int i = 0; i < length1; i++)
        {
            new_str[i] = str1->start[i];
        }
        for(int j = 0; j < length2; j++)
        {
            new_str[j + length1] = str2->start[j];
        }
        free(str1->start);
        str1->start = new_str;
        str1->length = total_length;
        str1->capacity = total_length * 2;
    }
}

int
string_contains(String *str, const char *literal)
{
    int l_length = 0;
    // Determine length of literal, we don't use strlen in this house
    while(literal[l_length] != '\0') {l_length++;}
    if(l_length == 0) {return 0;}

    for(int i = 0; i < str->length; i++)
    {
        if(i + l_length > str-> length) {return 0;}

        if(str->start[i] == literal[0])
        {
            // If l_length is 1 then we know str contains literal
            // this also avoids SEGFAULTS or weird behaviour in 
            // the while loop
            if(l_length == 1) {return 1;}

            int l_idx = 1;
            while(str->start[i + l_idx] == literal[l_idx])
            {
                l_idx++;
                if(l_idx >= l_length) {return 1;}
            }
        }
    }
    return 0;
}

void
string_to_lowercase(String *str)
{
    for(int i = 0; i < str->length; i++)
    {
        char c = str->start[i];
        if(c >= 'A' && c <= 'Z') {str->start[i] += 32;}
    }
}

String*
string_copy(String *str)
{
    if(!str || str->length == 0)
    {
        return NULL;
    }

    char *copy = (char*)malloc(sizeof(char) * str->capacity);
    String *copy_string = (String*)malloc(sizeof(String));
    copy_string->capacity = str->capacity;
    copy_string->length = str->length;
    copy_string->start = copy;

    for(int i = 0; i < str->length; i++)
    {
        copy_string->start[i] = str->start[i];
    }
    return copy_string;
}

// Compare two strings alphabetically. Returns true if str1 is less than str2 alphabetically
b32
string_compare(String *str1, String *str2)
{
    String *smaller = str1->length < str2->length ? str1 : str2;

    for(u32 i = 0; i < smaller->length; i++)
    {
        u8 ch1 = (str1->start[i] >= 'A' && str1->start[i] <= 'Z') ? str1->start[i] + 32 : str1->start[i];
        u8 ch2 = (str2->start[i] >= 'A' && str2->start[i] <= 'Z') ? str2->start[i] + 32 : str2->start[i];
        i8 diff = ch1 - ch2;
        if(diff == 0 || diff == 32 || diff == -32) continue;
        if(diff < 0) return true;
        if(diff > 0) return false;
    }
    return smaller == str1;
}

void
string_cstring(String *str, char *c_str, size_t length)
{
    if(length > str->length)
    {
        for(u32 i = 0; i < str->length; i++)
        {
            c_str[i] = str->start[i];
        }
        c_str[str->length] = '\0';
    }
}

void
string_replace(String *str, char *c_str, size_t length)
{
    if(str->capacity >= length)
    {
        str->length = length;
        for(u32 i = 0; i < length; i++)
        {
            str->start[i] = c_str[i];
        }
    }
    else
    {
        char *new_str = (char*)malloc(sizeof(char) * length * 2);
        for(u32 i = 0; i < length; i++)
        {
            new_str[i] = c_str[i];
        }
        str->length = length;
        str->capacity = length * 2;
        free(str->start);
        str->start = new_str;
    }
}

void
string_push(String *str, char c)
{
    if(str->length < str->capacity)
    {
        str->start[str->length++] = c;
    }
    else
    {
        char *new_str = (char*)malloc(sizeof(char) * (str->capacity + 1) * 2);
        for(u32 i = 0; i < str->capacity; i++)
        {
            new_str[i] = str->start[i];
        }
        new_str[str->capacity] = c;
        str->length++;
        str->capacity = str->length * 2;
        free(str->start);
        str->start = new_str;
    }
}

void
string_free(String *str)
{
    free(str->start);
    free(str);
}
