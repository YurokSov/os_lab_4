// Шараковский Юрий М8О-206Б-19
// Лабораторная работа №4
// Вариант: 21

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>

#define BUFSIZE 256
#define SYSEOF 0
#define FAILURE -1
#define SUCCESS 0
#define TRUE 1
#define FALSE 0
#define NUM_OF_CHILDS 2

char* parent_name[2] = { "/parent0", "/parent1" };
char* child_name[2] = { "/child0", "/child1" };
sem_t* parent_sem[2];
sem_t* child_sem[2];

int out_files[2];
char filename[2][BUFSIZE];
void* mmap_ptrs[2];

int signals[BUFSIZE];

int readString(int fd, char* str, int maxsize) {
    char ch;
    int rbytes = 0;
    int ret;
    if ((ret = read(fd, &ch, 1)) < 1)
        return SYSEOF;

    while ((ch == ' ') || (ch == '\n') || (ch == '\t'))
        if (read(fd, &ch, 1) < 1)
            return SYSEOF;

    int i = 0;
    for (; i < maxsize - 1; i++) {
        str[i] = ch;
        rbytes++;
        if (read(fd, &ch, 1) < 1)
            return rbytes;
        if (ch == '\r')
            continue;
        if (ch == '\n')
            break;
    }
    str[++i] = '\0';
    return rbytes;
}

static inline void reverseString(char* dst, char* src, int size) {
    for (int i = size - 1; i >= 0; --i)
        dst[size - i - 1] = src[i];
    return;
}

void open_sems() {
    for (int i = 0; i < NUM_OF_CHILDS; ++i) {
        parent_sem[i] = sem_open(parent_name[i], O_CREAT, S_IRUSR | S_IWUSR, 1);
        if (parent_sem[i] == SEM_FAILED) {
            perror("Error: ");
        }
        child_sem[i] = sem_open(child_name[i], O_CREAT, S_IRUSR | S_IWUSR, 0);
        if (child_sem[i] == SEM_FAILED) {
            perror("Error: ");
        }
    }
}

void close_sems() {
    for (int i = 0; i < NUM_OF_CHILDS; ++i) {
        if (sem_close(parent_sem[i])) {
            perror("Error: ");
        }
        if (sem_close(child_sem[i])) {
            perror("Error: ");
        }
    }
}

void unlink_sems() {
    for (int i = 0; i < NUM_OF_CHILDS; ++i) {
        if (sem_unlink(parent_name[i]) < 0) {
            perror("Error: ");
        }
        if (sem_unlink(child_name[i]) < 0) {
            perror("Error: ");
        }
    }
}

int parent() {
    char string_buf[BUFSIZE];
    int size;
    int idx = 0;

    while ((size = readString(STDIN_FILENO, string_buf, BUFSIZE)) != SYSEOF) {
        sem_wait(parent_sem[idx]);

        memcpy((char*)(mmap_ptrs[idx]), (char*)(&size), sizeof(size));
        memcpy((char*)((char*)(mmap_ptrs[idx]) + sizeof(size)), string_buf, size);

        sem_post(child_sem[idx]);

        idx ^= 1;
    }

    size = SYSEOF;

    sem_wait(parent_sem[0]);
    memcpy((char*)(mmap_ptrs[0]), (char*)(&size), sizeof(size));
    sem_post(child_sem[0]);

    sem_wait(parent_sem[1]);
    memcpy((char*)(mmap_ptrs[1]), (char*)(&size), sizeof(size));
    sem_post(child_sem[1]);

    return SUCCESS;
}

int child(int id) {
    char buffer[BUFSIZE];
    char reversed[BUFSIZE];
    char newline[] = { '\n' };

    char* input = mmap_ptrs[id];
    int output = out_files[id];

    int size = 0;

    while (TRUE) {
        sem_wait(child_sem[id]);

        memcpy(&size, input, sizeof(size));
        if (size == SYSEOF) {
            sem_post(parent_sem[id]);
            break;
        }
        memcpy(buffer, input + sizeof(size), size);

        reverseString(reversed, buffer, size);

        if (write(output, reversed, size) != size) {
            return FAILURE;
        }
        if (write(output, newline, sizeof(newline)) != sizeof(newline)) {
            return FAILURE;
        }

        sem_post(parent_sem[id]);
    }
    return SUCCESS;
}

int work() {
    readString(STDIN_FILENO, filename[0], BUFSIZE);
    readString(STDIN_FILENO, filename[1], BUFSIZE);

    open_sems();

    int is_parent = FALSE;
    int ret;
    int pid[2];
    for (int i = 0; i < NUM_OF_CHILDS; ++i) {
        pid[i] = fork();
        if (pid[i] < 0) {
            perror("Error: ");
            return FAILURE;
        }
        else if (pid[i] == 0) {
            out_files[i] = open(filename[i], O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
            if (out_files[i] == -1) {
                perror("Error: ");
            }
            else {
                printf("CHILD: %d HELLO!\n", getpid());
                ret = child(i);
                close(out_files[i]);
                printf("CHILD: %d GOODBYE!\n", getpid());
            }
            break;
        }
        else if (i == NUM_OF_CHILDS - 1) {
            is_parent = TRUE;
            printf("PARENT: %d HELLO!\n", getpid());
            ret = parent();
            int status;
            waitpid(pid[0], &status, 0);
            waitpid(pid[1], &status, 0);
            printf("PARENT: %d GOODBYE!\n", getpid());
            break;
        }
    }

    close_sems();
    if (is_parent) {
        unlink_sems();
    }
    return ret;
}

int main() {
    mmap_ptrs[0] = mmap(NULL, BUFSIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    mmap_ptrs[1] = mmap(NULL, BUFSIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (mmap_ptrs[0] == MAP_FAILED) {
        perror("Error: ");
        return FAILURE;
    }
    if (mmap_ptrs[1] == MAP_FAILED) {
        perror("Error: ");
        if (munmap(mmap_ptrs[0], BUFSIZE)) {
            perror("Error: ");
        }
        return FAILURE;
    }

    int ret = work();

    if (munmap(mmap_ptrs[0], 32)) {
        perror("Error: ");
    }
    if (munmap(mmap_ptrs[1], 65536)) {
        perror("Error: ");
    }

    return ret;
}