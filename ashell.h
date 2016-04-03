#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <dirent.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <iostream>

using namespace std;

void printPrompt(const char* wd) {
    char *result = new char[strlen(wd)+2];
    
    if (strlen(wd) > 16) {
        strcpy(result, "/.../");
        char *temp = new char[strlen(wd)];
        int count1 = strlen(wd) - 1;
        int count2 = 0;
        char c = wd[count1];
        
        while (c != '/') {
            temp[count2++] = c;
            c = wd[--count1];
        } //check last /
        temp[count2] = 0;
        
        int len = strlen(temp);
        for (int i = 0; i < len / 2; i++) {
            c = temp[i];
            temp[i] = temp[len - 1 - i];
            temp[len - 1 - i] = c;
        } //reverse the string
        
        strcat(result, temp);
        delete[](temp);
    }
    else //if length < 16
        strcpy(result, wd);
    
    strcat(result, "% ");
    write(STDOUT_FILENO, result, strlen(result)); //output the prompt  
    delete[](result);
} //output the prompt


bool isBuiltIn(char* str) {
    string command(str);
    return command == "cd" || command == "ff" || command == "ls" || command == "pwd" || command == "exit";
}

