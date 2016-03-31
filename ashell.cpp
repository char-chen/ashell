#include "line.h"
#include "ashell.h"

#include <iostream>


void cd(string dir)
{
    if (dir == "") {
        if (chdir(getenv("HOME")) == -1) {
            write(STDOUT_FILENO, "Error changing directory.\n", 26);
        }
    } else {
        if (chdir(dir.c_str()) == -1) {
            write(STDOUT_FILENO, "Error changing directory.\n", 26);
        }
    }
}

void ls(string dir) {
    struct dirent *myfile;
    struct stat fileStat;
    DIR *mydir;
    if (dir != "")
        mydir = opendir(dir.c_str());
    else
        mydir = opendir(".");
    
    if (mydir) {
        string buf;
        while ((myfile = readdir(mydir))) {
            stat(buf.c_str(), &fileStat);
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
            write(STDOUT_FILENO, myfile->d_name, strlen(myfile->d_name));
            write(STDOUT_FILENO, "\n", 1);
        }
        closedir(mydir);
    } else {
        write(STDOUT_FILENO, ("Failed to open directory \"" + dir).c_str(), 25 + dir.length());
        write(STDOUT_FILENO, "\"\n", 2);
    }
}

void ff(string filename, string directory)
{
    
}

void execBuildIn(char* command, char **args)
{
    cout << "try to exec build-in" << endl;
}





int main()
{
    char *wd = getcwd(NULL, 0); //working directory
    history *h = new history(); //init history struct
    
    while(true) {
        int status;
        printPrompt(wd); //output the promt
        
        string input = mygetline(h); //read and store commands
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
                if (execv(args[0], args) == -1) {
                    write(STDOUT_FILENO, "Failed to execute ", 18);
                    write(STDOUT_FILENO, args[0], strlen(args[0]));
                    write(STDOUT_FILENO, "\n", 1);
                }
                
            }
        }
        
        delete[](args);
    }
    
    return 0; //return 0;
}
