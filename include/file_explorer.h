#include "types.h"
#include "strings.h"

typedef enum
{
    INSERT,
    SEARCH,
    NORMAL,
    VISUAL,
} Mode;

typedef enum
{
    COPY,
    MOVE,
} OperationType;

typedef struct
{
    OperationType type;
    b32 is_dir;
    
    String *name;
    String *in_path;
    String *out_path;
} Operation;

typedef struct
{
    u32 size;
    u32 capacity;
    u32 start;
    u32 end;
    Operation *data;
} OperationQueue;

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
    String *current_directory;
    
    // x, y coordinates of the top left of the buffer
    // plus width and height of the buffer.
    u32 x;
    u32 y;
    u32 height;
    u32 width;
    
    u32 current_line;
    u32 num_lines;
    u32 capacity;
    u32 view_range_start;
    // All Lines before this index are directories
    u32 files_start;
    // view_range_end is one more than the last line with visible text
    // should always be view_range_start + height - 1 because first row is for the title
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
    
    String *query;
    u32 query_x;
    u32 query_y;
    
    u32 current_line;
    u32 num_lines;
    u32 capacity;
    u32 view_range_start;
    // view_range_end is one more than the last line with visible text
    // should always be view_range_start + height
    u32 view_range_end;
    
    Result *buffer;
} SearchBuffer;

OperationQueue *queue_new(u32 capacity)
{
    OperationQueue *op = (OperationQueue*)malloc(sizeof(OperationQueue));
    op->capacity       = capacity;
    op->size           = 0;
    op->start          = 0;
    op->end            = 0;
    op->data           = (Operation*)calloc(capacity, sizeof(Operation));
    return op;
}

Operation dequeue(OperationQueue *op)
{
    Operation operation = op->data[op->start];
    op->start = (op->start + 1) % op->capacity;
    op->size--;
    return operation;
}

void enqueue(OperationQueue *op, Operation operation)
{
    if(op->size < op->capacity)
    {
        op->data[op->end] = operation;
        op->end = (op->end + 1) % op->capacity;
        op->size++;
    }
    else
    {
        Operation *new_data = (Operation*)calloc(op->capacity * 2, sizeof(Operation));
        for(u32 i = 0; i < op->size; i++)
        {
            u32 index = (i + op->start) % op->capacity;
            new_data[i] = op->data[index];
        }
        new_data[op->size] = operation;
        free(op->data);
        op->capacity *= 2;
        op->size++;
        op->start = 0;
        op->end = op->size;
        op->data = new_data;
    }
}

void reallocate_buffer(Buffer*);
void reallocate_search_buffer(SearchBuffer*);
void panic(const char *error);
void draw_vertical_line(u32, u32, u32);
u64 search_test(String*, String*);
void exec_search(Buffer*, SearchBuffer*, String*);
void background(u16);
void clear_normal_buffer_area(Buffer*);
void clear_search_buffer_area(SearchBuffer*, u32);
void update_screen(Buffer*);
void update_visual_screen(Buffer*, u32, u32);
void update_search_screen(SearchBuffer*);
void draw_search_overlay(Buffer*, SearchBuffer*);
int pop_directory(String*);
void push_directory(String*, String*);
void load_directory(char*, Buffer*);
void init_buffer(Buffer*, u32, u32, u32, u32, String*);
void scroll(Buffer*, i32);
void search_scroll(SearchBuffer*);
void jump_to_line(Buffer*, u32);
void draw_text(String*, u32, u32);
void clear_text(u32, u32, u32);
void vertical_split(Buffer*);
void copy_file(String*, String*);
