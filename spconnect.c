/*
# spconnect

Connects to a serial port from a Windows Terminal/Console.
Copyright 2024 David Atkinson. MIT License. 
Available from https://github.com/david47k/spconnect/

## About

This program connects to a serial port from a Windows Console/Terminal.

It's a basic program, designed to be used directly from Windows Terminal, as an
alternative to e.g. PuTTY. It is tested on (and designed to work on) Windows 10.

## Using the program

### Configuring the serial port

You can specify baud rate etc. by first using the windows built-in `mode`
command. Running 'mode' by itself will give you a list of serial ports.
Common baud rates: 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600,
115200, 230400, 460800, 921600. e.g.:

`mode com1 baud=115200 parity=n data=8 stop=1 to=off xon=off odsr=off octs=off dtr=on rts=on`

or

`mode com1 115200,n,8,1`

### Starting the program

Start this program with the serial port as an argument, along with any options. e.g.:

`spconnect com1 -W 10000`

### Options

```
-L       --local-echo          Enable local echo of characters typed.
-S       --system-codepage     Use system codepage instead of UTF-8.
-R       --replace-cr          Replace input CR (\r) with newline (\n).
-D       --disable-vt          Disable sending and receiving of virtual terminal (VT) codes.
-W 100   --write-timeout 100   Serial port write timeout, in milliseconds. Default 1000.
```

### Quitting

Use `Ctrl-F10` to quit.

### Using a different codepage

The default is to use UTF-8 for console input and output. You can use the system
codepage instead by using the `-S` option. You can check the system codepage 
and change it using the the windows built-in `mode con cp` command. e.g.:

`mode con cp select=1251`

### Disable VT processing (raw mode)

The default is to process VT commands from both the keyboard and the serial 
port. You can disable VT processing (essentially a raw mode) using `-D`.
*/

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <locale.h>
#include <assert.h>
#include <windows.h>
#include <winbase.h>
#include <fileapi.h>
#include <synchapi.h>

//
// Tweakable constants
//
#define BUF_SIZE 4096           // Size of copy buffer, in bytes. May hold utf-8 data.
#define WBUF_SIZE 1024          // Size of wchar buffer, in wchar_t's. Must be 1/4 of BUF_SIZE.
#define RECORD_SIZE 256         // Size of console events buffer, in record items.
#define SLEEP_TIME 1            // Time to sleep between polls, in milliseconds.

//
// Options
//
bool LocalEcho = false;         // -l  Enable local echo of characters typed.
bool SystemCP = false;          // -s  Use system codepage instead of UTF-8.
bool ReplaceCR = false;         // -r  Replace input CR (\r) with newline (\n).
bool DisableVT = false;         // -d  Disable sending and receiving of virtual terminal (VT) codes.
bool DebugInput = false;        //     Debug input by echoing hex for input
DWORD WriteTimeout = 1000;      // -w  Serial port write timeout, in milliseconds.

//
// Function declarations
//
void   ExitWithError(const char * callstr, bool use_gle);
void   StrToLower(char* str, size_t max_len);
HANDLE InitStdin();
HANDLE InitStdout();
void   RestoreConsole();
DWORD  ReadStdin(HANDLE h_stdin, char * buf, DWORD buf_size);
int    main(int argc, char* argv[]);

//
// Handle errors and quit.
//
void ExitWithError(const char * callstr, bool use_gle) {
    if (use_gle) {
        fprintf(stderr, "\n%s failed with error %u.\n", callstr, GetLastError());
    }
    else {
        fprintf(stderr, "\n%s\n", callstr);
    }
    RestoreConsole();
    exit(1);
}

//
// Basic string tolower
//
void StrToLower(char * str, size_t max_len) {
    for(size_t i=0; (i<max_len) && (str[i] != 0); i++) {
        if(str[i] >= 'A' && str[i] <= 'Z') {
            str[i] = str[i] + 32;
        }
    }
}

//
// Globals to store console settings so they can be restored on exit
//
DWORD  STDIN_ORIGINAL_MODE  = 0;
UINT   STDIN_ORIGINAL_CP    = 0;
DWORD  STDOUT_ORIGINAL_MODE = 0;
UINT   STDOUT_ORIGINAL_CP   = 0;

// 
// Initialise stdin. Check that is a supported file type, configure it, disable line-edit mode, etc.
//
HANDLE InitStdin() {
    HANDLE stdin_h = GetStdHandle(STD_INPUT_HANDLE);    
    if (stdin_h == INVALID_HANDLE_VALUE) {
        ExitWithError("GetStdHandle(stdin)", true);
    }
    
    if (GetFileType(stdin_h) != FILE_TYPE_CHAR) {
        ExitWithError("GetFileType(h_stdin)", true);
    }

    if (GetConsoleMode(stdin_h, &STDIN_ORIGINAL_MODE) == 0) {   // Set global, so we can restore the console settings on exit
        ExitWithError("GetConsoleMode(h_stdin)", true);
    }
    DWORD stdin_mode =         
        ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS      // Allow the user to use the mouse to select and edit text
    //  | ENABLE_MOUSE_INPUT                                // Receive mouse input events
        | ENABLE_WINDOW_INPUT                               // Receive messages about changes to the size of the console screen buffer        
    ;
    if (!DisableVT) {
        //stdin_mode |= ENABLE_PROCESSED_INPUT;             // CTRL+C to be handled by the system
        stdin_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;        // User input is converted into VT sequences.
    }
    if (SetConsoleMode(stdin_h, stdin_mode) == 0) {
        ExitWithError("SetConsoleMode(h_stdin)", true);
    }

    if (!SystemCP) {
        STDIN_ORIGINAL_CP = GetConsoleCP();                 // Set global, so we can restore the console settings on exit
        SetConsoleCP(CP_UTF8);                              // Set UTF-8 codepage
    }

    return stdin_h;
}

//
// Initialise stdout. Check file type, parse VT sequences, set codepage etc.
//
HANDLE InitStdout() {
    HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_h == INVALID_HANDLE_VALUE) {
        ExitWithError("GetStdHandle(stdout)", true);
    }

    if (GetFileType(stdout_h) != FILE_TYPE_CHAR) {
        ExitWithError("GetFileType(stdout_h)", true);
    }

    if (GetConsoleMode(stdout_h, &STDOUT_ORIGINAL_MODE) == 0) { // Set global, so we can restore the console settings on exit
        ExitWithError("GetConsoleMode(stdout_h)", true);
    }
    DWORD stdout_mode = ENABLE_WRAP_AT_EOL_OUTPUT;              // Wrap cursor        
    if (!DisableVT) {
        stdout_mode |= ENABLE_PROCESSED_OUTPUT;                 // Process control sequences
        stdout_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;      // Parse VT sequences                
    }
    if (SetConsoleMode(stdout_h, stdout_mode ) == 0) {
        ExitWithError("SetConsoleMode(stdout_h)", true);
    }

    if (!SystemCP) {
        STDOUT_ORIGINAL_CP = GetConsoleOutputCP();              // Set global, so we can restore the console settings on exit
        SetConsoleOutputCP(CP_UTF8);                            // Set UTF-8 codepage
    }

    return stdout_h;
}

//
// Restore stdin and stdout to initial settings (during exit).
//
void RestoreConsole() {
    HANDLE stdin_h = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_h != INVALID_HANDLE_VALUE && STDIN_ORIGINAL_MODE != 0) {
        SetConsoleMode(stdin_h, STDIN_ORIGINAL_MODE);
    }
    if (STDIN_ORIGINAL_CP != 0) {
        SetConsoleCP(STDIN_ORIGINAL_CP);
    }
    HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_h != INVALID_HANDLE_VALUE && STDOUT_ORIGINAL_MODE != 0) {
        SetConsoleMode(stdout_h, STDOUT_ORIGINAL_MODE);
    }
    if (STDOUT_ORIGINAL_CP != 0) {
        SetConsoleOutputCP(STDOUT_ORIGINAL_CP);
    }
}

//
// Initialise serial port. 
//
HANDLE InitPort(char * sp_s) {
    // Open the serial port
    HANDLE port = CreateFileA(sp_s, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING, NULL);
    if (port == INVALID_HANDLE_VALUE) {
        ExitWithError("CreateFileA(sp_s)", true);
    }

    // Set comms timeouts.
    // We request for our reads to return straight away, even if there are no bytes (non-blocking). 
    // Writes will eventually timeout.
    COMMTIMEOUTS cto = { MAXDWORD, 0, 0, 0, WriteTimeout };        
    if (SetCommTimeouts(port, &cto) == 0) {
        ExitWithError("SetCommTimeouts", true);
    }

    return port;    
}

//
// Read stdin and fill the buffer with bytes. 
//
DWORD ReadStdin(HANDLE h_stdin, char * buf_c, DWORD buf_c_size) {    
    INPUT_RECORD ir[RECORD_SIZE];           // Place to store the input records we read
    wchar_t buf_w[WBUF_SIZE];               // Place to store the input characters we read
    assert(RECORD_SIZE < WBUF_SIZE);        // We must have enough room to store all the characters we read
    
    // Find the number of records available
    DWORD records_avail = 0;
    GetNumberOfConsoleInputEvents(h_stdin, &records_avail);    
    
    // If there is no data, return early
    if (records_avail < 1) {                            
        return 0;
    }
    
    // Read no more than RECORD_SIZE records
    records_avail = min(records_avail, RECORD_SIZE);    

    // Read the console events.
    // We must use ReadConsoleInputW instead of ReadConsoleInputA to avoid clobbering unicode input.
    // See: https://github.com/microsoft/terminal/issues/7777
    // Specifically comment: https://github.com/microsoft/terminal/issues/7777#issuecomment-726912745
    DWORD records_read = 0;
    if (ReadConsoleInputW(h_stdin, ir, records_avail, &records_read) == 0) {
        ExitWithError("ReadConsoleInputW", true);
    }
    if (records_read != records_avail) {
        ExitWithError("ReadConsoleInputW failed to read all available records.", false);
    }
    
    // Process the event records and extract only the keyboard data
    DWORD buf_w_idx = 0;
    for (DWORD i = 0; i < records_read; i++) {
        if (ir[i].EventType != KEY_EVENT) continue;         // Only interested in key events
        if (ir[i].Event.KeyEvent.bKeyDown != 1) continue;   // Only interested in keydown events            
        wchar_t c = ir[i].Event.KeyEvent.uChar.UnicodeChar; // Read one wchar

        // Check for Ctrl-F10 (in non-VT mode)
        if (ir[i].Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {    // Check for Ctrl
            // Check for F10
            if (ir[i].Event.KeyEvent.wVirtualKeyCode == VK_F10) {
                RestoreConsole();
                exit(0);
            }
        }

        // Replace \r with \n, if requested
        if (ReplaceCR && c == '\r') {                       
            c = '\n';
        }      

        // Add character to buffer, so long as it's not just a control key being pressed by itself (which would be mapped as a NUL), but allow pasted NULs (!)
        if (c != 0 || ir[i].Event.KeyEvent.dwControlKeyState == 0) {
            buf_w[buf_w_idx++] = c;
        }
    }
    
    // If there is no keyboard data, return early
    if (buf_w_idx == 0) {                                   
        return 0;
    }

    // Convert the wide string buffer (W) to a multi-byte (utf-8) string buffer (A)
    int bytes_stdin = WideCharToMultiByte(CP_UTF8, 0, buf_w, buf_w_idx, buf_c, buf_c_size, NULL, NULL);
    if (bytes_stdin == 0) {
        ExitWithError("WideCharToMultiByte", true);
    }

    // Check for Ctrl-F10 (in VT mode). \x1B [21;5~
    for (int i = 0; i < bytes_stdin - 6; i++) {        
        if (memcmp(buf_c, "\x1b""[21;5~", 7) == 0) {
            RestoreConsole();
            exit(0);
        }
    }

    return bytes_stdin;
}

//
// Main function - program entry point.
//
int main(int argc, char* argv[]) {
    char* sp_s = "";

    // Process arguments
    for(int i=1; i<argc; i++) {
        if (strlen(argv[i]) < 1) {
            // empty string
            continue;
        }

        if(argv[i][0] != '-') {
            // this argument must be a serial port
            sp_s = argv[i];
        } else {
            // match options
            
            // make the argument all lowercase
            StrToLower(argv[i], strlen(argv[i]));

            // for convinience, so we don't have to type as many [i]'s
            char* arg = argv[i];

            // match the string
            if (strcmp(arg, "--local-echo") == 0 || strcmp(arg, "-l") == 0) {
                LocalEcho = true;
            }
            else if (strcmp(arg, "--system-codepage") == 0 || strcmp(arg, "-s") == 0) {
                SystemCP = true;
            }
            else if (strcmp(arg, "--replace-cr") == 0 || strcmp(arg, "-r") == 0) {
                ReplaceCR = true;
            }
            else if (strcmp(arg, "--disable-vt") == 0 || strcmp(arg, "-d") == 0) {
                DisableVT = true;
            }
            else if (strcmp(arg, "--debug-input") == 0) {
                DebugInput = true;
            }
            else if (strcmp(arg, "--write-timeout") == 0 || strcmp(arg, "-w") == 0) {
                // check we have a follow-up number
                if((i+1) >= argc) {
                    fprintf(stderr, "\nNo write timeout specified\n");
                    exit(1);
                }
                i++;
                WriteTimeout = atoi(argv[i]);
            }
            else {
                fprintf(stderr, "\nUnknown option: %s\n", argv[i]);
                exit(1);
            }
        }
    }

    // Check that we have a serial port
    if (sp_s[0] == 0) {
        fprintf(stderr, "\nPlease specify a serial port. e.g. spconnect com1\n");
        exit(1);
    }

    // Initialize stdin and stdout and the serial port
    HANDLE h_stdin  = InitStdin();
    HANDLE h_stdout = InitStdout();
    HANDLE h_port   = InitPort(sp_s);

    // Display a welcome message.
    printf("Connecting to %s. Press Ctrl-F10 to quit.\n");

    // Main loop. Copy the data from stdin to the serial port, and from the serial port to stdout.
    while (1) {        
        // Read stdin
        char buf[BUF_SIZE];
        DWORD bytes_stdin = ReadStdin(h_stdin, buf, BUF_SIZE);   
       
        // If we read anything from stdin, process it
        if (bytes_stdin > 0) {                  
            DWORD bytes_written = 0;

            // Echo read characters back in hex, if requested (--debug-input)          
            if (DebugInput) {
                for (DWORD i = 0; i < bytes_stdin; i++) {
                    printf("[%02X]", buf[i]);
                }
            }
            
            // Echo read characters back to console (local echo)  
            if (LocalEcho) {
                if (WriteConsoleA(h_stdout, buf, bytes_stdin, &bytes_written, NULL) == 0) {
                    ExitWithError("WriteConsoleA(h_stdout) (echo)", true);
                }
            }

            // Write to serial port
            if (WriteFile(h_port, buf, bytes_stdin, &bytes_written, NULL) == 0) {
                ExitWithError("WriteFile(h_port)", true);
            }
            if (bytes_written != bytes_stdin) {
                ExitWithError("Timed out writing to serial port.", false);
            }
        }

        // Read serial port
        DWORD bytes_read = 0;
        if (ReadFile(h_port, &buf, BUF_SIZE, &bytes_read, NULL) == 0) {
            ExitWithError("ReadFile(h_port)", true);
        }
        
        // If we read anything from the serial port, process it
        if (bytes_read > 0) {                             
            // Write read data to stdout
            DWORD bytes_written = 0;
            if (WriteConsoleA(h_stdout, buf, bytes_read, &bytes_written, NULL) == 0) {
                ExitWithError("WriteFile(h_stdout)", true);
            }
            if (bytes_written != bytes_read) {
                fprintf(stderr, "\nWARNING: WriteFile(h_stdout) failed to write all available bytes (req: %u, written: %u).\n", bytes_read, bytes_written);
            }
        }

        // Sleep until we start the loop again
        Sleep(SLEEP_TIME);
    }

    return 0;
}
