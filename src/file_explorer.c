#include <unistd.h>
#include <dirent.h>
//#include <stdio.h>
#include <stdlib.h>
#include "../include/termbox.h"
#include "../include/strings.h"
#include "../include/types.h"

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
    u32 type;
} Line;

typedef struct 
{
    u32 current_line;
    u32 num_lines;
    u32 view_range_start;
    // view_range_end is one more than the last line with visible text
    u32 view_range_end;

    String **buffer;
} Buffer;

void sort_buffer(Buffer *screen)
{
    u32 length = screen->num_lines;

    for(u32 i = 0; i < length - 1; i++)
    {
        for(u32 j = 0; j < length - 1 - i; j++)
        {
            if(!string_compare(screen->buffer[j], screen->buffer[j + 1]))
            {
                String *temp = screen->buffer[j];
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
        String *line = screen->buffer[y];
        for(u32 x = 0; x < line->length; x++)
        {
            u32 tb_index = x + tb_width() * y;
            tb_buffer[tb_index].ch = (u32)line->start[x];
            tb_buffer[tb_index].fg = TB_WHITE;
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

void load_directory(char *path, Buffer *screen)
{
    clear_tb_buffer();
    for(u32 i = 0; i < screen->num_lines; i++)
    {
        string_free(screen->buffer[i]);
    }
    struct dirent *dir;
    DIR *cwd = opendir(path);
    u32 index = 0;
    screen->num_lines = 0;
    screen->current_line = 0;
    while(dir = readdir(cwd))
    {
        screen->buffer[index++] = string_from(dir->d_name); 
        screen->num_lines++;
    }

    sort_buffer(screen);
    closedir(cwd);
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
    screen.buffer = (String**)malloc(sizeof(String*) * 100);
    screen.view_range_end = tb_height();
    load_directory(path, &screen);
    for(;;)
    {
        update_screen(&screen);
        tb_poll_event(&event);
        if((u8)event.ch == 'j')
        {
            screen.current_line = (screen.current_line + 1) % screen.num_lines;
        }
        else if((u8)event.ch == 'k')
        {
            if(screen.current_line == 0)
            {
                screen.current_line = screen.num_lines - 1;
            }
            else
            {
                screen.current_line--;
            }
        }
        else if((u8)event.ch == 'h')
        {
            pop_directory(cur_directory);
            char *c_dir = string_cstring(cur_directory);
            load_directory(c_dir, &screen);
            free(c_dir);
        }
        else
        {
            break;
        }
    }
    tb_shutdown();
    return 0;
}
