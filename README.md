# shell_c

lowkey just a shell implementation in C. wrote this to understand how process control and syscalls work under the hood.

it's not bash but it gets the job done.

### features
- **basic execution:** runs external programs (`ls`, `cat`, `vim`, etc) by searching your PATH.
- **builtins:** implemented `cd`, `pwd`, `echo`, `type`, and `exit` manually.
- **redirections:** supports `>` (overwrite), `>>` (append), and `2>` (stderr). handled via `dup2`.
- **quoting:** handles single `'` and double `"` quotes so you can handle arguments with spaces.
- **tab autocomplete:** legit works. press tab to autocomplete commands. press twice to list matches.
- **raw mode:** implemented custom input handling for backspace and tab keys.

### how to run
compile it with gcc. no external libs needed.

```bash
gcc main.c -o myshell
./myshell
```

### notes
- the parsing logic is custom, so don't try anything too cursed with nested quotes.
- autocomplete scans the current directory and PATH, so it might lag if your system is bloated.
- if it segfaults, that's a skill issue.