#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using namespace std;

void prompt(const char* wd)
{
    char *result = new char[strlen(wd)];
    
	if (strlen(wd)>16)
	{
		strncpy(result, "/.../", 5);
		char temp[strlen(wd)];
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
    
	write(STDOUT_FILENO, result, strlen(result));
    
    delete(result);
}

int main()
{
	char *wd = getcwd(NULL,0); //working directory
	
	while(1)
	{			
        prompt(wd);
        char *input;
        
        while(1) //get full input
        {
            input
            
            
        }
        
        sleep(10000);
	}
	
	return 0;
}

