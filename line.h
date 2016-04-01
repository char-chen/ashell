#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

using namespace std;

void ResetCanonicalMode(int fd, struct termios *savedattributes) {
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes) {
    struct termios TermAttributes;
    
    // Make sure stdin is a terminal.
    if (!isatty(fd)) {
        fprintf(stderr, "Not a terminal.\n");
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

//mygetline
struct history {
    int count;
    string commands[10];
    
    history() {
        this->count = 0;
        for (int i=0; i<10; i++)
            this->commands[i] = "";
    }
};

void add(history *h, string command) {
    for (int i = 9; i > 0; i--)
        h->commands[i] = h->commands[i-1];
    h->commands[0] = command;
}

string mygetline(history *h)
{
	char RXChar;
    int count = -1;
	string line = "";
	struct termios SavedTermAttributes;

	SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    
	while (read(STDIN_FILENO, &RXChar, 1) != -1) {
        if (RXChar == 0x0A || RXChar == 0x04) {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }
        else if (RXChar == 27) {
			read(STDIN_FILENO, &RXChar, 1);
            read(STDIN_FILENO, &RXChar, 1);
            
			switch (RXChar) {
				case 'A': //UP
                    count++;
                    if (h->commands[count].length() == 0) {
                        write(STDOUT_FILENO, "\a", 1);
                        break;
                    } else {
                        while (line.length()>0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            line = line.substr(0,line.length()-1);
                        }
                        line = h->commands[count];
                        write(STDOUT_FILENO, line.c_str(), line.length());
                    }
					break;
				case 'B': //DOWN
                    if (count == -1) {
                        write(STDOUT_FILENO, "\a", 1);
                        break;
                    }
                    if (count == 0) {
                        while (line.length()>0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            line = line.substr(0,line.length()-1);
                        }
                        count--;
                        break;
                    } else {
                        count--;
                    }
                    while (line.length() > 0) {
                        write(STDOUT_FILENO, "\b \b", 3);
                        line = line.substr(0, line.length() - 1);
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
        } else if (RXChar == 0x7F || RXChar == 0x08) {
            if (line.length() == 0) {
                write(STDOUT_FILENO, "\a", 1);
            } else {
                line = line.substr(0, line.length() - 1);
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if(isprint(RXChar)) {
			line += RXChar;
			write(STDOUT_FILENO, &RXChar, 1);
		}
	}
    
	ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
	add(h, line);
	return line.c_str();	
}

char** getCommand(string str) {
    if (str.length() == 0)
        return NULL;
    
    int total = 1;
    
    for (unsigned int i = 0; i < str.length(); i++)
        if (str[i] == 0x20)
            total++;
    
    string *result = new string[total];
    char** args = new char *[total+1];
    
    for (int n = 0; n < total; n++) {
        int first = 0;
        int end = str.length();
        
        for (unsigned int i = 0; i < str.length(); i++) {
            if (str[i] != 0x20) {
                first = i;
                break;
            }
        } //get the first start
        
        for (unsigned int i = first + 1; i < str.length(); i++) {
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
    
    for (int i = 0; i < total; i++)
        args[i] = &(result[i][0]);
    
    return args;
}
