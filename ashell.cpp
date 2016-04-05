#include "line.h"
#include "ashell.h"
#include <vector>

using namespace std;

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

void ls(const char* dir) {
    DIR *mydir = dir ? opendir(dir):opendir(".");
    struct dirent *entry;
    struct stat fileStat;

    if (mydir) {
        while ((entry = readdir(mydir))) {
            if (dir) {
                stat((string(dir) +  "/" + string(entry->d_name)).c_str(), &fileStat);
            } else {
                stat(entry->d_name, &fileStat);
            }
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
        
    } else {
        string temp(dir);
        write(STDOUT_FILENO, ("Failed to open directory " + temp).c_str(), 25 + temp.length());
        write(STDOUT_FILENO, "\n", 1);
    }  

}

void ff(char* filename, const char* directory)  {
    if (filename) {
        struct stat fileStat;
        struct dirent *entry;
        DIR *dir = directory ? opendir(directory) : opendir(".");
        
        if (dir) {
            while ((entry = readdir(dir))) {
                string path = directory ? string(directory) + "/" : "./";
                stat((path + entry->d_name).c_str(), &fileStat);
                 
                //Checks if file matches
                if (strcmp(filename, entry->d_name) == 0) {
                    write(STDOUT_FILENO, path.c_str(), path.length());
                    write(STDOUT_FILENO, entry->d_name, strlen(entry->d_name));
                    write(STDOUT_FILENO, "\n", 1);
                }

                //If the entry is a directory, go inside it
                if (S_ISDIR(fileStat.st_mode)) {
                    if (strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, ".") != 0) {
                        ff(filename, (path + string(entry->d_name)).c_str());
                    }
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


void execute(char **args)
{
	//check any redirection		
	int status;
	if (isBuiltIn(args[0])) {
		execBuildIn(args[0], args);
    } else {
		status = fork();            
		if (status != 0) {
       	//Parent code
			waitpid(status,NULL, 0);
        } else {
                //Child code
        	if (execvp(args[0], args) == -1) {
        		//perror("Execvp Fails: ") ;
            	write(STDOUT_FILENO, "Failed to execute ", 18);
                write(STDOUT_FILENO, args[0], strlen(args[0]));
                write(STDOUT_FILENO, "\n", 1);
                exit(1);
            }
        }
	} 
}

int pipeNum(string input)
{
	int count = 0;
	
	for (int i=0; i < input.length(); i++) {
		if (input[i] == '|')
			count++;
	}	
	return count;
}

/*void onepipe(const string input)
{
	char *temp;
	strcpy(temp, input.c_str());
	
	pid_t b,c;
	int p[2];
	char *temp2 = strsep(&temp,"|");
	int length1 = 0;
	int length2 = 0;
	
	char **cmd1 = temp2 ? getCommand(string(temp2), &length1) : NULL;
	char **cmd2 = temp ? getCommand(string(temp), &length2) : NULL;
	
	if(!cmd2 || !cmd2[0])
		return;
		
	pipe(p);

	if (!(b = fork())) 
	{
		close(p[1]);
		close(0);
		dup2(p[0], 0);
		close(p[0]);
		if (isBuildIn(cmd2[0])) {
			execBuildIn(cmd2[0], cmd2);
			exit(0);
		} else {
			execvp(cmd2[0], cmd2);
			write(STDOUT_FILENO, "Failed to execute ", 18);
            write(STDOUT_FILENO, cmd2[0], strlen(cmd2[0]));
            write(STDOUT_FILENO, "\n", 1);
            exit(1);
		}
	
	
	}	
	close(p[0]);
	
	if (!(c = fork())) 
	{
		close(1);
		dup2(p[1], 1);
		close(p[1]);
		if(!cmd1 || !cmd1[0])
			exit(0);
		if (isBuildIn(cmd1[0])) {
			execBuildIn(cmd1[0], cmd1);
			exit(0);
		} else {
			execvp(cmd1[0], cmd1);
			write(STDOUT_FILENO, "Failed to execute ", 18);
            write(STDOUT_FILENO, cmd1[0], strlen(cmd1[0]));
            write(STDOUT_FILENO, "\n", 1);
            exit(1);
		}
	}
	close(p[1]);
	waitpid(b,NULL,0);
	
	
	
	for (int i = 0; i < length1; i++)
		delete[] cmd1[i];
		
	for (int i = 0; i < length2; i++)
		delete[] cmd2[i];
	
	delete[](cmd1);			
	delete[](cmd2);
	return;
}*///THIS PART inspired me to write the following code

void multipipe(const string input) {
	
	int count = pipeNum(input)+1; //total count
	int p[count][2];
	pid_t pid[count]; //my children
	char *str = new char[input.length()]; //in case of memory overlap
	strcpy(str, input.c_str());
	char ***cmd = new char**[count];
	string *temp = new string[count];
	int length[count];
	memset(length, 0, sizeof(int)*count);
	
	for (int i = 0; i < count; i++) {//get all pipe commands!
		temp[i].assign(strsep(&str, "|"));
		cmd[i] = getCommand(temp[i].c_str(), &length[i]);
	}
	
	for (int i = 0; i < count; i++) {

		pipe(p[i]);
		
		if (!(pid[i] = fork()))
		{	
			if (temp[i].find("<")!= -1 || temp[i].find(">") != -1) {
				for (int j = 0; j < length[i]; j++)
						delete[] cmd[i][j];//free memory space
						
				cmd[i] = redirection(temp[i],&length[i]); //redirection dup2 and get new memory here
				
			}
			if (i==count-1) { //the last son
				int j = 0;
				for (j = 0; j < i-1; j++) { //close all unnessary pipes
					close(p[j][1]);
					close(p[j][0]);
				}
				close(p[j][1]);  //prev
				close(p[i][0]);	 //curr
				dup2(p[j][0], STDIN_FILENO);
			} else if (i==0) {         //the first son
				close(p[i][0]);
				dup2(p[i][1], STDOUT_FILENO);
			} else {
				int j = 0;
				for (j = 0; j < i-1; j++) { //close all unnessary pipes
					close(p[j][1]);
					close(p[j][0]);
				}
				close(p[j][1]);  //prev
				close(p[i][0]);	 //curr
				
				dup2(p[j][0], STDIN_FILENO); //prev dup in
				dup2(p[i][1], STDOUT_FILENO); //curr dup out
			}
			
			if(!cmd[i] || !cmd[i][0])
				exit(0);

		//belongs to command 
			if (isBuiltIn(cmd[i][0])) {
				execBuildIn(cmd[i][0], cmd[i]);
				exit(0);
			} else {
				execvp(cmd[i][0], cmd[i]);
				write(STDOUT_FILENO, "Failed to execute ", 18);
            	write(STDOUT_FILENO, cmd[i][0], strlen(cmd[i][0]));
            	write(STDOUT_FILENO, "\n", 1);
            	exit(1);
			}
		}	
		
	}
	for (int i=0; i < count; i++) {
		close(p[i][0]);
		close(p[i][1]);
	}
	
	for (int i=0; i < count; i++)
		waitpid(pid[i], NULL, 0);
	
	
	//delete memory 
	for (int i = 0; i < count; i++) {
		for (int j = 0; j < length[i]; j++)
			delete[] cmd[i][j];
		delete[] cmd[i];
	}
	delete[] cmd;
				
	delete[](str);
} //recusion way is too hard to handle
//citations for pipe and redirection part
//http://web.cse.ohio-state.edu/~mamrak/CIS762/pipes_lab_notes.html
//http://www.cs.loyola.edu/~jglenn/702/S2005/Examples/dup2.html
//http://stackoverflow.com/questions/12981199/multiple-pipe-implementation-using-system-call-fork-execvp-wait-pipe-i
//ALSO, the book OPERATING SYSTEMS, Design and Implementation, third edition.
 
char** redirection(string input, int *length) {
	char **args;
	string inf, outf;
	int s,e;
	int in, out;
	string cmd;
	
	in = input.find_first_of("<");
	out = input.find_first_of(">");
	
	if (in == -1) {
		cmd = input.substr(0, out);
	} else {
 		if (out == -1)
 			cmd = input.substr(0, in);
 		else
 			cmd = in < out ? input.substr(0, in) : input.substr(0, out);
 	}
	trim(cmd);
	
	in = input.find_last_of("<");
	if (in != -1) {
		s = input.length();
		e = input.length();
		for (int i = in+1; i < input.length(); i++)
			if (input[i] != 32 || input[i] == 0) {
				s = i;
				break;
			}
		for (int i = s; i < input.length(); i++)
			if (input[i] == 0 || input[i] == 32 || input[i] == '>') {
				e = i;
				break;
			}
			
		inf = input.substr(in+1, e-in-1);
		trim(inf);
		
		in = open(inf.c_str(), O_RDONLY, 0);
		
		if (in == -1) {
 			write(1, "File \"", 6);
 			write(1, inf.c_str(), inf.length());
 			write(1, "\" does not exist!\n", 18);
 			exit(1);
 		}
 		dup2(in, 0);
 		close(in);
 		in = 0;
	}
 	
 	out = input.find_last_of(">");
 	if (out != -1) {
 		s = input.length();
		e = input.length();
		for (int i = out+1; i < input.length(); i++)
			if (input[i] != 32 || input[i] == 0) {
				s = i;
				break;
			}
		for (int i = s; i < input.length(); i++)
			if (input[i] == 0 || input[i] == 32 || input[i] == '<') {
				e = i;
				break;
			}
			
		outf = input.substr(out+1, e-in-1);
		trim(outf);
 		
 		out = creat(outf.c_str(), 0664);
 		dup2(out, STDOUT_FILENO);
 		close(out);
 		out = 0;
 	}
 	
 	args = getCommand(cmd, length);
 	return args;
}

int main() {

    history *h = new history(); //init history struct         

    while(true) {
    	char *wd = getcwd(NULL, 0); //working directory
        printPrompt(wd); //output the promt
        string input = mygetline(h);
		//fflush(stdin);
		
        if (input == "")
            continue;
        
		if (pipeNum(input) == 0) {
			int length = 0;
			
			char **args;
			string a,b;
				
			if (input.find("<")!= -1 || input.find(">") != -1) {
				int pid;
				if ((pid = fork()) == 0) {
					args = redirection(input, &length);
					execute(args);
					
					for (int i = 0; i < length; i++)
						delete[] args[i];
					delete[] args;	
					exit(0);
				} else {
					waitpid(pid, NULL, 0);
				}
			} else {
				args = getCommand(input, &length);
				execute(args);
				
				for (int i = 0; i < length; i++)
					delete[] args[i];
				delete[] args;	
			}							
		} else {
			multipipe(input);
		}
      		
        //delete[](args1);
        //delete[](args2);
        delete(wd);
    }
    
    return 0;
}
