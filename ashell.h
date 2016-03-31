#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <dirent.h>
#include <sstream>
#include <vector>
#include <termios.h>
#include <ctype.h>

using namespace std;

void prompt(const char* wd) {
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


void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes){
    struct termios TermAttributes;
    
    // Make sure stdin is a terminal. 
    if(!isatty(fd)){
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }
    
    // Save the terminal attributes so we can restore them later. 
    tcgetattr(fd, savedattributes);
    
    // Set the funny terminal modes. 
    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO); // Clear ICANON and ECHO. 
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}


