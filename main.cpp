#include "ashell.h"

//mygetline
struct history
{
    int count;
    string commands[10];
};

void add(history *h, string command)
{
    
    for (int i = 9; i > 0; i--) {
        h->commands[i] = h->commands[i-1];
    }
    h->commands[0] = command;
    
}

string mygetline(history *h)
{
	string line = "";
	struct termios SavedTermAttributes;
	char RXChar;

	SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);

    int count = -1;
    
	while(read(STDIN_FILENO, &RXChar, 1) != -1) {
		
        if(RXChar == 0x0A || RXChar == 0x04){
            write(STDOUT_FILENO, "\n", 1);
            break;
        }
        else if (RXChar == 27){
			read(STDIN_FILENO, &RXChar, 1);
            read(STDIN_FILENO, &RXChar, 1);
            
			switch (RXChar)
			{
				case 'A': //UP
                    count++;
                    if (h->commands[count].length() == 0) {
                        write(STDOUT_FILENO, "\a", 1);
                        break;
                    }
                    else
                    {
                        while (line.length()>0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            line = line.substr(0,line.length()-1);
                        }
                        
                        line = h->commands[count];
                        write(STDOUT_FILENO, line.c_str(), line.length());
                        
                    }
					break;
				case 'B': //DOWN
                    if (count == -1)
                    {
                        write(STDOUT_FILENO, "\a", 1);
                        break;
                    }
                    if (count == 0)
                    {
                        while (line.length()>0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            line = line.substr(0,line.length()-1);
                        }
                        count--;
                        break;
                    }
                    else
                        count--;
                        
                    while (line.length()>0) {
                        write(STDOUT_FILENO, "\b \b", 3);
                        line = line.substr(0,line.length()-1);
                    }
                        
                    line = h->commands[count];
                    write(STDOUT_FILENO, line.c_str(), line.length());
					break;
				case 'C': //RIGHT
					break;
				case 'D': //LEFT
                    
					break;
				default:
					break;
			}
        }
        else if (RXChar == 0x7F || RXChar == 0x08) {
            
            if (line.length()==0)
                write(STDOUT_FILENO, "\a", 1);
            else{
                line = line.substr(0,line.length()-1);
                write(STDOUT_FILENO, "\b \b", 3);
            }
        }
		else if(isprint(RXChar)) {
			line += RXChar;
			write(STDOUT_FILENO, &RXChar, 1);
		}
	}
    
	ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    
    add(h, line);
    
	return line.c_str();	
}
//




int main() {

    string m = "";

    history *h = new history;
    h->count = 0;
    
    for (int i=0; i<10; i++) {
        h->commands[i] = "";
    }
    
    
    while(1) //get full input
    {
        write(STDOUT_FILENO, "xxx: ", 5);
        m = mygetline(h);
        cout << "      " << m << endl;
    }
	
    
	return 0; //return 0;
}
