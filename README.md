# HomeMade Shell (C)

A POSIX-style Unix shell implemented in C to gain a deeper understanding of
process control, system calls, and terminal I/O in Unix-based operating systems.

This project focuses on replicating core shell behavior rather than full
bash compatibility.

---

## Features

- **External Command Execution**
  Executes binaries by resolving commands through the `PATH` environment variable
  using `fork`, `execv`, and `waitpid`.

- **Built-in Commands**
  Implements `cd`, `pwd`, `echo`, `type`, and `exit` directly within the shell.

- **I/O Redirection**
  Supports output and error redirection (`>`, `>>`, `2>`) using low-level file
  descriptor manipulation via `open`, `dup`, and `dup2`.

- **Command Parsing**
  Custom lexer and parser handling single and double quotes, escape characters,
  and whitespace-delimited arguments.

- **Tab Autocompletion**
  Provides interactive command autocompletion by scanning executables in the
  current directory and `$PATH`, computing longest common prefixes, and listing
  candidate matches.

- **Raw Terminal Input**
  Uses `termios` to enable non-canonical input mode for real-time character
  processing, backspace handling, and tab-based interaction.

---

## Build & Run

No external dependencies are required.

```bash
gcc main.c -o myshell
./myshell
