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

using namespace std;

void printPrompt(const char* wd)
{
    string result = "";
    
    if (strlen(wd)>16) {
        result.assign("/.../", 5);
        char temp[strlen(wd)];
        int count1 = strlen(wd)-1;
        int count2 = 0;
        char c = wd[count1];
        
        while(c!='/') {
            temp[count2++] = c;
            c = wd[--count1];
        } //check last /
        
        for (int i=0; i<(count2/2);i++) {
            c = temp[i];
            temp[i] = temp[count2-1-i];
            temp[count2-1-i] = c;
        } //reverse the string
        
        result += temp;
    }
    else //if length < 16
        result.assign(wd);
    
    result += "% "; 
    write(STDOUT_FILENO, result.c_str(), result.length()); //output the prompt
} //output the prompt


bool isBuildIn(char* str)
{
    string command(str);
    if (command == "cd" || command == "ff" || command == "ls" || command == "pwd" || command == "exit")
        return true;
    
    return false;
}

