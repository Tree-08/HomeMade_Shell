#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAXN 1024
#define PMAXN 2048

struct termios orig_termios;
const char *builtin_cmd[] = {"echo","exit","type","pwd","cd", NULL};
typedef struct {
    char **items;
    int count;
    int capacity;
} StringList;

void free_list(StringList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    free(list);
}

int shell_cd(char **args);
int shell_pwd(char **args);
int shell_echo(char **args);
int shell_type(char **args);
int shell_exit(char **args);

int (*builtin_func[]) (char **) = {
    &shell_echo, &shell_exit, &shell_type, &shell_pwd, &shell_cd
};

int is_builtin(const char *cmd){
    for(int i=0; builtin_cmd[i]!=NULL; i++){
        if(strcmp(cmd, builtin_cmd[i]) == 0) return 1;
    }
    return 0;
}

int shell_cd(char **args){
    char *target_path;
    if(args[1] == NULL || strcmp(args[1], "~")==0){
        target_path = getenv("HOME");
    }
    else target_path = args[1];

    if(chdir(target_path) != 0){
        printf("cd: %s: No such file or directory\n", target_path);
        return 0;
    }
    return 1;
}

int shell_pwd(char **args){
    char working_dir[PMAXN];
    if(getcwd(working_dir, PMAXN) == NULL){
        perror("GETCWD FAILED!");
        exit(EXIT_FAILURE);
    }
    printf("%s\n", working_dir);
    return 1;
}

int shell_echo(char **args){
    for(int i=1; args[i]!=NULL; i++){
        printf("%s ", args[i]);
    }
    printf("\n");
    return 1;
}

int shell_type(char **args){
    if(args[1] == NULL){
        printf("type: not enough arguments\n");
        return 0;
    }

    char *target_cmd = args[1];

    if(is_builtin(target_cmd)){ 
        printf("%s is a shell builtin\n", target_cmd);
        return 1;
    }

    char *path = getenv("PATH");
    char pathcpy[MAXN];
    char full_path[PMAXN];
    bool found = false;
    strcpy(pathcpy, path);
    char *dir = strtok(pathcpy, ":");
    while(dir!=NULL){
        // string concatenation safer
        snprintf(full_path, PMAXN, "%s/%s", dir, target_cmd);
        if(access(full_path, X_OK) == 0){
            printf("%s is %s\n", target_cmd, full_path);
            found = true;
            break;
        }

        dir = strtok(NULL, ":");
    }
    if(!found){
        printf("%s: not found\n", target_cmd);
        return 0;
    }
    return 1;
}

int shell_exit(char **args){
    return -1;
}

int run_external(char **args){
    char *path = getenv("PATH");
    char pathcpy[PMAXN]; strcpy(pathcpy, path);
    char full_path[PMAXN] = {0};
    bool found =0;
    char *dir = strtok(pathcpy, ":");

    while(dir!=NULL){
        // string concatenation safer
        snprintf(full_path, PMAXN, "%s/%s", dir, args[0]);
        if(access(full_path, X_OK) == 0){
            found = true;
            break;
        }

        dir = strtok(NULL, ":");
    }
    if(!found){
        printf("%s: command not found\n", args[0]);
        return 0;
    }

    int status;
    pid_t pid = fork();

    if(pid == 0){
        execv(full_path, args);
        perror("EXECV FAILED!");
        exit(EXIT_FAILURE);
    }

    else if(pid > 0){
        waitpid(pid, &status, 0);
    }

    else perror("FORK FAILED!");

    return 0;
}

int execute_command(char **args){
    if(args[0]==NULL){
        return 1;
    }

    char *out_file = NULL;
    bool is_err = 0;
    int fd_args = O_WRONLY | O_CREAT | O_TRUNC;
    for(int i=0; args[i]!=NULL; i++){
        if(strcmp(args[i], ">") == 0 || strcmp(args[i], "1>") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr, "syntax error: file name expected after '>'\n");
                return 0;
            }
            args[i] = NULL;
            out_file = args[i+1];
            break;
        }
        else if(strcmp(args[i], "2>") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr, "syntax error: file name expected after '>'\n");
                return 0;
            }
            args[i] = NULL;
            out_file = args[i+1];
            is_err = 1;
            break;
        }

        else if(strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr, "syntax error: file name expected after '>>'\n");
                return 0;
            }
            args[i] = NULL;
            out_file = args[i+1];
            fd_args = O_WRONLY | O_CREAT | O_APPEND;
            break;
        }

        else if(strcmp(args[i], "2>>") == 0){
            if(args[i+1] == NULL){
                fprintf(stderr, "syntax error: file name expected after '>>'\n");
                return 0;
            }
            args[i] = NULL;
            out_file = args[i+1];
            fd_args = O_WRONLY | O_CREAT | O_APPEND;
            is_err = 1;
            break;
        }
    }

    int saved_output = -1;

    if(out_file != NULL){
        if(is_err){
            saved_output = dup(STDERR_FILENO);
            int fd = open(out_file, fd_args, 0644);
            if(fd<0){
                perror("open failed");
            }


            if(dup2(fd, STDERR_FILENO) < 0){
                perror("dup2 failed");
                close(fd);
                return 1;
            }
        }

        else{
            saved_output = dup(STDOUT_FILENO);
            int fd = open(out_file, fd_args, 0644);
            if(fd<0){
                perror("open failed");
            }


            if(dup2(fd, STDOUT_FILENO) < 0){
                perror("dup2 failed");
                close(fd);
                return 1;
            }
        }
    }


    int rtrn = 0;
    int built_diff = 0;

    for(int i=0; builtin_cmd[i]!=NULL; i++){
        if(strcmp(args[0], builtin_cmd[i]) == 0){
            rtrn = (*builtin_func[i])(args);
            built_diff = 1;
            break;
        }
    }


    if(!built_diff) rtrn = run_external(args);

    if(out_file!=NULL){
        if(is_err) dup2(saved_output, STDERR_FILENO);
        else dup2(saved_output, STDOUT_FILENO);
        close(saved_output);
    }

    return rtrn;
}

void custom_parse(char *input_cmd, char **exec_argv, int *exec_argc){
    enum { NOT_IN, IN_SGL, IN_DBL } state = NOT_IN;

    char *p   = input_cmd;   // read
    char *dst = input_cmd;   // write

    int argc     = 0;
    int in_token = 0;

    while (*p) {
        switch (state) {
        case NOT_IN:
            if (isspace((unsigned char)*p)) {
                if (in_token) {
                    *dst = '\0';
                    dst++;
                    in_token = 0;
                }
                p++;
            } else if (*p == '\\') {
                p++;
                if (*p) {
                    if (!in_token) {
                        exec_argv[argc++] = dst;
                        in_token = 1;
                    }
                    *dst++ = *p++;
                }
            } else if (*p == '\'') {
                if (!in_token) {
                    exec_argv[argc++] = dst;
                    in_token = 1;
                }
                state = IN_SGL;
                p++;
            } else if (*p == '"') {
                if (!in_token) {
                    exec_argv[argc++] = dst;
                    in_token = 1;
                }
                state = IN_DBL;
                p++;
            } else {
                if (!in_token) {
                    exec_argv[argc++] = dst;
                    in_token = 1;
                }
                *dst++ = *p++;
            }
            break;

        case IN_SGL:
            if (*p == '\'') {
                state = NOT_IN;
                p++;
            } else {
                *dst++ = *p++;
            }
            break;

        case IN_DBL:
            if (*p == '"') {
                state = NOT_IN;
                p++;
            } else if (*p == '\\') {
                char next = p[1];
                if (next == '"' || next == '\\' || next == '$' || next == '`') {
                    *dst++ = next;
                    p += 2;
                } else if (next == '\n') {
                    p += 2;
                } else if (next != '\0') {
                    *dst++ = '\\';
                    *dst++ = next;
                    p += 2;
                } else {
                    *dst++ = *p++;
                }
            } else {
                *dst++ = *p++;
            }
            break;
        }
    }

    if (in_token) {
        *dst = '\0';
    }

    exec_argv[argc] = NULL;
    *exec_argc = argc;
}

void disableRawMode(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(){
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

StringList *findExecs(const char *partial_cmd) {
    StringList *results = malloc(sizeof(StringList));
    results->count = 0;
    results->capacity = 10;
    results->items = malloc(sizeof(char *) * results->capacity);

    int partial_len = strlen(partial_cmd);
    char *path_env = getenv("PATH");
    
    char *path_cpy = path_env ? strdup(path_env) : NULL;
    
    char *dir = path_cpy ? strtok(path_cpy, ":") : NULL;
    while (dir != NULL || path_cpy != NULL) {
        char *current_search_dir = dir ? dir : "."; 
        if (dir == NULL) path_cpy = NULL; 

        DIR *d = opendir(current_search_dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (strncmp(entry->d_name, partial_cmd, partial_len) == 0) {
                    struct stat st;
                    char full_path[MAXN];
                    
                    if (strcmp(current_search_dir, ".") == 0) {
                        snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s", current_search_dir, entry->d_name);
                    }

                    if (stat(full_path, &st) == 0 && !S_ISDIR(st.st_mode) && (st.st_mode & S_IXUSR)) {
                        if (results->count == results->capacity) {
                            results->capacity *= 2;
                            results->items = realloc(results->items, sizeof(char *) * results->capacity);
                        }
                        results->items[results->count++] = strdup(entry->d_name);
                    }
                }
            }
            closedir(d);
        }
        if (dir) dir = strtok(NULL, ":");
    }
    if (path_env) free(path_cpy);
                                  

    qsort(results->items, results->count, sizeof(char *), compare_strings);

    int unique_count = 0;
    for (int i = 0; i < results->count; i++) {
        if (i == 0 || strcmp(results->items[i], results->items[i-1]) != 0) {
            results->items[unique_count++] = results->items[i];
        } else {
            free(results->items[i]);
        }
    }
    results->count = unique_count;

    return results;
}

void read_input_raw(char *buffer) {
    int len = 0;
    char c;
    int tab_presses = 0;

    memset(buffer, 0, MAXN);

    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\t') {
            if(len == 3 && strcmp(buffer, "exi") == 0){
                printf("t ");
                strcat(buffer, "t ");
                len+=2;
                fflush(stdout);
            }

            StringList *matches = findExecs(buffer);

            if (matches->count == 0) {
                printf("\x07");
                fflush(stdout);
            } 
            else if (matches->count == 1) {
                char *match = matches->items[0];
                int match_len = strlen(match);
                if (match_len > len) {
                    printf("%s ", match + len);
                    strcat(buffer, match + len);
                    strcat(buffer, " ");
                    len = match_len + 1;
                } else {
                    printf(" ");
                    strcat(buffer, " ");
                    len++;
                }
                fflush(stdout);
                tab_presses = 0;
            } 
            else {
                if (tab_presses == 0) {
                    printf("\x07");
                    fflush(stdout);
                    tab_presses++;
                } else {
                    printf("\n");
                    for (int i = 0; i < matches->count; i++) {
                        printf("%s  ", matches->items[i]);
                    }
                    printf("\n");
                    printf("$ %s", buffer); 
                    fflush(stdout);
                    
                    tab_presses = 0;
                }
            }
            free_list(matches);
        }

        else if (c == '\n') {
            printf("\n");
            buffer[len] = '\0';
            break;
        }
        
        else if (c == 127 || c == 8) {
            if (len > 0) {
                len--;
                buffer[len] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            tab_presses = 0;
        }

        else if (len < MAXN - 1) {
            buffer[len++] = c;
            buffer[len] = '\0';
            printf("%c", c);
            fflush(stdout);
            tab_presses = 0;
        }
    }
}

int main(int argc, char *argv[]) {
  // Flush after every printf
    setbuf(stdout, NULL);
    enableRawMode();

    while(true){
        char input_cmd[MAXN];
        printf("$ ");

        read_input_raw(input_cmd);

        input_cmd[strcspn(input_cmd, "\n")] = '\0';

        if(input_cmd[0]=='\0') continue;

        char cmd_cpy[MAXN];
        strcpy(cmd_cpy, input_cmd);

        char *exec_argv[MAXN+1];
        int exec_argc=0;

        custom_parse(input_cmd, exec_argv, &exec_argc);

        if(execute_command(exec_argv) == -1) break;
    }

    disableRawMode();
    return 0;
}
