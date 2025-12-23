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
#include <signal.h>

#define MAXN 1024
#define PMAXN 4096

struct termios orig_termios;
const char *builtin_cmd[] = {"echo","exit","type","pwd","cd", NULL};

typedef struct {
    char **items;
    int count;
    int capacity;
} StringList;

StringList *history = NULL;

// --- StringList Helpers ---

StringList *init_string_list(){
    StringList *list = malloc(sizeof(StringList));
    list->count=0;
    list->capacity=4;
    list->items= malloc(list->capacity*sizeof(char *));
    return list;
}

void free_list(StringList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    free(list);
}

void list_add(StringList *list, char *str) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity * sizeof(char *));
        if (!list->items) {
            perror("realloc failed");
            exit(EXIT_FAILURE);
        }
    }
    list->items[list->count++] = strdup(str);
}

char* get_executable_path(char *command) {
    char *path = getenv("PATH");
    if (!path) return NULL;

    char *pathcpy = strdup(path);
    if (!pathcpy) return NULL;

    char *dir = strtok(pathcpy, ":");
    static char full_path[PMAXN]; 

    while(dir != NULL){
        snprintf(full_path, PMAXN, "%s/%s", dir, command);
        if(access(full_path, X_OK) == 0){
            free(pathcpy);
            return full_path;
        }
        dir = strtok(NULL, ":");
    }
    
    free(pathcpy);
    return NULL;
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

    // [FIX] Update PWD and OLDPWD
    char old_pwd[PMAXN];
    if (getcwd(old_pwd, sizeof(old_pwd)) == NULL) {
        perror("getcwd failed");
    }

    if(chdir(target_path) != 0){
        printf("cd: %s: No such file or directory\n", target_path);
        return 0;
    }

    char new_pwd[PMAXN];
    if (getcwd(new_pwd, sizeof(new_pwd)) != NULL) {
        setenv("OLDPWD", old_pwd, 1);
        setenv("PWD", new_pwd, 1);
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

    char *path = get_executable_path(target_cmd);
    if (path) {
        printf("%s is %s\n", target_cmd, path);
        return 1;
    } else {
        printf("%s: not found\n", target_cmd);
        return 0;
    }
}

int shell_exit(char **args){
    return -1;
}

int run_external(char **args, char *out_file, int fd_flags, bool is_err){
    
    char *full_path = NULL;
    char *cmd_path = get_executable_path(args[0]);

    // If not in path, check if it's a direct path (e.g. ./main)
    if (cmd_path == NULL) {
        if (access(args[0], X_OK) == 0) {
            full_path = args[0];
        } else {
            printf("%s: command not found\n", args[0]);
            return 0;
        }
    } else {
        full_path = cmd_path;
    }

    int status;
    pid_t pid = fork();

    if(pid == 0){
        if(out_file != NULL){
            int fd = open(out_file, fd_flags, 0644);
            if(fd<0){
                perror("Open Failed");
                exit(EXIT_FAILURE);
            }

            if(is_err) {
                if(dup2(fd, STDERR_FILENO) < 0) {
                    perror("dup2 failed");
                    exit(EXIT_FAILURE);
                }
            } else {
                if(dup2(fd, STDOUT_FILENO) < 0) {
                    perror("dup2 failed");
                    exit(EXIT_FAILURE);
                }
            }
            close(fd);
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        
        execv(full_path, args);
        perror("EXECV FAILED!");
        exit(EXIT_FAILURE);
    }

    else if(pid > 0){
        // WUNTRACED allows parent to return if child stops
        waitpid(pid, &status, WUNTRACED);
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
    
    // Parsing for redirection
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

    int rtrn = 0;
    int built_diff = 0;

    for(int i=0; builtin_cmd[i]!=NULL; i++){
        if(strcmp(args[0], builtin_cmd[i]) == 0){
            rtrn = (*builtin_func[i])(args);
            built_diff = 1;
            break;
        }
    }

    if(!built_diff) rtrn = run_external(args, out_file, fd_args, is_err);
    return rtrn;
}


StringList *custom_parse(char *input_cmd){
    StringList *tokens = init_string_list();
    enum { NOT_IN, IN_SGL, IN_DBL } state = NOT_IN;

    char *p    = input_cmd;   // read
    char *dst = input_cmd;   // write
    
    int in_token = 0;
    char *current_token_start = NULL;

    while (*p) {
        switch (state) {
        case NOT_IN:
            if (isspace((unsigned char)*p)) {
                if (in_token) {
                    *dst = '\0';
                    dst++;
                    in_token = 0;
                    list_add(tokens, current_token_start);
                }
                p++;
            } else {
                if (!in_token) {
                    current_token_start = dst;
                    in_token = 1;
                }
                
                if (*p == '\\') {
                    p++;
                    if (*p) *dst++ = *p++;
                } else if (*p == '\'') {
                    state = IN_SGL;
                    p++;
                } else if (*p == '"') {
                    state = IN_DBL;
                    p++;
                } else {
                    *dst++ = *p++;
                }
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

    if (state != NOT_IN) {
        fprintf(stderr, "Error: Unclosed quote detected.\n");
        free_list(tokens);
        return NULL;
    }

    if (in_token) {
        *dst = '\0';
        list_add(tokens, current_token_start);
    }

    if (tokens->count >= tokens->capacity) {
        tokens->capacity++;
        tokens->items = realloc(tokens->items, tokens->capacity * sizeof(char *));
        if(!tokens->items) exit(1);
    }
    tokens->items[tokens->count] = NULL;

    return tokens;
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
    
    if (!path_env) return results;

    char *path_cpy = strdup(path_env);
    if (!path_cpy) return results;
    
    char *dir = strtok(path_cpy, ":");
    while (dir != NULL) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (strncmp(entry->d_name, partial_cmd, partial_len) == 0) {
                    struct stat st;
                    char full_path[PMAXN];
                    
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

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
        dir = strtok(NULL, ":");
    }
    free(path_cpy);
                                   

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

char *get_longest_common_prefix(StringList *list) {
    if (list->count == 0) return strdup("");
    
    char *first = list->items[0];
    char *last = list->items[list->count - 1];
    
    int i = 0;
    while (first[i] && last[i] && first[i] == last[i]) {
        i++;
    }
    
    char *lcp = malloc(i + 1);
    strncpy(lcp, first, i);
    lcp[i] = '\0';
    
    return lcp;
}

void store_history(char *cmd){
    if(strlen(cmd) == 0) return;

    if(history->count >0 && strcmp(history->items[history->count-1], cmd) == 0) return;

    if(strlen(cmd) + history->count >= history->capacity){
        history->capacity = 2*history->capacity;
        history->items = realloc(history->items, history->capacity*sizeof(char *));
    }

    history->items[history->count++]=strdup(cmd);
}

void read_input_raw(char *buffer) {
    int len = 0;
    char c;
    int tab_presses = 0;

    memset(buffer, 0, MAXN);

    int history_idx = history ? history->count:0;
    char curr_chr[MAXN] = {0}; 

    while (read(STDIN_FILENO, &c, 1) == 1) {

        if (c == '\x1b') {
            char seq[2];

            if (read(STDIN_FILENO, &seq[0], 1) == 1 &&
                read(STDIN_FILENO, &seq[1], 1) == 1) {
                
                if (seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A': //UP 
                            if(history && history_idx >0){
                                if(history_idx == history->count){
                                    strcpy(curr_chr, buffer);
                                }
                                history_idx--;

                                printf("\033[2K\r$ ");

                                strcpy(buffer, history->items[history_idx]);
                                len =strlen(buffer);
                                printf("%s", buffer);
                                fflush(stdout);
                            }
                            break;
                        case 'B': //DOWN
                            if(history && history_idx< history->count){
                                history_idx++;

                                printf("\033[2K\r$ ");

                                if (history_idx == history->count) {
                                    strcpy(buffer, curr_chr);
                                } else {
                                    strcpy(buffer, history->items[history_idx]);
                                }
                                len = strlen(buffer);
                                printf("%s", buffer);
                                fflush(stdout);
                            }
                            break;
                        case 'C': //RIGHT
                            break;
                        case 'D': //LEFT
                            break;
                    }
                }
            }
            continue; 
        }

        if (c == '\t') {
            // Autocomplete for exit
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
                char *lcp = get_longest_common_prefix(matches);
                int lcp_len = strlen(lcp);
                if (lcp_len > len) {
                    printf("%s", lcp + len);
                    strcat(buffer, lcp + len);
                    len = lcp_len;
                    fflush(stdout);
                    tab_presses = 0; 
                }

                else{
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
                free(lcp);
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

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    history = init_string_list();

    while(true){
        char input_cmd[MAXN];
        printf("$ ");

        read_input_raw(input_cmd);

        input_cmd[strcspn(input_cmd, "\n")] = '\0';

        if(input_cmd[0]=='\0') continue;

        store_history(input_cmd);
        
        StringList *cmd_list = custom_parse(input_cmd);

        if (!cmd_list) continue;

        if (cmd_list->count > 0) {
            if(execute_command(cmd_list->items) == -1) {
                free_list(cmd_list);
                break;
            }
        }

        free_list(cmd_list);
    }

    disableRawMode();
    free_list(history);
    return 0;
}