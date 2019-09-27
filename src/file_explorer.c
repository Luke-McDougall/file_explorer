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
    String *text;
    u32 original_line_number;
    u8 is_dir;
    u64 color_mask;
} Result;

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

// NOTE(Luke): Remember this buffer should only contain strings also stored in the main buffer
// so don't free them twice!
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

    Result *buffer;
} SearchBuffer;

static String *global_current_directory;
static Mode global_mode;

void draw_title()
{
    static char title[19] = "Current Directory:";
    static u32 prev_length = 0;
    if(prev_length != 0)
    {
        for(u32 i = 0; i < prev_length; i++)
        {
            tb_change_cell(18 + i, 0, (u32)' ', TB_WHITE, TB_BLACK);
        }
    }
    prev_length = global_current_directory->length;

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
    if(query->length == 0) return;
    for(u32 i = 0; i < query->length; i++)
    {
        tb_change_cell(x + i, y, (u32)query->start[i], TB_WHITE, TB_BLACK);
    }
    tb_present();
}

u64 search_test(String *file, String *query)
{
    if(query->length > file->length) return 0;
    u64 color_mask = 0;
    // This is to make sure the bit shifting doesn't produce fucked values
    u64 one = 1;
    u32 index = 0;
    u32 num_matched = 0;
    for(u32 i = 0; i < query->length; i++)
    {
        while(index < file->length)
        {
            u8 c1 = query->start[i];
            u8 c2 = file->start[index];
            if(c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if(c2 >= 'A' && c2 <= 'Z') c2 += 32;
            i8 diff = c1 - c2;
            if(diff == 0)
            {
                color_mask |= (one << index);
                if(++num_matched == query->length) return color_mask;
                index++;
                break;
            }
            index++;
        }
    }
    return 0;
}

void exec_search(Buffer *screen, SearchBuffer *results, String *query)
{
    if(query->length == 0)
    {
        results->num_lines = screen->num_lines;
        for(u32 i = 0; i < screen->num_lines; i++)
        {
            results->buffer[i].text = screen->buffer[i].text;
            results->buffer[i].original_line_number = i;
            results->buffer[i].is_dir = screen->buffer[i].is_dir;
            results->buffer[i].color_mask = 0;
        }
    }
    else
    {
        results->num_lines = 0;
        for(u32 i = 0; i < screen->num_lines; i++)
        {
            u64 color_mask = search_test(screen->buffer[i].text, query);
            if(color_mask)
            {
                u32 index = results->num_lines;
                results->buffer[index].text = screen->buffer[i].text;
                results->buffer[index].original_line_number = i;
                results->buffer[index].is_dir = screen->buffer[i].is_dir;
                results->buffer[index].color_mask = color_mask;
                results->num_lines++;
            }
        }
    }
    results->view_range_start = 0;
    results->view_range_end = results->height;
}

void background(u16 bg)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 length = tb_width() * tb_height();
    for(u32 i = 0; i < length; i++)
    {
        tb_buffer[i].ch = (u32)' ';
        tb_buffer[i].bg = bg;
    }
    tb_present();
}

void clear_buffer_area_normal(Buffer *screen)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();

    for(u32 y = screen->y; y < screen->y + screen->height; y++)
    {
        for(u32 x = screen->x; x < screen->x + screen->width; x++)
        {
            u32 tb_index = x + tb_width() * y;
            tb_buffer[tb_index].ch = (u32)' ';
            tb_buffer[tb_index].bg = TB_BLACK;
        }
    }
    tb_present();
}

void clear_buffer_area_search(SearchBuffer *results, u32 query_length)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();

    // Clear query
    for(u32 x = results->x; x < results->x + query_length; x++)
    {
        u32 tb_index = x + tb_width() * (results->y - 1);
        tb_buffer[tb_index].ch = (u32)' ';
        tb_buffer[tb_index].bg = TB_BLACK;
    }

    // Clear rest of buffer
    for(u32 y = results->y; y < results->y + results->height; y++)
    {
        for(u32 x = results->x; x < results->x + results->width; x++)
        {
            u32 tb_index = x + tb_width() * y;
            tb_buffer[tb_index].ch = (u32)' ';
            tb_buffer[tb_index].bg = TB_BLACK;
        }
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
        // TODO(Luke): Make this robust
        if(line.is_dir)
        {
            u32 end_line = line.text->length + buffer_x + tb_width() * (y - screen->view_range_start + buffer_y);
            tb_buffer[end_line].ch = (u32)'/';
            tb_buffer[end_line].fg = TB_WHITE;
            tb_buffer[end_line].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }
        u32 end_x = line.text->length < buffer_width ? line.text->length : buffer_width;
        for(u32 x = 0; x < end_x; x++)
        {
            u32 tb_index = x + buffer_x + tb_width() * (y - screen->view_range_start + buffer_y);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            tb_buffer[tb_index].fg = TB_WHITE;
            tb_buffer[tb_index].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }
    }
    tb_present();
}

void update_search_screen(SearchBuffer *results)
{
    draw_title();
    u32 buffer_x = results->x, buffer_y = results->y; 
    u32 buffer_width = results->width, buffer_height = results->height;

    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 end = results->num_lines < results->view_range_end ? results->num_lines : results->view_range_end;
    for(u32 y = results->view_range_start; y < end; y++)
    {
        Result line = results->buffer[y];
        // TODO(Luke): Make this robust
        if(line.is_dir)
        {
            u32 end_line = line.text->length + buffer_x + tb_width() * (y - results->view_range_start + buffer_y);
            tb_buffer[end_line].ch = (u32)'/';
            tb_buffer[end_line].fg = TB_WHITE;
            tb_buffer[end_line].bg = y == results->current_line ? TB_BLUE : TB_BLACK;
        }
        u32 end_x = line.text->length < buffer_width ? line.text->length : buffer_width;
        for(u32 x = 0; x < end_x; x++)
        {
            u32 tb_index = x + buffer_x + tb_width() * (y - results->view_range_start + buffer_y);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            u16 fg = TB_WHITE;
            if((line.color_mask >> x) & 1) fg = TB_RED;
            tb_buffer[tb_index].fg = fg;
            tb_buffer[tb_index].bg = y == results->current_line ? TB_BLUE : TB_BLACK;
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
    clear_buffer_area_normal(screen);
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
    closedir(cwd);

    screen->view_range_end = screen->height;
    screen->view_range_start = 0;

    // Segregate directories and regular files
    u32 dir_end = 0;
    u32 length = screen->num_lines;
    for(u32 index = 0; index < length; index++)
    {
        if(screen->buffer[index].is_dir)
        {
            Line temp = screen->buffer[dir_end];
            screen->buffer[dir_end] = screen->buffer[index];
            screen->buffer[index] = temp;
            dir_end++;
        }
    }
    screen->files_start = dir_end;

    // Sort directory portion
    for(u32 i = 0; i < dir_end - 1; i++)
    {
        for(u32 j = 0; j < dir_end - 1 - i; j++)
        {
            if(!string_compare(screen->buffer[j].text, screen->buffer[j + 1].text))
            {
                Line temp = screen->buffer[j];
                screen->buffer[j] = screen->buffer[j + 1];
                screen->buffer[j + 1] = temp;
            }
        }
    }

    // Sort file portion
    for(u32 i = dir_end; i < length - 1; i++)
    {
        for(u32 j = dir_end; j < length - 1 - i + dir_end; j++)
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

void scroll(Buffer *screen, i32 lines)
{
    i32 new_start = (i32)screen->view_range_start + lines;
    i32 new_end = (i32)screen->view_range_end + lines;
    if(new_start >= 0 && new_end <= screen->num_lines)
    {
        screen->view_range_start = (u32)new_start;
        screen->view_range_end = (u32)new_end;
        clear_buffer_area_normal(screen);
    }
}

void jump_to_line(Buffer *screen, u32 line_number)
{
    screen->current_line = line_number;

    if(screen->current_line < screen->view_range_start)
    {
        i32 diff = screen->current_line - screen->view_range_start;
        scroll(screen, diff);
    }
    else if(screen->current_line >= screen->view_range_end)
    {
        i32 diff = screen->current_line - screen->view_range_end + 1;
        scroll(screen, diff);
    }
}

void search_scroll(SearchBuffer *results)
{
    results->view_range_start++;
    results->view_range_end++;
    clear_buffer_area_search(results, 0);
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

    SearchBuffer results = {};
    results.buffer = (Result*)calloc(100, sizeof(Result));
    results.y = tb_height() / 2 + 2;
    results.height = tb_height() / 2 - 2;
    results.x = 0;
    results.width = tb_width();
    results.view_range_end = results.y + results.height;

    background(TB_BLACK);
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
                        clear_buffer_area_normal(&screen);
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
                            clear_buffer_area_normal(&screen);
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
                else if((u8)event.ch == 'l' || event.key == TB_KEY_ENTER)
                {
                    if(screen.buffer[screen.current_line].is_dir)
                    {
                        push_directory(global_current_directory, screen.buffer[screen.current_line].text);
                        string_cstring(global_current_directory, path, size);
                        load_directory(path, &screen);
                    }
                }
                else if(event.key == TB_KEY_CTRL_D)
                {
                    jump_to_line(&screen, screen.files_start);
                }
                else if(event.key == TB_KEY_CTRL_U)
                {
                    jump_to_line(&screen, 0);
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
                    clear_buffer_area_search(&results, 0);
                    if(!search_query)
                    {
                        search_query = string_new(20);
                    }
                    string_push(search_query, (u8)event.ch);
                    draw_query(search_query, 0, results.y - 1);
                    exec_search(&screen, &results, search_query);
                    update_search_screen(&results);
                }
                else if(event.key == TB_KEY_SPACE)
                {
                    clear_buffer_area_search(&results, 0);
                    if(!search_query)
                    {
                        search_query = string_new(20);
                    }
                    string_push(search_query, ' ');
                    draw_query(search_query, 0, results.y - 1);
                    exec_search(&screen, &results, search_query);
                    update_search_screen(&results);
                }
                else if(event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2)
                {
                    if(search_query && search_query->length > 0)
                    {
                        clear_buffer_area_search(&results, search_query->length);
                        string_pop(search_query);
                        draw_query(search_query, 0, results.y - 1);
                        exec_search(&screen, &results, search_query);
                        update_search_screen(&results);
                    }
                }
                else if(event.key == TB_KEY_TAB)
                {
                    results.current_line = (results.current_line + 1) % results.num_lines;
                    if(results.current_line >= results.view_range_end) 
                    {
                        // search_scroll calls clear_buffer_area_search()
                        search_scroll(&results);
                    }
                    else if(results.current_line < results.view_range_start)
                    {
                        results.view_range_start = results.current_line;
                        results.view_range_end = results.current_line + results.height;
                        clear_buffer_area_search(&results, 0);
                    }
                    update_search_screen(&results);
                }
                else if(event.key == TB_KEY_ENTER)
                {
                    if(search_query && search_query->length > 0)
                    {
                        clear_buffer_area_search(&results, search_query->length);
                        search_query->length = 0;
                    }
                    else
                    {
                        clear_buffer_area_search(&results, 0);
                    }
                    jump_to_line(&screen, results.buffer[results.current_line].original_line_number);
                    global_mode = NORMAL;
                    update_screen(&screen);
                }
                else if(event.key == TB_KEY_ESC)
                {
                    if(search_query && search_query->length > 0)
                    {
                        clear_buffer_area_search(&results, search_query->length);
                        search_query->length = 0;
                    }
                    else
                    {
                        clear_buffer_area_search(&results, 0);
                    }
                    global_mode = NORMAL;
                    //clear_tb_buffer();
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
