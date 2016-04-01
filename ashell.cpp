#include "line.h"
#include "ashell.h"
#include <iostream>

void cd(char *dir) {
    if (dir == NULL) {
        if (chdir(getenv("HOME")) == -1) {
            write(STDOUT_FILENO, "Error changing directory.\n", 26);
        }
    } else {
        if (chdir(dir) == -1) {
            write(STDOUT_FILENO, "Error changing directory.\n", 26);
        }
    }
}

void ls(char* dir) {
    DIR *mydir;
    struct dirent *entry;
    struct stat fileStat;
    
    if (dir) {
        mydir = opendir(dir);
        stat(dir, &fileStat);
    } else {
        mydir = opendir(".");
        stat(".", &fileStat);
    }
    
    if (mydir) {
        while ((entry = readdir(mydir))) {
            write(STDOUT_FILENO, (S_ISDIR(fileStat.st_mode)) ? "d" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IRUSR) ? "r" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IWUSR) ? "w" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IXUSR) ? "x" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IRGRP) ? "r" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IWGRP) ? "w" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IXGRP) ? "x" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IROTH) ? "r" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IWOTH) ? "w" : "-", 1);
            write(STDOUT_FILENO, (fileStat.st_mode & S_IXOTH) ? "x" : "-", 1);
            write(STDOUT_FILENO, " ", 1);
            write(STDOUT_FILENO, entry->d_name, strlen(entry->d_name));
            write(STDOUT_FILENO, "\n", 1);
        }
        closedir(mydir);
    } else {
        string temp(dir);
        write(STDOUT_FILENO, ("Failed to open directory \"" + temp).c_str(), 25 + temp.length());
        write(STDOUT_FILENO, "\"\n", 2);
    }
}

void ff(char* filename, char* directory) {
    if (filename) {
        DIR *dir;
        struct dirent *entry;
        struct stat fileStat;
        if (directory) {
            dir = opendir(directory);
            stat(directory, &fileStat);
        } else {
            dir = opendir(".");
            stat(".", &fileStat);
        }
        
        if (dir) {
            while ((entry = readdir(dir))) {
                if (strcmp(filename, entry->d_name) == 0) {
                    write(STDOUT_FILENO, entry->d_name, strlen(entry->d_name));
                    write(STDOUT_FILENO, "\n", 1);
                }
                if (S_ISDIR(fileStat.st_mode)) {
                    ff(filename, entry->d_name);
                }
            }
            closedir (dir);
        } else {
            perror ("");
        }
    } else {
        write(STDOUT_FILENO, "ff command requires a filename!\n", 35);
    }
}

void execBuildIn(char* str, char **args) {
    string command(str);
    
    if (command == "exit") {
        exit(0);
    } else if (command == "cd") {
        cd(args[1]);
    } else if (command == "ls") {
        ls(args[1]);
    } else if (command == "ff") {
        ff(args[1], args[2]);
    } else if (command == "pwd") {
        char *wd = getcwd(NULL, 0);
        write(STDOUT_FILENO, wd, strlen(wd));
        write(STDOUT_FILENO, "\n", 1);
    }
}

int main() {
    history *h = new history(); //init history struct
    while(true) {
        int status;
        char *wd = getcwd(NULL, 0); //working directory
        printPrompt(wd); //output the promt
        string input = mygetline(h); //read and store commands
        
        if (input == "")
            continue;
        
        char **args = getCommand(input);
        
        if (isBuildIn(args[0])) {
            execBuildIn(args[0], args);
        } else {
            status = fork();
            
            if (status != 0) {
                //Parent code
                waitpid(-1, &status, 0);
            } else {
                //Child code
                if (execvp(args[0], args) == -1) {
                    write(STDOUT_FILENO, "Failed to execute ", 18);
                    write(STDOUT_FILENO, args[0], strlen(args[0]));
                    write(STDOUT_FILENO, "\n", 1);
                }
            }
        }
        delete[](args);
    }
    
    return 0;
}
