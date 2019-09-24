#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
//#include "../include/termbox.h"
#include "../include/strings.h"

int main()
{
    //tb_init();
    //struct tb_event event = {};
    struct dirent *dir;
    char path[128];
    size_t size = 128;
    getcwd(path, size);
    String *real_path = string_from(path);
    String *real_path_copy = string_copy(real_path);
    String *files[10];
    int index = 0;
    DIR *cwd = opendir(path);
    while(dir = readdir(cwd))
    {
        if(dir->d_type == DT_REG)
        {
            files[index] = string_from(dir->d_name);
            index++;
        }
    }
    printf("%s\n", real_path_copy->start);
    for(int i = 0; i < index; i++)
    {
        printf("%s, ", files[i]->start);
    }
    printf("\n");
    return 0;
}
