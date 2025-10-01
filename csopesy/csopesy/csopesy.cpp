#include <iostream>
#include <string>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <conio.h> 
#include <algorithm> // for rotate
#include <fstream> // in marquee.cpp
// for text to ascii art
#include <map>
#include <vector>
#include <sstream>

using namespace std;

// shared resources/globals
queue<string> keyboard_queue;
mutex queue_mutex;
//
atomic<bool> running(true);
atomic<bool> keyboard_stop(false);
atomic<bool> marqueeRunning(false);
atomic<int> marqueeSpeed(200);
//
string marqueeText = "CSOPESY!";
// text to ascii
map<char, vector<string>> asciiFont;
int letterHeight = 6; // in letters.txt, each letter is 6 lines

void loadASCIIfont(const string& filename) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Error opening font file: " << filename << endl;
        return;
    }

    string line;
    char currentChar = 'A'; // start mapping at A

    while (currentChar <= 'Z' && infile) {
        vector<string> charLines;

        // Read exactly letterHeight lines for this char
        for (int i = 0; i < letterHeight && getline(infile, line); i++) {
            charLines.push_back(line);
        }

        if (!charLines.empty()) {
            asciiFont[currentChar] = charLines;
        }

        // each letter is separated by an empty line/newline
        getline(infile, line);

        currentChar++;
    }

    int width = asciiFont['A'][0].size();
    asciiFont[' '] = vector<string>(letterHeight, string(width, ' '));
}


string textToAscii(const string& text) {
    vector<string> output(letterHeight, "");
    int letterSpacing = 1; // space between letters

    for (char c : text) {
        char upper = toupper(c);

        if (asciiFont.find(upper) == asciiFont.end()) {
            // fallback: use width of 'A' or just 5 spaces
            int width = asciiFont['A'][0].size();
            for (int i = 0; i < letterHeight; i++) {
                output[i] += string(width + letterSpacing, ' ');
            }
        } else {
            const vector<string>& art = asciiFont[upper];
            for (int i = 0; i < letterHeight; i++) {
                output[i] += art[i] + string(letterSpacing, ' ');
            }
        }
    }

    // join into one big string
    stringstream ss;
    for (string& line : output) {
        ss << line << "\n";
    }
    return ss.str();
}


// ----- KEYBOARD HANDLER -----
void keyboardHandler() {
    string input_buffer = "";
    while (!keyboard_stop) {
        // check for keyboard input
        if (_kbhit()) { 
            char key = _getch();
            if (key == '\r') {
                // ENTER key
                lock_guard<mutex> lock(queue_mutex);
                keyboard_queue.push(input_buffer);
                input_buffer.clear();
                cout << endl;
            } 
            else if (key == '\b') { 
                // BACKSPACE key 
                if (!input_buffer.empty()) {
                    input_buffer.pop_back();
                    cout << "\b \b";
                }
            } 
            else if (key >= ' ' && key <= '~') {
                // printable ASCII
                input_buffer += key;
                cout << key;
            }
        }
        // allow marquee thread to run
        this_thread::sleep_for(chrono::microseconds(500));
    }
}

// ----- MARQUEE LOGIC -----
/* void marqueeHandler() {
    while (running) {
        if (marqueeRunning) {
            string text;
            {
                lock_guard<mutex> lock(queue_mutex);
                text = marqueeText;
            }
            rotate(text.begin(), text.begin() + 1, text.end());
            cout << "\r" << text << flush;
            this_thread::sleep_for(chrono::milliseconds(marqueeSpeed));
        } else {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
} */

void marqueeHandler() {
    int offset = 0;
    while (running) {
        if (marqueeRunning) {
            //string text;
            string asciiText;
            {
                lock_guard<mutex> lock(queue_mutex);
                asciiText = marqueeText;
            }
            // rotate(text.begin(), text.begin() + 1, text.end());
            // print marquee at the top, move everything to the bottom
            
            // Split asciiText into lines
            vector<string> lines;
            string line;
            stringstream ss(asciiText);
            while (getline(ss, line)) {
                lines.push_back(line);
            }

            system("cls"); // clear screen
            int width = 80; // console width
            size_t loopLen = lines.empty() ? 0 : (lines[0].size() + 3); // base wrap size

            for (string& l : lines) {
                // duplicate line so scrolling wraps around seamlessly
                string doubled = l + "   " + l;

                // clamp offset within doubled length
                size_t safeOffset = offset % doubled.size();

                // safe substring
                string view = doubled.substr(safeOffset, width);
                cout << view << "\n";
            }

            cout << "\nCommand> " << flush;

            // Move cursor to line 1, clear, print marquee
            //cout << "\033[1;1H\033[K" << text << flush;

            // Move cursor back down to command line
            //cout << "\n\n\n\n\n\n \033[3;1HCommand> " << flush;

            this_thread::sleep_for(chrono::milliseconds(marqueeSpeed));
            // offset = (offset + 1) % 80;

            // cycle offset across the full doubled line length
            if (!lines.empty()) {
                offset = (offset + 1) % (lines[0].size() + 3);
            }

            if (loopLen > 0) {
                offset = (offset + 1) % loopLen;
            }
        } else {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}


// ----- COMMAND INTERPRETER -----
// from the template we removed the switch cases
void commandInterpreter() {
    while (running) {
        string command;
        {
            lock_guard<mutex> lock(queue_mutex);
            if (!keyboard_queue.empty()) {
                command = keyboard_queue.front();
                keyboard_queue.pop();
            }
        }

        if (command.empty()) {
            this_thread::sleep_for(chrono::milliseconds(10));
            continue;
        }

        if (command == "help") {
            system("cls");
            cout << "\nAvailable commands:\n"
                 << " help            - Show this help menu\n"
                 << " start_marquee   - Start marquee animation\n"
                 << " stop_marquee    - Stop marquee animation\n"
                 << " set_text        - Change marquee text\n"
                 << " set_speed       - Change marquee speed (ms)\n"
                 << " exit            - Quit the emulator\n";
            cout << "\nCommand> " << flush;
        }
        else if (command == "start_marquee") {
            if (!marqueeRunning) {
                marqueeRunning = true;
                cout << "Marquee started.\n";
            } else {
                cout << "Marquee already running.\n";
            }
        }
        else if (command == "stop_marquee") {
            if (marqueeRunning) {
                marqueeRunning = false;
                cout << "Marquee stopped.\n";

                // sleep for a few secs
                this_thread::sleep_for(chrono::milliseconds(10));
                system("cls");
                cout << "\nCommand> " << flush;
            } else {
                cout << "Marquee not running.\n";
                // sleep for a few secs
                this_thread::sleep_for(chrono::milliseconds(10));
                system("cls");
                cout << "\nCommand> " << flush;
            }
            // sleep for a few secs
            this_thread::sleep_for(chrono::milliseconds(10));
            system("cls");
            cout << "\nCommand> " << flush;
        }
        else if (command == "set_text") {
            cout << "Enter new text: ";
            while (true) {
                lock_guard<mutex> lock(queue_mutex);
                if (!keyboard_queue.empty()) {
                    string plainText = keyboard_queue.front();
                    keyboard_queue.pop();
                    marqueeText = textToAscii(plainText);
                    //marqueeText = keyboard_queue.front();
                    //keyboard_queue.pop();
                    break;
                }
            }
            cout << "\nCommand> " << flush;
        }
        else if (command == "set_speed") {
            cout << "Enter new speed (ms): ";
            while (true) {
                lock_guard<mutex> lock(queue_mutex);
                if (!keyboard_queue.empty()) {
                    try {
                        marqueeSpeed = stoi(keyboard_queue.front());
                        keyboard_queue.pop();
                    } catch (...) {
                        cout << "Invalid speed.\n";
                    }
                    break;
                }
            }
            // sleep for a few secs
            this_thread::sleep_for(chrono::milliseconds(10));
            system("cls");
            cout << "\nCommand> " << flush;
        }
        else if (command == "exit") {
            cout << "Exiting program...\n";
            running = false;
            keyboard_stop = true;
            break;
        }
        else {
            cout << "Invalid command. Type 'help' for list.\n";
            cout << "\nCommand> " << flush;
        }
    }
}

int main() {
    loadASCIIfont("letters.txt");
    marqueeText = textToAscii("Hello, World!"); // make sure the font is alr loaded

    cout << "Welcome to CSOPESY! Type 'help' for commands.\n"
        << "Group 5 Developers: \n"
        << "Brillantes, Althea\n"
        << "Clavano, Angelica (Jack)\n"
        << "Narito, Ivan\n"
        << "Version Date: October 1, 2025\n"
        << "----------------------------------------\n"
        << "Command> ";

    // create different threads for each major component
    thread kbThread(keyboardHandler);
    thread marqueeThread(marqueeHandler);
    thread commandThread(commandInterpreter);

    // by using join it will wait for the threads to finish before exiting main
    kbThread.join();
    marqueeThread.join();
    commandThread.join();

    return 0;
}
