#include "line.cpp"


void cd(string dir)
{
    if (dir == "") {
        if (chdir(getenv("HOME")); == -1)
            write(STDOUT_FILENO, "Error changing directory.\n", 26);
    }
    
    else {
        if (chdir(dir.c_str()) == -1)
            write(STDOUT_FILENO, "Error changing directory.\n", 26);
    }
}

void ls(string dir) {

}

void pwd() {

}

void ff(string filename, string directory)
{
    
}
 
string* split(string str)
{
    string *result = new string[3];
    int s = str.find(" ");
    result[0] = str.substr(0, s);
    
    int first = 0;
    int end = 0;
    
    for (int n=0; n < 3; n++) {
        for (int i=0; i < str.length();i++) {
            if (str[i] != 0x20) {
                first = i;
                break;
            }
        } //get the first start
        for (int i=first; i < str.length();i++) {
            if (str[i] == 0x20) {
                end = i;
                break;
            }
        }
        result[n] = str.substr(first, end);
        str = str.substr(end, str.length());
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
            pwd();
        } else {
			write(STDOUT_FILENO, "Failed to execute ", 18);
			write(STDOUT_FILENO, input.c_str(), input.length());
			write(STDOUT_FILENO, "\n", 1);
		} //default situation
        
	} //keep working until exit
	
	return 0; //return 0;
}
