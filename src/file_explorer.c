#define _GNU_SOURCE
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include "../include/termbox.h"
#include "../include/strings.h"
#include "../include/types.h"

#define SCROLL_SPEED 1

typedef enum
{
    INSERT,
    SEARCH,
    NORMAL,
    VISUAL,
} State;

typedef struct
{
    String *text;
    u8 is_dir;
} Line;

typedef struct 
{
    i32 current_line;
    u32 num_lines;
    u32 view_range_start;
    // All Lines before this index are directories
    u32 files_start;
    // view_range_end is one more than the last line with visible text
    u32 view_range_end;

    Line *buffer;
} Buffer;

// Bubble sort for now, might change this later if it becomes a problem
void sort_buffer(Buffer *screen)
{
    u32 length = screen->num_lines;

    for(u32 i = 0; i < length - 1; i++)
    {
        for(u32 j = 0; j < length - 1 - i; j++)
        {
            if(!string_compare(screen->buffer[j].text, screen->buffer[j + 1].text))
            {
                Line temp = screen->buffer[j];
                screen->buffer[j] = screen->buffer[j + 1];
                screen->buffer[j + 1] = temp;
            }
        }
    }
}

void clear_tb_buffer()
{
    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 length = tb_width() * tb_height();
    for(u32 i = 0; i < length; i++)
    {
        tb_buffer[i].ch = (u32)' ';
        tb_buffer[i].bg = TB_BLACK;
    }
    tb_present();
}

void update_screen(Buffer *screen)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 end = screen->num_lines < screen->view_range_end ? screen->num_lines : screen->view_range_end;
    for(u32 y = screen->view_range_start; y < end; y++)
    {
        Line line = screen->buffer[y];
        for(u32 x = 0; x < line.text->length; x++)
        {
            u32 tb_index = x + tb_width() * (y - screen->view_range_start);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            tb_buffer[tb_index].fg = line.is_dir ? TB_RED : TB_WHITE;
            tb_buffer[tb_index].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }
    }
    tb_present();
}

int pop_directory(String *path)
{
    i32 index = (i32)(path->length - 1);

    for(;;)
    {
        char c = path->start[index];
        index--;
        path->length--;
        if(c == '/') return 1;
        if(index < 0) return 0;
    }
}

void push_directory(String *path, String *dir)
{
    string_push(path, '/');
    string_concat(path, dir);
}

void load_directory(char *path, Buffer *screen)
{
    clear_tb_buffer();
    struct dirent *dir;
    DIR *cwd = opendir(path);
    u32 index = 0;
    screen->num_lines = 0;
    screen->current_line = 0;
    while(dir = readdir(cwd))
    {
        if(screen->buffer[index].text == NULL)
        {
            screen->buffer[index].text = string_from(dir->d_name); 
        }
        else
        {
            string_replace(screen->buffer[index].text, dir->d_name, strlen(dir->d_name));
        }
        screen->buffer[index].is_dir = dir->d_type == DT_DIR;
        index++;
        screen->num_lines++;
    }

    sort_buffer(screen);
    screen->view_range_end = tb_height();
    screen->view_range_start = 0;
    closedir(cwd);
}

void scroll(Buffer *screen, i32 lines)
{
    i32 new_start = (i32)screen->view_range_start + lines;
    i32 new_end = (i32)screen->view_range_end + lines;
    if(new_start >= 0 && new_end <= screen->num_lines)
    {
        screen->view_range_start = (u32)new_start;
        screen->view_range_end = (u32)new_end;
        clear_tb_buffer();
    }
}

u32 negative_modulo(i32 max, i32 val)
{
    i32 result;
    for(;;)
    {
        result = max + val;
        if(result >= 0) return (u32)result;
        val = result;
    }
}

int main()
{
    tb_init();
    struct tb_event event = {};
    char path[128];
    size_t size = 128;
    getcwd(path, size);
    String *cur_directory = string_from(path);
    Buffer screen = {};
    screen.buffer = (Line*)calloc(100, sizeof(Line));
    screen.view_range_end = tb_height();
    load_directory(path, &screen);
    for(;;)
    {
        update_screen(&screen);
        tb_poll_event(&event);
        if((u8)event.ch == 'j')
        {
            screen.current_line = (screen.current_line + 1) % screen.num_lines;
            if(screen.current_line >= screen.view_range_end) scroll(&screen, SCROLL_SPEED);
            if(screen.current_line == 0) 
            {
                clear_tb_buffer();
                screen.view_range_start = 0;
                screen.view_range_end = tb_height();
            }
        }
        else if((u8)event.ch == 'k')
        {
            if(screen.current_line <= 0)
            {
                screen.current_line = negative_modulo(screen.current_line, (i32)screen.num_lines - 1);
                if(screen.num_lines > tb_height())
                {
                    clear_tb_buffer();
                    screen.view_range_start = screen.num_lines - tb_height();
                    screen.view_range_end = screen.num_lines;
                }
            }
            else
            {
                screen.current_line--;
            }
            if(screen.current_line < screen.view_range_start) scroll(&screen, -SCROLL_SPEED);
        }
        else if((u8)event.ch == 'h')
        {
            pop_directory(cur_directory);
            string_cstring(cur_directory, path, size);
            load_directory(path, &screen);
        }
        else if((u8)event.ch == 'l')
        {
            push_directory(cur_directory, screen.buffer[screen.current_line].text);
            string_cstring(cur_directory, path, size);
            load_directory(path, &screen);
        }
        else
        {
            break;
        }
    }
    for(int i = 0; i < 100; i++)
    {
        if(screen.buffer[i].text != NULL)
        {
            string_free(screen.buffer[i].text);
        }
    }
    string_free(cur_directory);
    free(screen.buffer);
    tb_shutdown();
    return 0;
}
