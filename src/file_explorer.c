#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include "../include/termbox.h"
#include "../include/strings.h"
#include "../include/types.h"

typedef struct 
{
    u32 current_line;
    u32 num_lines;
    u32 view_range_start;
    // view_range_end is one more than the last line with visible text
    u32 view_range_end;

    String **buffer;
} Buffer;

void update_screen(Buffer *screen)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();
    for(u32 y = screen->view_range_start; y < screen->num_lines; y++)
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

int main()
{
    tb_init();
    struct tb_event event = {};
    struct dirent *dir;
    char path[128];
    size_t size = 128;
    getcwd(path, size);
    DIR *cwd = opendir(path);
    Buffer screen = {};
    screen.buffer = (String**)malloc(sizeof(String*) * 100);
    screen.view_range_end = tb_height() + 1;
    u32 index = 0;
    while(dir = readdir(cwd))
    {
        screen.buffer[index++] = string_from(dir->d_name); 
        screen.num_lines++;
    }

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
        else
        {
            break;
        }
    }
    tb_shutdown();
    return 0;
}
