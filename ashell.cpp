#include "line.cpp"

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
    DIR *mydir;
    mydir = opendir(dir.c_str());
    struct dirent *myfile;
    struct stat mystat;

    char buf[512];
    while((myfile = readdir(mydir)) != NULL)
    {
        sprintf(buf, "%s/%s", dir.c_str(), myfile->d_name);
        stat(buf, &mystat);
        printf("%zu",mystat.st_size);
        printf(" %s\n", myfile->d_name);
    }
    closedir(mydir);
}

void ff(string filename, string directory) {
    
}
 
string* split(string str) {
    string *result = new string[3];
    
    for (int n=0; n < 3; n++) {
        
        int first = 0;
        int end = str.length();
        
        for (int i=0; i < str.length(); i++) {
            if (str[i] != 0x20) {
                first = i;
                break;
            }
        } //get the first start
        
        for (int i=first+1; i < str.length(); i++) {
            if (str[i] == 0x20) {
                end = i;
                break;
            }
        }
        result[n] = str.substr(first, end - first);
        str = str.substr(end, str.length() - end);
        
        if (str.length()==0)
            break;
    }

    return result;
}

int main() {
	char *wd = getcwd(NULL, 0); //working directory
    
    //initial history system
    history *h = new history;
    h->count = 0;
    
    for (int i=0; i<10; i++) {
        h->commands[i] = "";
    }
    ///
    
	while(true) {
        prompt(wd); //output the promt

        string input = mygetline(h);
        string *args = split(input);
        
		if (args[0].length()==0)
			continue;
        
		if (args[0] == "exit") {
			exit(0);
        } else if (args[0] == "cd") {
            cd(args[1]);
            wd = getcwd(NULL, 0);
        } else if (args[0] == "ls") {
            ls(args[1]);
        } else if (args[0] == "ff") {
            ff(args[1], args[2]);
        } else if (args[0] == "pwd") {
            write(STDOUT_FILENO, wd, strlen(wd));
			write(STDOUT_FILENO, "\n", 1);
        } else {
			write(STDOUT_FILENO, "Failed to execute ", 18);
			write(STDOUT_FILENO, args[0].c_str(), args[0].length());
			write(STDOUT_FILENO, "\n", 1);
		} //default situation
	} //keep working until exit
	
	return 0; //return 0;
}
