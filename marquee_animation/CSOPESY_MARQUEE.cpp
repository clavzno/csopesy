// CSOPESY_MARQUEE.cpp 
/*

progress:
- marquee can use start_marquee to animate and stop_marquee to stop
- unfixed bug  of set_text, text is not moving for now

*/

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <fstream>
using namespace std;

string marqueeText;
atomic<bool> running{ false };
mutex mtx;
thread marqueeThread;

string loadFromFile(const string& filename) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Error" << filename << endl;
        return "Default";
    }


    string line, content;
    while (getline(infile, line)) {
        content += line + "\n";
    }

    return content;
}

void runMarquee(int width, int speed) {
    int offset = 0;
    while (running) {
        {
            lock_guard<mutex> lock(mtx);
            system("cls");

            for (const char& c : marqueeText) {
                if (c == '\n') {
                    cout << "\n" << string(offset, ' ');
                }

                else {
                    cout << c;
                }
            }
            cout.flush();
        }
        this_thread::sleep_for(chrono::milliseconds(speed));
        offset = (offset + 1) % width;
    }
}

void startMarquee(int width = 80, int speed = 100) {
    if (!running) {
        running = true;
        marqueeThread = thread(runMarquee, width, speed);
    }
}

void stopMarquee() {
    if (running) {
        running = false;
        if (marqueeThread.joinable()) {
            marqueeThread.join();
        }
        //system("cls");
    }
}

int main()
{

    marqueeText = loadFromFile("art.txt");
    cout << "current commands: set_text <message>, start_marquee, stop_marquee";
    string command;

    while (true) {
        cout << "> ";
        getline(cin, command);


        if (command.rfind("set_text", 0) == 0) {
            string newText = command.substr(9);
            lock_guard<mutex> lock(mtx);
            marqueeText = newText;
            cout << "Text set to: " << newText;

        }
        else if (command == "start_marquee") {
            startMarquee();
        }

        else if (command == "stop_marquee") {
            stopMarquee();
        }

        else if (command == "exit") {
            stopMarquee();
            break; //Not sure if break is the right command
        }
        else {
            cout << "Unknown command.";

        }
    }
}
