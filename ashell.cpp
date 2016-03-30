#include "line.cpp"


void cd(string dir) {
    if (dir == "")
        chdir(getenv("HOME"));
    else
        chdir(dir.c_str());
}

void ls(string dir) {

}

void pwd() {

}

void ff(string filename, string directory) {
    
}
 
vector<string> split(string str, char delimiter) {
    vector<string> internal;
    stringstream ss(str); // Turn the string into a stream.
    string tok;
    
    while(getline(ss, tok, delimiter))
        internal.push_back(tok);
  
    return internal;
}

int main() {
	char *wd = getcwd(NULL, 0); //working directory
    
    //initial
    history *h = new history;
    h->count = 0;
    
    for (int i=0; i<10; i++) {
        h->commands[i] = "";
    }
    ///
    
	while(true) {
        prompt(wd); //output the promt

        string input = mygetline(h);
        
		/*if (input.length()==0)
			continue;

        //vector<string> args = split(input, ' ');
         
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
		} //default situation*/
	} //keep working until exit
	
	return 0; //return 0;
}
