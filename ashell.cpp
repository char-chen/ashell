#include <unistd.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>


char* printPath(const char* wd)
{
	
	char *result = (char*)malloc(sizeof(wd));
	
	if (strlen(wd)>16)
	{
		strncpy(result, "/.../", 5);
		char temp[sizeof(wd)];
		int count1 = strlen(wd)-1;
		int count2 = 0;
		char c = wd[count1];
		
		while(c!='/')
		{
			temp[count2++] = c;
			c = wd[--count1];
		}
		
		for (int i=0; i<(count2/2);i++)
		{
			c = temp[i];
			temp[i] = temp[count2-1-i];
			temp[count2-1-i] = c;
		}
		strcat(result, temp);
	}
	else
		strcpy(result, wd);
		
	strcat(result, "% ");
	
	return result;
}

int main()
{
	char *wd; //current working directory
	wd = getcwd(NULL,0);
	char* prompt = printPath(wd);
	
	while(1)
	{			
		char readin[10];
		
		write(STDOUT_FILENO, prompt, (int)strlen(prompt));
		
		read(STDIN_FILENO, readin, strlen(prompt));
	}
	
	return 0;
}

