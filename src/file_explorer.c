#define _GNU_SOURCE
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/termbox.h"
#include "../include/file_explorer.h"


// TODO(Luke):
// 1. Add more basic file operations. Copy, delete, move etc
// 2. Add vertical/horizontal splits. I'll need some mechanism to manage multiple buffers
// 3. Finish insert mode and implement visual mode.
// 4. For visual mode implement a way to highlight multiple lines
// 5. Deal with resize event eventually
// 6. General robustness/error handling stuff that will be very tedious


// Maybe ideas.
// 1. add an x offset field to Buffer

#define MAX_BUFFERS 2
#define TEXT_OFF 7

static u32 global_terminal_width;
static u32 global_terminal_height;

static u32 global_state_active_buffer;
static u32 global_state_num_buffers;
static Buffer **global_state_buffers;

static Mode global_mode;
static char global_path[256];
static size_t global_path_size = 256;

void panic(const char *error)
{
    tb_shutdown();
    fprintf(stderr, "%s\n", error);
    exit(1);
}


void draw_vertical_line(u32 y_start, u32 y_end, u32 x)
{
    for(u32 i = y_start; i < y_end; i++)
    {
        tb_change_cell(x, i, 1472, TB_BLACK, TB_WHITE);
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
                if(results->num_lines >= results->capacity)
                {
                    reallocate_search_buffer(results);
                }
            }
        }
    }

    u32 max_height            = screen->height / 4;
    results->view_range_start = 0;
    results->current_line     = 0;
    results->query_x          = screen->x;
    results->query_y          = screen->y + screen->height;
    results->x                = screen->x;
    results->width            = screen->width / 2;
    if(results->num_lines > max_height)
    {
        results->height = max_height;
        results->y = screen->y + screen->height - max_height;
    }
    else
    {
        results->height = results->num_lines;
        results->y = screen->y + screen->height - results->num_lines;
    }
    results->view_range_end = results->height;

    // Sort search results by string length. The idea is that shorter strings are closer matches than long strings
    // with this search system. And there's always more letters that you can add to close in on any longer strings
    for(u32 i = 1; i < results->num_lines; i++)
    {
        Result current = results->buffer[i];
        u32 current_length = current.text->length;
        u32 index = i;

        while(index > 0 && results->buffer[index - 1].text->length > current_length)
        {
            results->buffer[index] = results->buffer[index - 1];
            index--;
        }
        results->buffer[index] = current;
    }
}

void background(u16 bg)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 length = global_terminal_width * global_terminal_height;
    for(u32 i = 0; i < length; i++)
    {
        tb_buffer[i].ch = (u32)' ';
        tb_buffer[i].bg = bg;
    }
    tb_present();
}

void clear_normal_buffer_area(Buffer *screen)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();

    for(u32 y = screen->y; y < screen->y + screen->height; y++)
    {
        for(u32 x = screen->x; x < screen->x + screen->width; x++)
        {
            u32 tb_index = x + global_terminal_width * y;
            tb_buffer[tb_index].ch = (u32)' ';
            tb_buffer[tb_index].bg = TB_BLACK;
        }
    }
    tb_present();
}

// Pass zero for query_length to not clear the query
void clear_search_buffer_area(SearchBuffer *results, u32 query_length)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();
    u32 tb_index = global_terminal_width * results->query_y;

    // Clear query
    clear_text(results->query_x, results->query_y, query_length);

    // Clear rest of buffer
    tb_index = global_terminal_width * results->y;
    for(u32 y = results->y; y < results->y + results->height; y++)
    {
        for(u32 x = results->x; x < results->x + results->width; x++)
        {
            tb_buffer[tb_index + x].ch = (u32)' ';
            tb_buffer[tb_index + x].bg = TB_BLACK;
        }
        tb_index += global_terminal_width;
    }
    tb_present();
}

void update_screen(Buffer *screen)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();

    // Draw title
    {
        static char title[19] = "Current Directory:";

        for(u32 i = 0; i < 18; i++)
        {
            tb_change_cell(i + screen->x, screen->y, (u32)title[i], TB_WHITE, TB_BLACK);
        }
        for(u32 i = 0; i < screen->current_directory->length; i++)
        {
            tb_change_cell(i + screen->x + 18, screen->y, (u32)screen->current_directory->start[i], TB_WHITE, TB_BLACK);
        }
    }
    draw_text(NULL, screen->x, screen->y + screen->height);

    u32 end_y;
    if(screen->num_lines < screen->view_range_end)
    {
        end_y = screen->num_lines;
    }
    else
    {
        end_y = screen->view_range_end;
    }

    for(u32 y = screen->view_range_start; y < end_y; y++)
    {
        Line line = screen->buffer[y];
        // TODO(Luke): Make this robust
        if(line.is_dir)
        {
            u32 end_line = line.text->length + screen->x + global_terminal_width * (screen->y + y - screen->view_range_start + 1);
            tb_buffer[end_line].ch = (u32)'/';
            tb_buffer[end_line].fg = TB_WHITE;
            tb_buffer[end_line].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }

        u32 end_x;
        if(screen->x + screen->width < line.text->length)
        {
            end_x = screen->x + screen->width;
        }
        else
        {
            end_x = line.text->length;
        }

        for(u32 x = 0; x < end_x; x++)
        {
            u32 tb_index = screen->x + x + global_terminal_width * (screen->y + y - screen->view_range_start + 1);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            tb_buffer[tb_index].fg = TB_WHITE;
            tb_buffer[tb_index].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }
    }

    for(u32 i = 0; i < global_terminal_width; i++)
    {
        u32 index = i + global_terminal_width * (screen->y + screen->height - 1);
        tb_buffer[index].fg = TB_WHITE | TB_UNDERLINE;
    }

    tb_present();
}

void update_visual_screen(Buffer *screen, u32 start, u32 end)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();

    // Draw title
    {
        static char title[19] = "Current Directory:";

        for(u32 i = 0; i < 18; i++)
        {
            tb_change_cell(i + screen->x, screen->y, (u32)title[i], TB_WHITE, TB_BLACK);
        }
        for(u32 i = 0; i < screen->current_directory->length; i++)
        {
            tb_change_cell(i + screen->x + 18, screen->y, (u32)screen->current_directory->start[i], TB_WHITE, TB_BLACK);
        }
    }
    draw_text(NULL, screen->x, screen->y + screen->height);

    u32 end_y;
    if(screen->num_lines < screen->view_range_end)
    {
        end_y = screen->num_lines;
    }
    else
    {
        end_y = screen->view_range_end;
    }

    for(u32 y = screen->view_range_start; y < end_y; y++)
    {
        Line line = screen->buffer[y];
        // TODO(Luke): Make this robust
        if(line.is_dir)
        {
            u32 end_line = line.text->length + screen->x + global_terminal_width * (screen->y + y - screen->view_range_start + 1);
            tb_buffer[end_line].ch = (u32)'/';
            tb_buffer[end_line].fg = TB_WHITE;
            tb_buffer[end_line].bg = y == screen->current_line ? TB_BLUE : TB_BLACK;
        }

        u32 end_x;
        if(screen->x + screen->width < line.text->length)
        {
            end_x = screen->x + screen->width;
        }
        else
        {
            end_x = line.text->length;
        }

        for(u32 x = 0; x < end_x; x++)
        {
            u32 tb_index = screen->x + x + global_terminal_width * (screen->y + y - screen->view_range_start + 1);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            tb_buffer[tb_index].fg = TB_WHITE;
            if(y >= start && y < end)
            {
                tb_buffer[tb_index].bg = TB_BLUE;
            }
            else
            {
                tb_buffer[tb_index].bg = TB_BLACK;
            }
        }
    }

    for(u32 i = 0; i < global_terminal_width; i++)
    {
        u32 index = i + global_terminal_width * (screen->y + screen->height - 1);
        tb_buffer[index].fg = TB_WHITE | TB_UNDERLINE;
    }

    tb_present();
}

void update_search_screen(SearchBuffer *results)
{
    struct tb_cell *tb_buffer = tb_cell_buffer();

    // Draw query bar
    draw_text(results->query, results->query_x, results->query_y);

    u32 end;
    if(results->num_lines < results->view_range_end)
    {
        end = results->num_lines;
    }
    else
    {
        end = results->view_range_end;
    }

    for(u32 y = results->view_range_start; y < end; y++)
    {
        Result line = results->buffer[y];
        // TODO(Luke): Make this robust
        if(line.is_dir)
        {
            u32 end_line = line.text->length + results->x + global_terminal_width * (y - results->view_range_start + results->y);
            tb_buffer[end_line].ch = (u32)'/';
            tb_buffer[end_line].fg = TB_WHITE;
            tb_buffer[end_line].bg = y == results->current_line ? TB_BLUE : TB_BLACK;
        }

        u16 bg = y == results->current_line ? TB_MAGENTA : TB_WHITE;
        for(u32 x = 0; x < line.text->length; x++)
        {
            u32 tb_index = x + results->x + global_terminal_width * (y - results->view_range_start + results->y);
            tb_buffer[tb_index].ch = (u32)line.text->start[x];
            u16 fg = y == results->current_line ? TB_WHITE : TB_BLACK;
            if((line.color_mask >> x) & 1) fg |= TB_BOLD;
            tb_buffer[tb_index].fg = fg;
            tb_buffer[tb_index].bg = bg;
        }
        for(u32 x = line.text->length; x < results->width; x++)
        {
            u32 tb_index = x + results->x + global_terminal_width * (y - results->view_range_start + results->y);
            tb_buffer[tb_index].ch = (u32)' ';
            tb_buffer[tb_index].bg = bg;
        }
    }
    tb_present();
}

void draw_search_overlay(Buffer *screen, SearchBuffer *results)
{
    clear_normal_buffer_area(screen);
    u32 original_view_end = screen->view_range_end;
    if(screen->num_lines < screen->height - 1 - results->height)
    {
        update_screen(screen);
    }
    else
    {
        screen->view_range_end  = screen->view_range_start + (screen->height - 1 - results->height);
        update_screen(screen);
        screen->view_range_end = original_view_end;
    }
    update_search_screen(results);
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
    clear_normal_buffer_area(screen);
    struct dirent *dir;
    DIR *cwd = opendir(path);
    u32 index = 0;
    screen->num_lines = 0;
    screen->current_line = 0;
    while(dir = readdir(cwd))
    {
        if(index >= screen->capacity)
        {
            reallocate_buffer(screen);
        }

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

    screen->view_range_end = screen->height - 1;
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
    for(u32 i = 1; i < dir_end; i++)
    {
        Line val = screen->buffer[i];
        u32 index = i;

        while(index > 0 && !(string_compare(screen->buffer[index - 1].text, val.text)))
        {
            screen->buffer[index] = screen->buffer[index - 1];
            index--;
        }
        screen->buffer[index] = val;
    }

    // Sort file portion
    for(u32 i = dir_end + 1; i < length; i++)
    {
        Line val = screen->buffer[i];
        u32 index = i;

        while(index > dir_end && !(string_compare(screen->buffer[index - 1].text, val.text)))
        {
            screen->buffer[index] = screen->buffer[index - 1];
            index--;
        }
        screen->buffer[index] = val;
    }
}

void init_buffer(Buffer *buf, u32 x, u32 y, u32 width, u32 height, String *directory)
{
    buf->x                 = x;
    buf->y                 = y;
    buf->width             = width;
    buf->height            = height;
    buf->view_range_start  = 0;
    buf->view_range_end    = height - 1;
    buf->current_line      = 0;
    buf->current_directory = string_copy(directory);
    buf->buffer            = (Line*)calloc(100, sizeof(Line));
    buf->capacity          = 100;

    // Load buffers current directory
    string_cstring(directory, global_path, global_path_size);
    load_directory(global_path, buf);
}

void reallocate_buffer(Buffer *buf)
{
    Line *new_buffer = calloc(buf->num_lines * 2, sizeof(Line));

    for(u32 i = 0; i < buf->num_lines; i++)
    {
        new_buffer[i] = buf->buffer[i];
    }
    free(buf->buffer);
    buf->capacity *= 2;
    buf->buffer = new_buffer;
}

void reallocate_search_buffer(SearchBuffer *buf)
{
    Result *new_buffer = calloc(buf->num_lines * 2, sizeof(Result));

    for(u32 i = 0; i < buf->num_lines; i++)
    {
        new_buffer[i] = buf->buffer[i];
    }
    free(buf->buffer);
    buf->capacity *= 2;
    buf->buffer = new_buffer;
}

void scroll(Buffer *screen, i32 lines)
{
    i32 new_start = (i32)screen->view_range_start + lines;
    i32 new_end = (i32)screen->view_range_end + lines;
    if(new_start >= 0 && new_end <= screen->num_lines)
    {
        screen->view_range_start = (u32)new_start;
        screen->view_range_end = (u32)new_end;
        clear_normal_buffer_area(screen);
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
    clear_search_buffer_area(results, 0);
}

// Pass NULL for text to only print the global mode.
void draw_text(String *text, u32 x, u32 y)
{
    char *mode;
    u16 bg;

    switch(global_mode)
    {
        case NORMAL:
        mode = "NORMAL";
        bg = TB_BLUE;
        break;

        case SEARCH:
        mode = "SEARCH";
        bg = TB_MAGENTA;
        break;

        case INSERT:
        mode = "INSERT";
        bg = TB_GREEN;
        break;

        case VISUAL:
        mode = "VISUAL";
        bg = TB_YELLOW;
        break;
    }

    for(u32 i = 0; i < TEXT_OFF - 1; i++)
    {
        tb_change_cell(x + i, y, (u32)mode[i], TB_BLACK, bg);
    }

    if(text)
    {
        for(u32 i = 0; i < text->length; i++)
        {
            tb_change_cell(x + i + TEXT_OFF, y, (u32)text->start[i], TB_WHITE, TB_BLACK);
        }
    }

    tb_present();
}

void clear_text(u32 x, u32 y, u32 length)
{
    for(u32 i = 0; i < length; i++)
    {
        tb_change_cell(x + i + TEXT_OFF, y, (u32)' ', TB_WHITE, TB_BLACK);
    }
    tb_present();
}

void vertical_split(Buffer *buffer)
{
    if(global_state_num_buffers < MAX_BUFFERS)
    {
        u32 buffer_width = buffer->width / 2;
        u32 buffer_height = buffer->height;
        u32 x_off = buffer->x;
        u32 y_off = buffer->y;

        buffer->width = buffer_width;

        Buffer *buffer2 = (Buffer*)malloc(sizeof(Buffer));

        init_buffer(buffer2, x_off * 2 + buffer_width, y_off, buffer_width, buffer_height, buffer->current_directory);
        global_state_buffers[global_state_num_buffers++] = buffer2;

        update_screen(buffer);
        update_screen(buffer2);
        draw_vertical_line(0, global_terminal_height, buffer->x + buffer->width);
    }
}

// TODO(Luke): This, like all file IO, needs to handle errors at some point buddy boy
void copy_file(String *src_file, String *dst_file)
{
    string_cstring(src_file, global_path, global_path_size);
    struct stat statbuf;
    stat(global_path, &statbuf);
    size_t length = statbuf.st_size;

    int fd_in = open(global_path, O_RDONLY);

    string_cstring(dst_file, global_path, global_path_size);

    // O_CREAT|O_EXCL ensure a new file is created and S_IRUSR + S_IWUSR sets read and write permissions for the user
    int fd_out = open(global_path, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR + S_IWUSR);

    copy_file_range(fd_in, NULL, fd_out, NULL, length, 0);
    close(fd_in);
    close(fd_out);
}

void delete_file(String *filename)
{
    string_cstring(filename, global_path, global_path_size);

    if(unlink(global_path) < 0) panic(strerror(errno));
}

void rename_file(String *filename, String *new_filename)
{
    char oldname[256];
    char newname[256];
    string_cstring(filename, oldname, 256);
    string_cstring(new_filename, newname, 256);
    rename(oldname, newname);
}

int main()
{
    tb_init();

    global_terminal_width = tb_width();
    global_terminal_height = tb_height();
    global_mode = NORMAL;
    struct tb_event event = {};
    getcwd(global_path, global_path_size);

    Buffer *buf            = (Buffer*)malloc(sizeof(Buffer));
    buf->current_directory = string_from(global_path);
    buf->buffer            = (Line*)calloc(100, sizeof(Line));
    buf->capacity          = 100;
    buf->x                 = 2;
    buf->y                 = 0;
    buf->width             = global_terminal_width - 10;
    buf->height            = global_terminal_height - 1;

    load_directory(global_path, buf);

    global_state_buffers       = (Buffer**)malloc(sizeof(Buffer*) * MAX_BUFFERS);
    global_state_num_buffers   = 1;
    global_state_active_buffer = 0;
    global_state_buffers[0]    = buf;

    SearchBuffer results = {};
    results.buffer = (Result*)calloc(100, sizeof(Result));
    results.capacity = 100;

    // Name of new file created. Might move this somewhere else some time
    String *new_file_name = NULL;

    OperationQueue *op = queue_new(5);
    Operation operation = {};

    u32 visual_select_range_start;
    u32 visual_select_range_end;
    b32 new_visual = true;

    background(TB_BLACK);
    update_screen(buf);
    Buffer *screen = global_state_buffers[0];
    b32 running = true;
    while(running)
    {
        tb_poll_event(&event);
        if(event.type == TB_EVENT_RESIZE)
        {
            //screen->width = event.w - 10;
            //screen->height = event.h - 1;
            //screen->view_range_start = 0;
            //screen->view_range_end = screen->height - 1;
            //tb_init();
            //background(TB_BLACK);
            //update_screen(screen);
            
            global_state_buffers[0]->width = event.w - 10;
            global_state_buffers[0]->height = event.h - 1;
            global_state_buffers[0]->view_range_start = 0;
            global_state_buffers[0]->view_range_end = global_state_buffers[0]->height - 1;
            global_terminal_width = event.w;
            global_terminal_height = event.h;
            
            // Note(Luke): THIS IS NOT PERMANENT DON'T FORGET TO CHANGE THIS LATER WHEN YOU ALLOW MORE THAN 2 BUFFERS!!!
            if(global_state_buffers[1])
            {
                global_state_buffers[0]->width /= 2;

                global_state_buffers[1]->x = global_state_buffers[0]->x * 2 + global_state_buffers[0]->width;
                global_state_buffers[1]->y = global_state_buffers[0]->y;
                global_state_buffers[1]->width = global_state_buffers[0]->width;
                global_state_buffers[1]->height = global_state_buffers[0]->height;
                global_state_buffers[1]->view_range_start = 0;
                global_state_buffers[1]->view_range_end = global_state_buffers[1]->height;
            }
            tb_shutdown();
            tb_init();
            background(TB_BLACK);
            update_screen(global_state_buffers[0]);
            update_screen(global_state_buffers[1]);
            
            continue;
        }

        switch(global_mode)
        {
            case NORMAL:
            {
                if((u8)event.ch == 'j')
                {
                    screen->current_line = (screen->current_line + 1) % screen->num_lines;
                    if(screen->current_line >= screen->view_range_end) scroll(screen, 1);
                    if(screen->current_line == 0)
                    {
                        clear_normal_buffer_area(screen);
                        jump_to_line(screen, 0);
                    }
                }
                else if((u8)event.ch == 'k')
                {
                    if(screen->current_line == 0)
                    {
                        if(screen->num_lines > screen->view_range_end)
                        {
                            clear_normal_buffer_area(screen);
                        }
                        jump_to_line(screen, screen->num_lines - 1);
                    }
                    else
                    {
                        screen->current_line--;
                    }
                    if(screen->current_line < screen->view_range_start) scroll(screen, -1);
                }
                else if((u8)event.ch == 'h')
                {
                    pop_directory(screen->current_directory);
                    string_cstring(screen->current_directory, global_path, global_path_size);
                    load_directory(global_path, screen);
                }
                else if((u8)event.ch == 'l' || event.key == TB_KEY_ENTER)
                {
                    if(screen->buffer[screen->current_line].is_dir)
                    {
                        push_directory(screen->current_directory, screen->buffer[screen->current_line].text);
                        string_cstring(screen->current_directory, global_path, global_path_size);
                        load_directory(global_path, screen);
                    }
                }
                else if(event.key == TB_KEY_CTRL_D)
                {
                    jump_to_line(screen, screen->files_start);
                }
                else if(event.key == TB_KEY_CTRL_U)
                {
                    jump_to_line(screen, 0);
                }
                else if((u8)event.ch == 'D')
                {
                    push_directory(screen->current_directory, screen->buffer[screen->current_line].text);
                    delete_file(screen->current_directory);
                    pop_directory(screen->current_directory);
                    string_cstring(screen->current_directory, global_path, global_path_size);
                    load_directory(global_path, screen);
                    update_screen(screen);
                }
                else if((u8)event.ch == 'd')
                {
                    operation.type = MOVE;
                    operation.name = string_copy(screen->buffer[screen->current_line].text);
                    operation.in_path = string_copy(screen->current_directory);
                    operation.is_dir = screen->buffer[screen->current_line].is_dir;
                    enqueue(op, operation);
                }
                else if((u8)event.ch == 'y')
                {
                    operation.type = COPY;
                    operation.name = string_copy(screen->buffer[screen->current_line].text);
                    operation.in_path = string_copy(screen->current_directory);
                    operation.is_dir = screen->buffer[screen->current_line].is_dir;
                    enqueue(op, operation);
                }
                else if((u8)event.ch == 'p')
                {
                    if(op->size > 0)
                    {
                        operation = dequeue(op);
                        if(operation.type == COPY)
                        {
                            operation.out_path = string_copy(screen->current_directory);
                            push_directory(operation.in_path, operation.name);
                            push_directory(operation.out_path, operation.name);

                            copy_file(operation.in_path, operation.out_path);

                            // Reload directory to see the results of the copy
                            string_cstring(screen->current_directory, global_path, global_path_size);
                            load_directory(global_path, screen);

                            string_free(operation.name);
                            string_free(operation.in_path);
                            string_free(operation.out_path);
                        }
                        else if(operation.type == MOVE)
                        {
                            operation.out_path = string_copy(screen->current_directory);
                            push_directory(operation.in_path, operation.name);
                            push_directory(operation.out_path, operation.name);
                            copy_file(operation.in_path, operation.out_path);
                            delete_file(operation.in_path);
                            string_cstring(screen->current_directory, global_path, global_path_size);
                            load_directory(global_path, screen);

                            // Reload directory of moved file if it is open in a buffer
                            pop_directory(operation.in_path);
                            if(string_equals(operation.in_path, global_state_buffers[0]->current_directory))
                            {
                                string_cstring(operation.in_path, global_path, global_path_size);
                                load_directory(global_path, global_state_buffers[0]);
                                update_screen(global_state_buffers[0]);
                            }
                            if(string_equals(operation.in_path, global_state_buffers[1]->current_directory))
                            {
                                string_cstring(operation.in_path, global_path, global_path_size);
                                load_directory(global_path, global_state_buffers[1]);
                                update_screen(global_state_buffers[1]);
                            }

                            string_free(operation.name);
                            string_free(operation.in_path);
                            string_free(operation.out_path);
                        }
                    }
                }
                else if((u8)event.ch == 's')
                {
                    global_mode = SEARCH;
                }
                else if((u8)event.ch == 'i')
                {
                    global_mode = INSERT;
                }
                else if((u8)event.ch == 'v')
                {
                    global_mode = VISUAL;
                }
                else if((u8)event.ch == 'V')
                {
                    vertical_split(screen);
                }
                else if((u8)event.ch == 'w')
                {
                    global_state_active_buffer = (global_state_active_buffer + 1) % global_state_num_buffers;
                    screen = global_state_buffers[global_state_active_buffer];
                }
                else if((u8)event.ch == 'q')
                {
                    running = false;
                }
                update_screen(screen);
            } break;

            case SEARCH:
            {
                // 0x21 - 0x7E is the range of valid ascii character codes that can be added to the query
                if((u8)event.ch >= 0x21 && (u8)event.ch <= 0x7E)
                {
                    if(!results.query)
                    {
                        results.query = string_new(20);
                    }
                    string_push(results.query, (u8)event.ch);
                    exec_search(screen, &results, results.query);

                    // NOTE(Luke): Clearing the search buffer before executing the search was causing a bug
                    // that cleared the previous position on the screen instead of the updated one. Keep things
                    // like this in mind in the future with multiple buffers!!!

                    clear_search_buffer_area(&results, 0);
                    draw_search_overlay(screen, &results);
                }
                else if(event.key == TB_KEY_SPACE)
                {
                    clear_search_buffer_area(&results, 0);
                    if(!results.query)
                    {
                        results.query = string_new(20);
                    }
                    string_push(results.query, ' ');
                    exec_search(screen, &results, results.query);
                    draw_search_overlay(screen, &results);
                }
                else if(event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2)
                {
                    if(results.query && results.query->length > 0)
                    {
                        clear_search_buffer_area(&results, results.query->length);
                        string_pop(results.query);
                        exec_search(screen, &results, results.query);
                        draw_search_overlay(screen, &results);
                    }
                }
                else if(event.key == TB_KEY_TAB)
                {
                    results.current_line = (results.current_line + 1) % results.num_lines;
                    if(results.current_line >= results.view_range_end)
                    {
                        // search_scroll calls clear_search_buffer_area()
                        search_scroll(&results);
                    }
                    else if(results.current_line < results.view_range_start)
                    {
                        results.view_range_start = results.current_line;
                        results.view_range_end = results.current_line + results.height;
                        clear_search_buffer_area(&results, 0);
                    }
                    draw_search_overlay(screen, &results);
                }
                else if(event.key == TB_KEY_ENTER)
                {
                    if(results.query && results.query->length > 0)
                    {
                        clear_search_buffer_area(&results, results.query->length);
                        results.query->length = 0;
                    }
                    else
                    {
                        clear_search_buffer_area(&results, 0);
                    }
                    jump_to_line(screen, results.buffer[results.current_line].original_line_number);
                    global_mode = NORMAL;
                    update_screen(screen);
                }
                else if(event.key == TB_KEY_ESC)
                {
                    if(results.query && results.query->length > 0)
                    {
                        clear_search_buffer_area(&results, results.query->length);
                        results.query->length = 0;
                    }
                    else
                    {
                        clear_search_buffer_area(&results, 0);
                    }
                    global_mode = NORMAL;
                    update_screen(screen);
                }
            } break;

            case INSERT:
            {
                if((u8)event.ch >= 0x21 && (u8)event.ch <= 0x7E)
                {
                    if(!new_file_name)
                    {
                        new_file_name = string_new(20);
                    }
                    string_push(new_file_name, (u8)event.ch);
                    draw_text(new_file_name, screen->x, screen->y + screen-> height);
                }
                else if(event.key == TB_KEY_BACKSPACE || event.key == TB_KEY_BACKSPACE2)
                {
                    new_file_name->length--;
                    tb_change_cell(screen->x + new_file_name->length + TEXT_OFF, screen->y + screen-> height, (u32)' ', TB_BLACK, TB_BLACK);
                    tb_present();
                }
                else if(event.key == TB_KEY_SPACE)
                {
                    if(!new_file_name)
                    {
                        new_file_name = string_new(20);
                    }
                    string_push(new_file_name, ' ');
                    draw_text(new_file_name, screen->x, screen->y + screen-> height);
                }
                else if(event.key == TB_KEY_ENTER)
                {
                    if(new_file_name && new_file_name->length > 0)
                    {
                        push_directory(screen->current_directory, new_file_name);
                        string_cstring(screen->current_directory, global_path, global_path_size);
                        pop_directory(screen->current_directory);
                        int new_fd = open(global_path, O_CREAT|O_EXCL, S_IRUSR + S_IWUSR);
                        if(new_fd >= 0)
                        {
                            close(new_fd);
                        }
                        else
                        {
                            //panic(strerror(errno));
                            String *error = string_from(strerror(errno));
                            clear_text(screen->x, screen->y + screen->height, new_file_name->length);
                            draw_text(error, screen->x, screen->y + screen->height);
                            tb_poll_event(&event);
                            clear_text(screen->x, screen->y + screen->height, error->length);
                            string_free(error);
                        }
                        clear_text(screen->x, screen->y + screen-> height, new_file_name->length);
                        new_file_name->length = 0;
                        string_cstring(screen->current_directory, global_path, global_path_size);
                        load_directory(global_path, screen);
                    }
                    global_mode = NORMAL;
                    update_screen(screen);
                }
                else if(event.key == TB_KEY_ESC)
                {
                    if(new_file_name)
                    {
                        clear_text(screen->x, screen->y + screen-> height, new_file_name->length);
                        new_file_name->length = 0;
                    }
                    global_mode = NORMAL;
                }
            } break;

            case VISUAL:
            {
                if(new_visual)
                {
                    visual_select_range_start = screen->current_line;
                    visual_select_range_end = screen->current_line + 1;
                    new_visual = false;
                }
                if((u8)event.ch == 'j')
                {
                    if(!(visual_select_range_start == screen->current_line && visual_select_range_end >= screen->num_lines))
                    {
                        if(visual_select_range_start + 1 == visual_select_range_end)
                        {
                            visual_select_range_end++;
                        }
                        else if(visual_select_range_start == screen->current_line)
                        {
                            visual_select_range_end++;
                        }
                        else
                        {
                            visual_select_range_start++;
                        }
                        clear_normal_buffer_area(screen);

                        if(visual_select_range_end >= screen->view_range_end) scroll(screen, 1);
                        update_visual_screen(screen, visual_select_range_start, visual_select_range_end);
                    }
                }
                else if((u8)event.ch == 'k')
                {
                    if(!(visual_select_range_end == screen->current_line + 1 && visual_select_range_start <= 0))
                    {
                        if(visual_select_range_end == visual_select_range_start + 1)
                        {
                            visual_select_range_start--;
                        }
                        else if(visual_select_range_start == screen->current_line)
                        {
                            visual_select_range_end--;
                        }
                        else
                        {
                            visual_select_range_start--;
                        }
                        clear_normal_buffer_area(screen);

                        if(visual_select_range_start < screen->view_range_start) scroll(screen, -1);
                        update_visual_screen(screen, visual_select_range_start, visual_select_range_end);
                    }
                }
                else if((u8)event.ch == 'y')
                {
                    for(u32 i = visual_select_range_start; i < visual_select_range_end; i++)
                    {
                        operation.type = COPY;
                        operation.name = string_copy(screen->buffer[i].text);
                        operation.in_path = string_copy(screen->current_directory);
                        operation.is_dir = screen->buffer[i].is_dir;
                        enqueue(op, operation);
                    }
                    new_visual = true;
                    global_mode = NORMAL;
                    update_screen(screen);
                }
                else if((u8)event.ch == 'D')
                {
                    for(u32 i = visual_select_range_start; i < visual_select_range_end; i++)
                    {
                        push_directory(screen->current_directory, screen->buffer[i].text);
                        string_cstring(screen->current_directory, global_path, global_path_size);
                        unlink(global_path);
                        pop_directory(screen->current_directory);
                    }
                    string_cstring(screen->current_directory, global_path, global_path_size);
                    load_directory(global_path, screen);
                    new_visual = true;
                    global_mode = NORMAL;
                    update_screen(screen);
                }
                else
                {
                    new_visual = true;
                    global_mode = NORMAL;
                    clear_normal_buffer_area(screen);
                    update_screen(screen);
                }
            } break;
        }
    }
    /*
    for(u32 k = 0; k < global_state_num_buffers; k++)
    {
        screen = global_state_buffers[k];
        for(u32 i = 0; i < 100; i++)
        {
            if(screen->buffer[i].text) string_free(screen->buffer[i].text);
        }
        string_free(screen->current_directory);
        free(screen->buffer);
        free(screen);
    }
    free(global_state_buffers);
    free(results.buffer);
    if(results.query) string_free(results.query);
    if(new_file_name) string_free(new_file_name);
    if(op.name) string_free(op.name);
    if(op.in_path) string_free(op.in_path);
    if(op.out_path) string_free(op.out_path);
    */
    tb_shutdown();
    return 0;
}
