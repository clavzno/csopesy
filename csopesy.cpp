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
//proposed functions

enum ProcessState { READY, RUNNING, SLEEPING, FINISHED};

struct Process {
    int pid;
    string name;
    ProcessState state;
    int totalInstructions;
    int executedInstructions;
    int sleepTicks;

    Process(int id, const string& n, int instrCount):
        pid(id), name(n), state(READY), totalInstructions(instrCount),
        executedInstructions(0), sleepTicks(0) {}
};

struct CPUCore {
    int id;
    shared_ptr<Process> currentProcess;
    int quantumRemaining; //for round robin

    int activeTicks;
    int totalTicks;

    CPUCore(int coreId) : id(coreId), currentProcess(nullptr), quantumRemaining(0),
                          activeTicks(0), totalTicks(0) {}

    bool isIdle() const {
        return currentProcess == nullptr;
    }
};

enum SchedulerType { FCFS, RR }; //first come first server, Round Robin

class Scheduler {
private:
    SchedulerType type;
    int numCPUs;
    int quantumCycles;
    int tickCounter;

    queue<shared_ptr<Process>> readyQueue;
    vector<CPUCore> cores;
    vector<shared_ptr<Process>> finishedList;

    mutex schedMutex; //for general safety

public:

    Scheduler() : type(FCFS), numCPUs(1), quantumCycles(0), tickCounter(0) {}

    void initialize(SchedulerType t, int cpus, int quantum) {
        type = t;
        numCPUs = cpus;
        quantumCycles = quantum;
        tickCounter = 0;
        cores.clear();

        for (int i = 0; i < numCPUs; i++) {
            cores.emplace_back(i);
        }
    }

    void addProcess(shared_ptr<Process> p) {
        lock_guard<mutex> lock(schedMutex);
        readyQueue.push(p);
    }

    void tick() {
        lock_guard<mutex> lock(schedMutex);
        tickCounter++;


        for (auto& core : cores) {
            core.totalTicks++;
            if (core.isIdle() && !readyQueue.empty()) {
                core.currentProcess = readyQueue.front();
                readyQueue.pop();
                core.currentProcess->state = RUNNING;
                core.quantumRemaining = quantumCycles;
            }
        }

        for (auto& core : cores) {
            if (!core.isIdle()) {
                core.activeTicks++;
                auto& p = core.currentProcess;

                if (p->sleepTicks > 0) {
                    p->sleepTicks--;
                }
                else {
                    p->executedInstructions++;

                    if (p->executedInstructions >= p->totalInstructions) {
                        p->state = FINISHED;
                        finishedList.push_back(p);
                        core.currentProcess = nullptr;
                    }
                    else {
                        if (type == RR) {
                            core.quantumRemaining--;
                            if (core.quantumRemaining <= 0) {
                                p->state = READY;
                                readyQueue.push(p);
                                core.currentProcess = nullptr;
                            }
                        }
                    }
                }
            }
        }
    }
    void printUtilization() {
        lock_guard<mutex> lock(schedMutex);
        cout << "\nCPU Utilization Report\n";
        for (auto& core : cores) {
            double utilization = 0.0;
            if (core.totalTicks > 0)
                utilization = (double)core.activeTicks / core.totalTicks * 100.0;
            cout << "Core " << core.id << ": " << utilization << "% active \n";
        }
        cout << "----------\n";
    }
    void printStatus() {
        lock_guard<mutex> lock(schedMutex);
        cout << "\n Scheduler Status \n";
        cout << "Ready Queue: " << readyQueue.size() << " process(es)\n";
        cout << "Finished: " << finishedList.size() << "\n";

        for (auto& core : cores) {
            cout << "Core " << core.id << ": ";
            if (core.isIdle()) cout << "Idle\n";
            else cout << "Running " << core.currentProcess->name
                << " (" << core.currentProcess->executedInstructions
                << "/" << core.currentProcess->totalInstructions << ")\n";
        }
        cout << "----------\n"; //fix this part later
    }
    void printReadyQueue() {
        lock_guard<mutex> lock(schedMutex);
        queue<shared_ptr<Process>> temp = readyQueue;
        cout << "Ready Queue Contents:\n";
        while (!temp.empty()) {
            auto p = temp.front(); temp.pop();
            cout << " " << p->name << " (" << p->executedInstructions << "/"
                << p->totalInstructions << ")\n";
        }
    }
};


// shared resources/globals
queue<string> keyboard_queue;
mutex queue_mutex;

atomic<bool> running(true);
atomic<bool> keyboard_stop(false);
atomic<bool> marqueeRunning(false);
atomic<int> marqueeSpeed(200);

string marqueeText = "CSOPESY";
// text to ascii
map<char, vector<string>> asciiFont;
int letterHeight = 6; // in letters.txt, each letter is 6 lines

//new
Scheduler scheduler;
atomic<bool> schedulerRunning(false);

int batchProcessFreq = 5;
int minIns = 5;
int maxIns = 15;
int delayPerExec = 200;


int config_numCPU = 2;
SchedulerType config_schedType = FCFS;
int config_quantumCycles = 3;


atomic<int> globalTick(0);
atomic<int> processCounter(0);
// loads ascii font from letters.txt (do not edit letters.txt)
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

        // read exactly letterHeight lines for this char
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

    // handle ! char at the end
    if (infile) {
        vector<string> charLines;
        for (int i = 0; i < letterHeight && getline(infile, line); i++) {
            charLines.push_back(line);
        }

        if (!charLines.empty()) {
            asciiFont['!'] = charLines;
        }
    }

    // handle the space char " "
    int width = asciiFont['A'][0].size(); // Assume all letters have the same width
    asciiFont[' '] = vector<string>(letterHeight, string(width, ' '));
}




// converts plain text into ASCII art font
string textToAscii(const string& text) {
    vector<string> output(letterHeight, "");
    int letterSpacing = 1; // space between letters

    for (char c : text) {
        char upper = toupper(c);

        if (asciiFont.find(upper) == asciiFont.end()) {
            // FALLBACK: use width of 'A' or just blank spaces
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

    // joine verything into one big string
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

//new

void loadConfig(const string& filename) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Config file not found: " << filename << ". Using defaults. \n";
        return;
    }

    string key, value;
    while (infile >> key) {
        infile >> ws;
        getline(infile, value);

        if (!value.empty() && value.front() == '"') value.erase(0, 1);
        if (!value.empty() && value.back() == '"') value.pop_back();

        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (key == "num-cpu") config_numCPU = stoi(value);
        else if (key == "scheduler-type") {
            if (value == "rr") config_schedType = RR;
            else config_schedType = FCFS;
        }
        else if (key == "quantum-cycles") config_quantumCycles = stoi(value);
        else if (key == "batch-process-freq") batchProcessFreq = stoi(value);
        else if (key == "min-ins") minIns = stoi(value);
        else if (key == "max-ins") maxIns = stoi(value);
        else if (key == "delay-per-exec") delayPerExec = stoi(value);
    }

    cout << "[Config] Loaded successfully from " << filename << ":\n";
    cout << " CPUs=" << config_numCPU
        << " Type=" << (config_schedType == RR ? "RR" : "FCFS")
        << " Quantum=" << config_quantumCycles
        << " BatchProcessFreq=" << batchProcessFreq
        << " Minins=" << minIns
        << " Maxins=" << maxIns
        << " Delay=" << delayPerExec << "ms"
        << endl;

}
// ----- MARQUEE LOGIC -----
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
            
            // Split asciiText into lines
            vector<string> lines;
            string line;
            stringstream ss(asciiText);
            while (getline(ss, line)) {
                lines.push_back(line);
            }

            system("cls"); // clear screen
            int width = 80; // base console width
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

            this_thread::sleep_for(chrono::milliseconds(marqueeSpeed));
            

            // cycle offset across the full doubled line length instead of offset = (offset + 1) % 80;
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
//helper only



//new
void schedulerHandler() {

    while (running) {
        if (schedulerRunning) {
            scheduler.tick();

            globalTick++;
            if (globalTick % batchProcessFreq == 0) {
                int pid = ++processCounter;
                int instrCount = rand() % (maxIns - minIns + 1) + minIns;
                auto p = make_shared<Process>(pid, "autoP" + to_string(pid), instrCount);
                scheduler.addProcess(p);
                cout << "[Scheduler Auto-created process " << p->name
                    << " with " << instrCount << " instructions.\n";

            }
            this_thread::sleep_for(chrono::milliseconds(200));
        }
        else {
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
        // new set of commands
        else if (command == "scheduler-start") {
            if (!schedulerRunning) {
                schedulerRunning = true;
                globalTick = 0;
                processCounter = 0;
                scheduler.initialize(config_schedType, config_numCPU, config_quantumCycles);
                cout << "Scheduler started with 2 CPUs (FCFS).\n";

                for (int i = 0; i < 5; i++) {
                    auto p = make_shared<Process>(i, "p" + to_string(i + 1), rand() % 10 + 5);
                    scheduler.addProcess(p);
                }
            }
            else {
                cout << "Scheduler already running";
            }
        }
        else if (command == "scheduler-stop") {
            if (schedulerRunning) {
                schedulerRunning = false;
                cout << "Scheduler stopped.\n";
            }
            else {
                cout << "Scheduler not running...\n";
            }
        }
        else if (command == "report-util") {
            scheduler.printStatus();
        }

        else if (command == "report-cpu") {
            scheduler.printUtilization();
        }
        else if (command == "set_text") {
            std::cout << "Enter new text: ";
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
    loadConfig("config.txt");
    loadASCIIfont("letters.txt");
    marqueeText = textToAscii("CSOPESY"); // make sure the font is alr loaded

    cout  << "Command> ";

    // create different threads for each major component
    thread kbThread(keyboardHandler);
    thread marqueeThread(marqueeHandler);
    thread commandThread(commandInterpreter);
    //new thread
    thread schedulerThread(schedulerHandler);

    // by using join it will wait for the threads to finish before exiting main
    kbThread.join();
    marqueeThread.join();
    commandThread.join();
    schedulerThread.join();

    return 0;
}
