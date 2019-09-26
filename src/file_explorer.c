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
} Mode;

typedef struct
{
    String *text;
    u8 is_dir;
} Line;

typedef struct 
{
    // x, y coordinates of the top left of the buffer
    // plus width and height of the buffer.
    u32 x;
    u32 y;
    u32 height;
    u32 width;

    i32 current_line;
    u32 num_lines;
    u32 view_range_start;
    // All Lines before this index are directories
    u32 files_start;
    // view_range_end is one more than the last line with visible text
    // should always be view_range_start + height
    u32 view_range_end;

    Line *buffer;
} Buffer;

static String *global_current_directory;
static Mode global_mode;

void draw_title()
{
    static char title[19] = "Current Directory:";
    for(u32 i = 0; i < 18; i++)
    {
        tb_change_cell(i, 0, (u32)title[i], TB_WHITE, TB_BLACK);
    }
    for(u32 i = 0; i < global_current_directory->length; i++)
    {
        tb_change_cell(18 + i, 0, (u32)global_current_directory->start[i], TB_WHITE, TB_BLACK);
    }
    tb_present();
}

void draw_query(String *query, u32 x, u32 y)
{
    for(u32 i = 0; i < query->length; i++)
    {
        tb_change_cell(x + i, y, (u32)query->start[i], TB_WHITE, TB_BLACK);
    }
    tb_present();
}

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
    draw_title();
    u32 buffer_x = screen->x, buffer_y = screen->y; 
    u32 buffer_width = screen->width, buffer_height = screen->height;

    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 end = screen->num_lines < screen->view_range_end ? screen->num_lines : screen->view_range_end;
    for(u32 y = screen->view_range_start; y < end; y++)
    {
        Line line = screen->buffer[y];
        u32 end_x = line.text->length < buffer_width ? line.text->length : buffer_width;
        for(u32 x = 0; x < end_x; x++)
        {
            u32 tb_index = x + buffer_x + tb_width() * (y - screen->view_range_start + buffer_y);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            tb_buffer[tb_index].fg = line.is_dir ? TB_RED : TB_WHITE;
            tb_buffer[tb_index].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }
    }
    tb_present();
}

void update_search_screen(Buffer *screen, String *query)
{
    draw_title();
    u32 buffer_x = screen->x, buffer_y = screen->y; 
    u32 buffer_width = screen->width, buffer_height = screen->height;
    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 end = screen->num_lines < screen->view_range_end ? screen->num_lines : screen->view_range_end;
    static char c_query[128];
    string_cstring(query, c_query, 128);
    for(u32 y = screen->view_range_start; y < end; y++)
    {
        Line line = screen->buffer[y];
        u32 end_x = line.text->length < buffer_width ? line.text->length : buffer_width;
        //u64 color_mask = 0;
        for(u32 x = 0; x < end_x; x++)
        {
            if(string_contains(line.text, c_query))
            {
                u32 tb_index = x + buffer_x + tb_width() * (y - screen->view_range_start + buffer_y);
                tb_buffer[tb_index].ch = (u32)line.text->start[x];
                tb_buffer[tb_index].fg = line.is_dir ? TB_RED : TB_WHITE;
                tb_buffer[tb_index].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
            }
        }
    }
    tb_present();
}

int pop_directory(String *path)
{
    i32 index = (i32)(path->length - 1);
    char c;

    while(index >= 0)
    {
        c = path->start[index];
        index--;
        path->length--;
        if(c == '/') return 1;
    }
    return 0;
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
    screen->view_range_end = screen->height;
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
    i32 result = val;
    while(result < 0)
    {
        result = max + val;
        val = result;
    }
    return (u32)result;
}

int main()
{
    tb_init();
    global_mode = NORMAL;
    struct tb_event event = {};
    char path[256];
    size_t size = 256;
    getcwd(path, size);
    global_current_directory = string_from(path);
    String *search_query = NULL;

    Buffer screen = {};
    screen.buffer = (Line*)calloc(100, sizeof(Line));
    screen.y = 1;
    screen.height = tb_height() / 2;
    screen.x = 0;
    screen.width = tb_width();
    screen.view_range_end = screen.height;
    load_directory(path, &screen);
    update_screen(&screen);
    b32 running = true;
    while(running)
    {
        tb_poll_event(&event);
        switch(global_mode)
        {
            case NORMAL:
            {
                if((u8)event.ch == 'j')
                {
                    screen.current_line = (screen.current_line + 1) % screen.num_lines;
                    if(screen.current_line >= screen.view_range_end) scroll(&screen, SCROLL_SPEED);
                    if(screen.current_line == 0) 
                    {
                        clear_tb_buffer();
                        screen.view_range_start = 0;
                        screen.view_range_end = screen.height;
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
                            screen.view_range_start = screen.num_lines - screen.height;
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
                    pop_directory(global_current_directory);
                    string_cstring(global_current_directory, path, size);
                    load_directory(path, &screen);
                }
                else if((u8)event.ch == 'l')
                {
                    if(screen.buffer[screen.current_line].is_dir)
                    {
                        push_directory(global_current_directory, screen.buffer[screen.current_line].text);
                        string_cstring(global_current_directory, path, size);
                        load_directory(path, &screen);
                    }
                }
                else if((u8)event.ch == 's')
                {
                    global_mode = SEARCH;
                }
                else
                {
                    running = false;
                }
                update_screen(&screen);
            } break;

            case SEARCH:
            {
                if((u8)event.ch >= 0x21 && (u8)event.ch <= 0x7E)
                {
                    clear_tb_buffer();
                    if(!search_query)
                    {
                        search_query = string_new(20);
                    }
                    string_push(search_query, (u8)event.ch);
                    draw_query(search_query, 0, screen.y + screen.height);
                    update_search_screen(&screen, search_query);
                }
                else if(event.key == TB_KEY_SPACE)
                {
                    clear_tb_buffer();
                    if(!search_query)
                    {
                        search_query = string_new(20);
                    }
                    string_push(search_query, ' ');
                    draw_query(search_query, 0, screen.y + screen.height);
                    update_search_screen(&screen, search_query);
                }
                else if(event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2)
                {
                    if(search_query)
                    {
                        tb_change_cell(search_query->length - 1, screen.y + screen.height, (u32)' ', TB_BLACK, TB_BLACK);
                        string_pop(search_query);
                        tb_present();
                    }
                    update_search_screen(&screen, search_query);
                }
                else if(event.key == TB_KEY_ESC)
                {
                    if(search_query)
                    {
                        while(search_query->length > 0)
                        {
                            tb_change_cell(search_query->length - 1, screen.y + screen.height, (u32)' ', TB_BLACK, TB_BLACK);
                            search_query->length--;
                        }
                        tb_present();
                    }
                    global_mode = NORMAL;
                    clear_tb_buffer();
                    update_screen(&screen);
                }
            } break;
        }
    }
    for(int i = 0; i < 100; i++)
    {
        if(screen.buffer[i].text != NULL)
        {
            string_free(screen.buffer[i].text);
        }
    }
    string_free(global_current_directory);
    free(screen.buffer);
    tb_shutdown();
    return 0;
}
