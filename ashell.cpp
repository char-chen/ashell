#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>


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

void cd (string dir) {
    DIR *pDir = opendir(dir);
}
 
int main() {
	char *wd = getcwd(NULL,0); //working directory
	
	while(1) {			
        prompt(wd); //output the promt
        string input = "";

        char c = 0;

        while(read(STDIN_FILENO, &c, 1) != -1) {

            if (c == '\n')
                break;
            else
                input += c;
        } //read in input line
		
		if (input.length()==0)
			continue;

		if (input == "exit")
			exit(0);
		else
		{
			write(STDOUT_FILENO, "Failed to execute ", 18);
			write(STDOUT_FILENO, input.c_str(), input.length());
			write(STDOUT_FILENO, "\n", 1);
		}//default situation

	} //keep working until exit
	
	return 0; //return 0;
}
