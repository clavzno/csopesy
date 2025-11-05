// change to true if doing #4 in the submission
bool doingnumfour = false;
/* default config.txt
num-cpu 4
scheduler "rr"
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delay-per-exec 0
*/

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
#include <memory>
#include <cstdlib>
#include <ctime>
// for printing the utilization report location
#include <direct.h>

using namespace std;
//proposed functions

enum ProcessState { READY, RUNNING, SLEEPING, FINISHED };
// shared resources/globals
queue<string> keyboard_queue;
mutex queue_mutex;

atomic<bool> running(true);
atomic<bool> keyboard_stop(false);
atomic<bool> marqueeRunning(false);
atomic<int> marqueeSpeed(200);

//dummy processes
//bool dummyProcessEnabled = true;

string marqueeText = "CSOPESY";
// text to ascii
map<char, vector<string>> asciiFont;
int letterHeight = 6; // in letters.txt, each letter is 6 lines

//new
//
//atomic<bool> schedulerRunning(false);

int batchProcessFreq = 5;
int minIns = 5;
int maxIns = 15;
int delayPerExec = 200;



atomic<int> globalTick(0);
atomic<int> processCounter(0);

//
atomic<bool> schedulerRunning(false);

struct Process {
    int pid;
    string name;
    ProcessState state;
    int totalInstructions;
    int executedInstructions;
    int sleepTicks;

    // instruction engine-related
    vector<string> instructions; // list of instructions to execute
    int instructionPointer = 0; // current instruction index
    map<string, uint16_t> variables; // variable mem
    vector<string> log; // for PRINT

    Process(int id, const string& n, int instrCount) :
        pid(id),
        name(n), \
        state(READY),
        totalInstructions(instrCount),
        executedInstructions(0),
        sleepTicks(0) {
    }

    // instruction engine-related
    void executeNextInstruction() {

        //for state - uncomment if you want to see them executing in real time
        /*
        if (schedulerRunning == true) {
            cout << "Executing instruction at " << instructionPointer << " / " << instructions.size() << endl;
        }*/

        if (instructionPointer >= instructions.size()) {
            //for state - uncomment if you want to see info
            // cout << "Process " << name << "finished at instructionPointer " << instructionPointer << "\n";
            state = FINISHED;
            return;
        }

        stringstream ss(instructions[instructionPointer]);
        string cmd;
        ss >> cmd;

        if (cmd == "DECLARE") {
            string var; uint16_t val;
            ss >> var >> val;
            variables[var] = val;
        }
        else if (cmd == "ADD") {
            string dest, a, b; ss >> dest >> a >> b;
            uint16_t v1 = variables.count(a) ? variables[a] : stoi(a);
            uint16_t v2 = variables.count(b) ? variables[b] : stoi(b);
            variables[dest] = v1 + v2;
        }
        else if (cmd == "SUBTRACT") {
            string dest, a, b; ss >> dest >> a >> b;
            uint16_t v1 = variables.count(a) ? variables[a] : stoi(a);
            uint16_t v2 = variables.count(b) ? variables[b] : stoi(b);
            variables[dest] = v1 - v2;
        }
        else if (cmd == "SLEEP") {
            int ticks; ss >> ticks;
            sleepTicks = ticks;
        }
        else if (cmd == "PRINT") {
            string msg;
            getline(ss, msg);

            if (doingnumfour) {
                size_t pos = msg.find("x");
                if (pos != string::npos && variables.count("x")) {
                    msg.replace(pos, 1, to_string(variables["x"]));
                }
            }

            log.push_back(msg);
            // cout << "[" << name << "] " << msg << endl; // uncomment if you want hello world to print during scheduler-stop
        }
        else if (cmd == "FOR") {
            // FOR <repeatCount> <instruction something something>
            int numRepeats;
            ss >> numRepeats;
            string rest; // rest of the instruction here
            getline(ss, rest);

            // trim rest
            while (!rest.empty() && isspace(rest.front())) {
                rest.erase(rest.begin());
            }

            while (!rest.empty() && isspace(rest.back())) {
                rest.pop_back();
            }

            if (!rest.empty() && numRepeats > 0) {
                // insert the rest of the instruction for numRepeats after current position
                vector<string> newInstr;
                for (int i = 0; i < numRepeats; ++i)
                    newInstr.push_back(rest);

                // insert into instruction list right after current position
                instructions.insert(instructions.begin() + instructionPointer + 1,
                    newInstr.begin(), newInstr.end());
                //accuracy
                //totalInstructions = instructions.size();
            }
        }

        instructionPointer++;
        executedInstructions++;

        //if (schedulerRunning == true) {
        //    cout << "Executed Instructions: " << executedInstructions << endl;
        //}
    }
};

struct CPUCore {
    int id;
    shared_ptr<Process> currentProcess;
    int quantumRemaining; //for round robin

    int activeTicks;
    int totalTicks;

    CPUCore(int coreId) : id(coreId), currentProcess(nullptr), quantumRemaining(0),
        activeTicks(0), totalTicks(0) {
    }

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
                if (core.isIdle()) continue; // added - avoid accessing a null ptr

                core.activeTicks++;
                auto& p = core.currentProcess;

                if (p->sleepTicks > 0) {
                    p->sleepTicks--;
                }

                if (!p) continue; // just in case 

                else {
                    try {
                        // TODO: this may cause it to abort if the instruction was malformed 
                        // one instruction per tick
                        p->executeNextInstruction(); // was p->executedInstructions

                        /*if (p->executedInstructions >= p->totalInstructions) {
                        p->state = FINISHED;
                        finishedList.push_back(p);
                        core.currentProcess = nullptr;
                        }*/
                    }
                    catch (...) {
                        // if the next instruction was malformed for some reason it will notify the user and mark it as finished
                        std::cerr << "[Unknown error] in process " << p->name << std::endl;
                        p->state = FINISHED;
                        finishedList.push_back(p);
                        core.currentProcess = nullptr;
                        continue;
                    }


                    // changed, instruction engine-relatef
                    // check if process finished after execution
                    if (p->state == FINISHED) {
                        finishedList.push_back(p);
                        core.currentProcess = nullptr;
                        continue;
                    }

                    // had to redo the rr handling
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


    // instructions generator - Process instructions are pre-determined and not typed by the user. E.g., randomized via scheduler-start command.
    vector<string> generateRandomInstructions(int count) {
        vector<string> instr;

        //declare before using 
        instr.push_back("DECLARE x 5");

        vector<string> possible = {
            "ADD x x 1",
            "SUBTRACT x x 1",
            "PRINT Hello world",
            "SLEEP 2"
        };
        for (int i = 1; i < count; ++i)
            instr.push_back(possible[rand() % possible.size()]);
        return instr;
    }

    // for number 4 in demo: PRINT ADD PRINT ADD
    // update should be in schedulerhandler p->instructions = scheduler.generateRandomInstructions(instrCount);
    vector<string> generateTestInstructions(int count) {
        vector<string> instr;

        instr.push_back("DECLARE x 0");

        // alternate bet print and add
        for (int i = 1; i < count; ++i) {
            if (i % 2 == 1) {
                // odd = print
                instr.push_back("PRINT value from: x");
            }
            else {
                // even = add
                int addVal = rand() % 10 + 1;
                instr.push_back("ADD x x " + to_string(addVal));
            }
        }

        return instr;
    }

    // ------------------------------ STATUS PRINTING ------------------------------

    // get actual core usage of actual running process right now, not cumulative
    double getCurrentCPUUtilization() {
        int coresUsed = 0;
        for (auto& core : cores) {
            if (!core.isIdle()) coresUsed++;
        }

        if (cores.empty()) return 0.0;
        if (!schedulerRunning) return 0.0; // scheduler-stop = paused = should be 0% usage
        return (static_cast<double>(coresUsed) / cores.size()) * 100.0;
    }

    // cpu utilization
    void printUtilization() {
        lock_guard<mutex> lock(schedMutex);
        cout << "\nCPU Utilization Report\n";

        /*
        for (auto& core : cores) {
            double utilization = 0.0;
            if (core.totalTicks > 0)
                utilization = (double)core.activeTicks / core.totalTicks * 100.0;
            cout << "Core " << core.id << ": " << utilization << "% active \n";
        }
        cout << "------------------------------\n";*/

        int activeCores = 0;
        for (auto& core : cores) {
            bool active = schedulerRunning && !core.isIdle();
            cout << "Core " << core.id << ": " << (active ? "Active" : "Idle") << "\n";
            if (active) activeCores++;
        }

        double utilization = (cores.empty()) ? 0.0
            : (static_cast<double>(activeCores) / cores.size()) * 100.0;

        if (!schedulerRunning) utilization = 0.0;
        cout << "Current CPU Utilization: " << utilization << "%\n";
    }

    // scheduler, core status
    void printStatus() {
        lock_guard<mutex> lock(schedMutex);
        cout << "\nScheduler Status \n";
        cout << "Ready Queue: " << readyQueue.size() << " process(es)\n";
        cout << "Finished: " << finishedList.size() << "\n";

        // core utilization
        for (auto& core : cores) {
            cout << "Core " << core.id << ": ";
            if (core.isIdle()) cout << "Idle\n";
            else cout << "Running " << core.currentProcess->name
                << " (" << core.currentProcess->executedInstructions
                << "/" << core.currentProcess->totalInstructions << ")\n";
        }
        cout << "------------------------------\n";
    }

    // log cpu and process utilization report - report-util
    string saveUtilizationFile(const string& filename) {
        lock_guard<mutex> lock(schedMutex);
        ofstream logfile(filename, ios::app);

        // failure would return the location still
        if (!logfile.is_open()) {
            char absBuf[_MAX_PATH];
            if (_fullpath(absBuf, filename.c_str(), _MAX_PATH)) {
                return string(absBuf);
            }
            char* cwd = _getcwd(nullptr, 0);
            string attempted = (cwd ? string(cwd) : string(".")) + string("\\") + filename;
            if (cwd) free(cwd);
            return attempted;
        }

        // calculate the core stats (active cores)
        int coresUsed = 0;
        for (auto& core : cores) {
            if (!core.isIdle()) coresUsed++;
        }

        size_t coresAvailable = cores.size() - static_cast<size_t>(coresUsed);
        // int coresAvailable = cores.size() - coresUsed;
        // double totalActive = 0, totalTicks = 0;

        /* commented out to prevent cumulative stats, we want the current ones, avoid using total
        for (auto& core : cores) {
            totalActive += core.activeTicks;
            totalTicks += core.totalTicks;
        } */

        // double cpuUtil = (totalTicks == 0) ? 0 : (totalActive / totalTicks) * 100.0;
        double cpuUtil = (cores.empty()) ? 0.0
            : (static_cast<double>(coresUsed) / cores.size()) * 100.0;
        
        if (!schedulerRunning)
            cpuUtil = 0.0;

        // get current system time for timestamps
        auto now = chrono::system_clock::now();
        time_t time_now = chrono::system_clock::to_time_t(now);

        // for the header time 
        tm timeinfo{};
        localtime_s(&timeinfo, &time_now);
        char headerTime[30];
        strftime(headerTime, sizeof(headerTime), "%m/%d/%Y %I:%M:%S%p", &timeinfo);

        // following the sample image in specs
        logfile << "\n\n\n";
        logfile << "------------------------------" << headerTime << "------------------------------" << "\n";
        logfile << "CPU Utilization: " << cpuUtil << "%\n";
        logfile << "Cores used: " << coresUsed << "\n";
        logfile << "Cores available: " << coresAvailable << "\n\n";
        logfile << "Core Utilization: \n";
        logfile << "------------------------------\n";
        logfile << "Running processes:\n";

        // get the running proceses info
        for (auto& core : cores) {
            if (!core.isIdle()) {
                auto p = core.currentProcess;
                tm timeinfo{};
                localtime_s(&timeinfo, &time_now);
                char timeStr[30];
                strftime(timeStr, sizeof(timeStr), "%m/%d/%Y %I:%M:%S%p", &timeinfo);

                // print to the file
                logfile << p->name << " (" << timeStr << ")"
                    << "\tCore: " << core.id << "\t"
                    << p->executedInstructions << "/" << p->totalInstructions
                    << "\n";
            }
        }

        logfile << "\n Finished processes:\n";
        for (auto& p : finishedList) {
            tm timeinfo{};
            localtime_s(&timeinfo, &time_now);
            char timeStr[30];
            strftime(timeStr, sizeof(timeStr), "%m/%d/%Y %I:%M:%S%p", &timeinfo);
            logfile << p->name << " (" << timeStr << ")"
                << "\tFinished\t"
                << p->executedInstructions << "/" << p->totalInstructions
                << "\n";
        }

        logfile << "------------------------------\n";
        logfile.close();

        // return the absolute path
        const string logName = "csopesy-log.txt";
        char absBuf[_MAX_PATH];
        string absPath;
        if (_fullpath(absBuf, logName.c_str(), _MAX_PATH)) {
            absPath = absBuf;
        }
        else {
            char* cwd = _getcwd(nullptr, 0);
            absPath = (cwd ? string(cwd) : string(".")) + "\\" + logName;
            if (cwd) free(cwd);
        }
        return absPath;
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

    queue<shared_ptr<Process>> getReadyQueue() {
        lock_guard<mutex> lock(schedMutex);
        return readyQueue; // returns a copy
    }

};

Scheduler scheduler;

int config_numCPU = 2;
SchedulerType config_schedType = FCFS;
int config_quantumCycles = 3;


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
        }
        else {
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

// new - configuration/initialization
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
        }
        else {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

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

                if (doingnumfour)
                    p->instructions = scheduler.generateTestInstructions(instrCount); // for #4 demo
                else {
                    p->instructions = scheduler.generateRandomInstructions(instrCount); // instruction engine-related
                }

                scheduler.addProcess(p);

                // uncomment if you want the screen to print info
                /* cout << "[Scheduler Auto-created process " << p->name
                    << " with " << instrCount << " instructions.\n"; */

            }
            // this_thread::sleep_for(chrono::milliseconds(200)); changed so we can use the config value
            this_thread::sleep_for(chrono::milliseconds(delayPerExec));
        }
        else {
            // pause it
            // scheduler.tick();
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}


// ----- COMMAND INTERPRETER -----
// from the template we removed the switch cases
void commandInterpreter() {
    shared_ptr<Process> currentProcess = nullptr; // active process context
    bool inProcessContext = false;
    bool initialized = false; // must be initialized before use

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

        if (!inProcessContext) {
            // ---------- MAIN CONSOLE COMMANDS ----------
            // enforce initialization before other commands
            if (!initialized && command != "initialize" && command != "help" && command != "exit") {
                cout << "[Error] Please run 'initialize' first before any other command.\n";
                cout << "\nCommand> " << flush;
                continue;
            }

            //begin command handling
            if (command == "help") {
                system("cls");
                cout << "\nAvailable commands:\n"
                    << " help                  - Show this help menu\n"
                    << " screen -s [name]      - Creates a new process\n"
                    << " screen -r [name]      - Reopens an existing process\n"
                    << " screen -ls            - List all processes\n"
                    << " scheduler-start       - Start scheduler\n"
                    << " scheduler-stop        - Stop the scheduler\n"
                    << " report-util           - Report scheduler status\n"
                    << " report-cpu            - Report CPU utilization\n"
                    << " start_marquee         - Start marquee animation\n"
                    << " stop_marquee          - Stop marquee animation\n"
                    << " set_text              - Change marquee text\n"
                    << " set_speed             - Change marquee speed (ms)\n"
                    << " exit                  - Quit the emulator\n";
                cout << "\nCommand> " << flush;
            }

            else if (command == "initialize") {
                loadConfig("config.txt");
                scheduler.initialize(config_schedType, config_numCPU, config_quantumCycles);
                initialized = true; // mark as ready
                cout << "[OK] Scheduler initialized. CPUs=" << config_numCPU
                    << " Type=" << (config_schedType == RR ? "RR" : "FCFS")
                    << " Quantum=" << config_quantumCycles << endl;
                cout << "\nCommand> " << flush; // added
            }

            else if (command.rfind("screen -s", 0) == 0) {
                // screen -s <name>
                string name = command.substr(9);
                name.erase(remove_if(name.begin(), name.end(), ::isspace), name.end());
                if (name.empty()) {
                    cout << "Usage: screen -s [name]\n";
                    cout << "\nCommand> " << flush;
                }
                else {
                    int pid = ++processCounter;
                    int instr = rand() % (maxIns - minIns + 1) + minIns;
                    auto p = make_shared<Process>(pid, name, instr);

                    if (doingnumfour)
                        p->instructions = scheduler.generateTestInstructions(instr); // for #4 demo
                    else {
                        p->instructions = scheduler.generateRandomInstructions(instr); // instruction engine-related
                    }

                    scheduler.addProcess(p);

                    cout << "[New Screen] Created process: " << name
                        << " (" << instr << " instructions)\n";
                    currentProcess = p;
                    inProcessContext = true;
                    cout << "\n[" << name << " @process]> ";
                }
            }

            else if (command.rfind("screen -r", 0) == 0) {
                // reopen process by name
                string name = command.substr(9);
                name.erase(remove_if(name.begin(), name.end(), ::isspace), name.end());
                if (name.empty()) {
                    cout << "Usage: screen -r [name]\n";
                    cout << "\nCommand> " << flush;
                }
                else {
                    bool found = false;
                    {
                        // search ready queue for a process
                        queue<shared_ptr<Process>> temp = scheduler.getReadyQueue();

                        while (!temp.empty()) {
                            auto p = temp.front();
                            temp.pop();
                            if (p->name == name && p->state != FINISHED) { // added check to prevent reataching to finished processes
                                found = true;
                                currentProcess = p;
                                inProcessContext = true;
                                break;
                            }
                        }
                    }
                    if (found) {
                        cout << "[Reattached] Switched to process: " << name << "\n";
                        cout << "\n[" << name << " @process]> ";
                    }
                    else {
                        cout << "[Error] Process not found.\n";
                        cout << "\nCommand> " << flush;
                    }
                }
            }

            else if (command == "screen -ls") {
                scheduler.printStatus(); // shows cores + ready/finished
                scheduler.printUtilization(); // adds core utilization summary
                cout << "\nCommand> " << flush;
            }

            else if (command == "start_marquee") {
                if (!marqueeRunning) {
                    marqueeRunning = true;
                    cout << "Marquee started.\n";
                }
                else cout << "Marquee already running.\n";
                cout << "\nCommand> " << flush;
            }

            else if (command == "scheduler-start") {
                if (!schedulerRunning) {
                    schedulerRunning = true;
                    globalTick = 0;
                    processCounter = 0;
                    scheduler.initialize(config_schedType, config_numCPU, config_quantumCycles);
                    cout << "Scheduler started.\n";

                    for (int i = 0; i < 5; i++) {
                        int instrCount = rand() % 10 + 5;
                        // auto p = make_shared<Process>(i, "p" + to_string(i + 1), rand() % 10 + 5);
                        auto p = make_shared<Process>(i, "p" + to_string(i + 1), instrCount);

                        if (doingnumfour)
                            p->instructions = scheduler.generateTestInstructions(instrCount); // for #4 demo
                        else {
                            p->instructions = scheduler.generateRandomInstructions(instrCount); // instruction engine-related
                        }

                        scheduler.addProcess(p);
                    }
                }
                else cout << "Scheduler already running.\n";
                cout << "\nCommand> " << flush;
            }

            else if (command == "scheduler-stop") {
                if (schedulerRunning) {
                    schedulerRunning = false;

                    cout << "Scheduler stopped.\n";
                    cout << "\nCommand> " << flush;
                }
                else cout << "Scheduler not running...\n";
                cout << "\nCommand> " << flush;
            }

            else if (command == "report-util") {
                scheduler.printStatus();
                //add-on
                scheduler.printUtilization();
                std::string location;
                location = scheduler.saveUtilizationFile("csopesy-log.txt");
                cout << "Report generated at " << location << "\n";
                cout << "\nCommand> " << flush;
            }

            else if (command == "report-cpu") {
                scheduler.printUtilization();
                cout << "\nCommand> " << flush;
            }

            else if (command == "stop_marquee") {
                if (marqueeRunning) {
                    marqueeRunning = false;
                    cout << "Marquee stopped.\n";
                }
                else cout << "Marquee not running.\n";
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
                        }
                        catch (...) {
                            cout << "Invalid speed.\n";
                        }
                        break;
                    }
                }
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
        else {
            // ---------- PROCESS CONTEXT COMMANDS ----------
            if (command == "exit") {
                cout << "[Detaching] Returning to main console...\n";
                inProcessContext = false;
                currentProcess = nullptr;
                cout << "\nCommand> " << flush;
            }

            else if (command == "process-smi") {
                if (currentProcess) {
                    cout << "\nProcess Info:\n";
                    cout << " Name: " << currentProcess->name << "\n";
                    cout << " PID: " << currentProcess->pid << "\n";

                    // Replace the ternary operators with clear logic
                    string stateStr;
                    if (currentProcess->state == READY)
                        stateStr = "READY";
                    else if (currentProcess->state == RUNNING)
                        stateStr = "RUNNING";
                    else if (currentProcess->state == SLEEPING)
                        stateStr = "SLEEPING";
                    else
                        stateStr = "FINISHED";

                    cout << " State: " << stateStr << "\n";
                    cout << " Progress: " << currentProcess->executedInstructions
                        << "/" << currentProcess->totalInstructions << "\n";

                    // Add finished notice
                    if (currentProcess->state == FINISHED) {
                        cout << "Status: Finished!\n";
                    }

                    // Added instruction log
                    if (!currentProcess->log.empty()) {
                        cout << "\nLogs:\n";
                        for (const auto& msg : currentProcess->log)
                            cout << "  " << msg << "\n";
                    }
                }
                else {
                    cout << "[Error] No process attached.\n";
                }

                // Keep prompt consistent
                if (currentProcess)
                    cout << "\n[" << currentProcess->name << " @process]> ";
                else
                    cout << "\n[none @process]> ";
            }
        }
    }
}


int main() {
    loadConfig("config.txt");
    loadASCIIfont("letters.txt");
    marqueeText = textToAscii("CSOPESY"); // make sure the font is alr loaded

    cout << "Command> ";

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