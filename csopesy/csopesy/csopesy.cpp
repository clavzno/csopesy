#include <iostream>
#include <string>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <conio.h> 

std::queue<std::string> keyboard_queue;
std::mutex queue_mutex;

std::atomic<bool> running(true);
std::atomic<bool> keyboard_stop(false);
std::atomic<bool> marqueeRunning(false);
std::atomic<int> marqueeSpeed(200);

std::string marqueeText = "Hello, World!   ";

// keyboard handler
void keyboardHandler() {
    std::string input_buffer = "";

    while (!keyboard_stop) {
        if (_kbhit()) { // check for keyboard input
            char key = _getch();

            if (key == '\r') {  // ENTER key
                std::lock_guard<std::mutex> lock(queue_mutex);
                keyboard_queue.push(input_buffer);
                input_buffer.clear();
                std::cout << std::endl;
            }
            else if (key == '\b') { // BACKSPACE
                if (!input_buffer.empty()) {
                    input_buffer.pop_back();
                    std::cout << "\b \b";
                }
            }
            else if (key >= ' ' && key <= '~') { // Printable ASCII
                input_buffer += key;
                std::cout << key;
            }
        }

        // Allow marquee thread to run
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

// marquee animation (placeholder)
void marquee() {
    std::string text;
    while (running) {
        if (marqueeRunning) {
            text = marqueeText;
            std::rotate(text.begin(), text.begin() + 1, text.end());
            std::cout << "\r" << text << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(marqueeSpeed));
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void commandInterpreter() {
    while (running) {
        std::string command;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (!keyboard_queue.empty()) {
                command = keyboard_queue.front();
                keyboard_queue.pop();
            }
        }

        if (command.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (command == "help") {
            std::cout << "Available commands:\n"
                << " help            - Show this help menu\n"
                << " start_marquee   - Start marquee animation\n"
                << " stop_marquee    - Stop marquee animation\n"
                << " set_text        - Change marquee text\n"
                << " set_speed       - Change marquee speed (ms)\n"
                << " exit            - Quit the emulator\n";
        }
        else if (command == "start_marquee") {
            if (!marqueeRunning) {
                marqueeRunning = true;
                std::cout << "Marquee started.\n";
            }
            else {
                std::cout << "Marquee already running.\n";
            }
        }
        else if (command == "stop_marquee") {
            if (marqueeRunning) {
                marqueeRunning = false;
                std::cout << "Marquee stopped.\n";
            }
            else {
                std::cout << "Marquee not running.\n";
            }
        }
        else if (command == "set_text") {
            std::cout << "Enter new text: ";
            while (true) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (!keyboard_queue.empty()) {
                    marqueeText = keyboard_queue.front();
                    keyboard_queue.pop();
                    break;
                }
            }
        }
        else if (command == "set_speed") {
            std::cout << "Enter new speed (ms): ";
            while (true) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (!keyboard_queue.empty()) {
                    try {
                        marqueeSpeed = std::stoi(keyboard_queue.front());
                        keyboard_queue.pop();
                    }
                    catch (...) {
                        std::cout << "Invalid speed.\n";
                    }
                    break;
                }
            }
        }
        else if (command == "exit") {
            std::cout << "Exiting program...\n";
            running = false;
            keyboard_stop = true;
            break;
        }
        else {
            std::cout << "Invalid command. Type 'help' for list.\n";
        }
    }
}

int main() {
    std::thread kbThread(keyboardHandler);
    std::thread marqueeThread(marquee);
    std::thread commandThread(commandInterpreter);

    kbThread.join();
    marqueeThread.join();
    commandThread.join();

    return 0;
}
