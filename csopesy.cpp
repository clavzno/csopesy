// change to true if doing #4 in the MO1 submission
bool doingnumfour = false;
/* default config.txt
num-cpu 4
scheduler "rr"
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delay-per-exec 0

num-cpu 4
scheduler "rr"
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delay-per-exec 0

max-overall-mem 65536        # 64KB total memory
mem-per-frame 4096           # 4KB page size
min-mem-per-proc 4096        # 4KB min per process
max-mem-per-proc 32768       # 32KB max per process
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
#include <iomanip>
#include <climits>
// for printing the utilization report location
#include <direct.h>

using namespace std;

// Forward declarations
class Process;
class Scheduler;

// Global declarations
extern Scheduler scheduler;
extern int config_memPerFrame;
extern int config_maxOverallMem;
extern int config_minMemPerProc;
extern int config_maxMemPerProc;

enum ProcessState { READY, RUNNING, SLEEPING, FINISHED };
// shared resources/globals
queue<string> keyboard_queue;
mutex queue_mutex;

atomic<bool> running(true);
atomic<bool> keyboard_stop(false);
atomic<bool> marqueeRunning(false);
atomic<int> marqueeSpeed(200);

// dummy processes
// bool dummyProcessEnabled = true;

string marqueeText = "CSOPESY";
// text to ascii
map<char, vector<string>> asciiFont;
int letterHeight = 6; // in letters.txt, each letter is 6 lines

//new
//atomic<bool> schedulerRunning(false);

int batchProcessFreq = 5;
int minIns = 5;
int maxIns = 15;
int delayPerExec = 200;

atomic<int> globalTick(0);
atomic<int> processCounter(0);

atomic<bool> schedulerRunning(false);

atomic<bool> systemInitialized(false);

// ------ MO2 memory manager ------
struct MemoryStats {
    int totalMemory = 0;
    int usedMemory = 0;
    int freeMemory = 0;
    long long numPagedIn = 0;
    long long numPagedOut = 0;
    long long pageFaults = 0;
};

struct VmCpuStats {
    long long idleCpuTicks = 0;
    long long activeCpuTicks = 0;
    long long totalCpuTicks = 0;
};

MemoryStats g_memoryStats;
VmCpuStats  g_vmCpuStats;

// ----- Demand Paging Structures -----
struct PageTableEntry {
    bool valid = false;
    bool dirty = false;
    bool referenced = false;
    int frameIndex = -1;
    long long lastUsed = 0;   // For LRU approximation
    long backingStoreOffset;  // location in csopesy-backing-store.txt
};

struct ProcessMemory {
    int memSizeBytes;
    int numPages;
    vector<PageTableEntry> pageTable;
    int allocatedFrames = 0;

    ProcessMemory() : memSizeBytes(0), numPages(0) {}
    ProcessMemory(int size, int frameSize); // Declaration only
};

// Global memory structures
vector<int> frameTable;            // maps frame index to process ID (-1 if free)
vector<vector<uint8_t>> physFrames; // Physical memory frames
vector<long long> frameLastUsed;    // For LRU eviction
int totalFrames = 0;

fstream backingStore;
mutex memoryMutex;
mutex backingStoreMutex;

// Memory configuration parameters
int config_maxOverallMem = 0;
int config_memPerFrame = 0;
int config_minMemPerProc = 0;
int config_maxMemPerProc = 0;

// Function declarations
void initializeMemorySystem();
int handlePageFault(Process* p, int page);
bool allocateMemoryForProcess(Process* p);
void releaseProcessMemory(Process* p);
void writePageToBackingStore(int frameIndex, long offset);
void readPageFromBackingStore(int frameIndex, long offset);

// ProcessMemory constructor definition
ProcessMemory::ProcessMemory(int size, int frameSize) : memSizeBytes(size) {
    numPages = (size + frameSize - 1) / frameSize;
    pageTable.resize(numPages);
}

// Now define the Process class
class Process {
private:
    int pid;
    string name;
    ProcessState state;
    int totalInstructions;
    int executedInstructions;
    int sleepTicks;

    // Memory management
    ProcessMemory mem;
    bool memoryFaulted;
    string faultAddressHex;
    string faultTime;

    // instruction engine-related
    vector<string> instructions; // list of instructions to execute
    int instructionPointer; // current instruction index
    map<string, uint16_t> variables; // variable mem
    vector<string> log; // for PRINT

public:
    Process(int id, const string& n, int instrCount) :
        pid(id),
        name(n),
        state(READY),
        totalInstructions(instrCount),
        executedInstructions(0),
        sleepTicks(0),
        memoryFaulted(false),
        instructionPointer(0) {
    }

    // Getters
    int getPid() const { return pid; }
    string getName() const { return name; }
    ProcessState getState() const { return state; }
    int getTotalInstructions() const { return totalInstructions; }
    int getExecutedInstructions() const { return executedInstructions; }
    ProcessMemory& getMemory() { return mem; }
    const ProcessMemory& getMemory() const { return mem; }
    bool isMemoryFaulted() const { return memoryFaulted; }
    string getFaultAddress() const { return faultAddressHex; }
    string getFaultTime() const { return faultTime; }

    // Setters
    void setState(ProcessState s) { state = s; }
    void setSleepTicks(int ticks) { sleepTicks = ticks; }
    void setInstructions(const vector<string>& instr) { instructions = instr; }
    void setMemorySize(int size) {
        mem = ProcessMemory(size, config_memPerFrame);
    }

    // instruction engine-related
    void executeNextInstruction() {
        if (instructionPointer >= instructions.size()) {
            state = FINISHED;
            return;
        }

        stringstream ss(instructions[instructionPointer]);
        string cmd;
        ss >> cmd;

        if (cmd == "DECLARE") {
            string var; uint16_t val;
            if (!(ss >> var >> val)) {
                // Invalid instruction format, ignore or error
                instructionPointer++;
                executedInstructions++;
                return;
            }
            if (variables.size() >= 32 && variables.count(var) == 0) {
                instructionPointer++;
                executedInstructions++;
                return;
            }
            if (mem.numPages > 0) {
                const int SYMBOL_TABLE_PAGE = 0;

                if (!mem.pageTable[SYMBOL_TABLE_PAGE].valid) {
                    handlePageFault(this, SYMBOL_TABLE_PAGE);
                    return; 
                }
                mem.pageTable[SYMBOL_TABLE_PAGE].referenced = true;
                mem.pageTable[SYMBOL_TABLE_PAGE].dirty = true;
                mem.pageTable[SYMBOL_TABLE_PAGE].lastUsed = globalTick;
                frameLastUsed[mem.pageTable[SYMBOL_TABLE_PAGE].frameIndex] = globalTick;
            }

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

            // output should be shown via screen-r process-smi
            log.push_back(msg);
        }
        else if (cmd == "FOR") {
            int numRepeats;
            ss >> numRepeats;
            string rest;
            getline(ss, rest);

            while (!rest.empty() && isspace(rest.front())) {
                rest.erase(rest.begin());
            }

            while (!rest.empty() && isspace(rest.back())) {
                rest.pop_back();
            }

            if (!rest.empty() && numRepeats > 0) {
                vector<string> newInstr;
                for (int i = 0; i < numRepeats; ++i)
                    newInstr.push_back(rest);

                instructions.insert(instructions.begin() + instructionPointer + 1, newInstr.begin(), newInstr.end());
            }
        }
        else if (cmd == "READ") {
            string var, addrStr;
            ss >> var >> addrStr;

            // Convert hex address to decimal
            int addr;
            try {
                addr = stoi(addrStr, nullptr, 16);
            }
            catch (...) {
                memoryFaulted = true;
                faultAddressHex = addrStr;
                time_t t = time(NULL);
                faultTime = ctime(&t);
                if (!faultTime.empty() && faultTime.back() == '\n')
                    faultTime.pop_back();
                state = FINISHED;
                return;
            }

            // Check bounds
            if (addr < 0 || addr >= mem.memSizeBytes) {
                memoryFaulted = true;
                faultAddressHex = addrStr;
                time_t t = time(NULL);
                faultTime = ctime(&t);
                if (!faultTime.empty() && faultTime.back() == '\n')
                    faultTime.pop_back();
                state = FINISHED;
                return;
            }

            int page = addr / config_memPerFrame;
            int offset = addr % config_memPerFrame;

            if (!mem.pageTable[page].valid) {
                handlePageFault(this, page);
            }

            // Mark as referenced
            mem.pageTable[page].referenced = true;
            mem.pageTable[page].lastUsed = globalTick;

            int frame = mem.pageTable[page].frameIndex;
            frameLastUsed[frame] = globalTick;

            // Read from physical memory
            uint16_t val;
            lock_guard<mutex> lock(memoryMutex);
            memcpy(&val, &physFrames[frame][offset], sizeof(uint16_t));
            variables[var] = val;
        }
        else if (cmd == "WRITE") {
            string addrStr; int val;
            ss >> addrStr >> val;

            // Convert hex address to decimal
            int addr;
            try {
                addr = stoi(addrStr, nullptr, 16);
            }
            catch (...) {
                memoryFaulted = true;
                faultAddressHex = addrStr;
                time_t t = time(NULL);
                faultTime = ctime(&t);
                if (!faultTime.empty() && faultTime.back() == '\n')
                    faultTime.pop_back();
                state = FINISHED;
                return;
            }

            // Check bounds
            if (addr < 0 || addr >= mem.memSizeBytes) {
                memoryFaulted = true;
                faultAddressHex = addrStr;
                time_t t = time(NULL);
                faultTime = ctime(&t);
                if (!faultTime.empty() && faultTime.back() == '\n')
                    faultTime.pop_back();
                state = FINISHED;
                return;
            }

            int page = addr / config_memPerFrame;
            int offset = addr % config_memPerFrame;

            if (!mem.pageTable[page].valid) {
                handlePageFault(this, page);
            }

            // Mark as referenced and dirty
            mem.pageTable[page].referenced = true;
            mem.pageTable[page].dirty = true;
            mem.pageTable[page].lastUsed = globalTick;

            int frame = mem.pageTable[page].frameIndex;
            frameLastUsed[frame] = globalTick;

            // Write to physical memory
            uint16_t v16 = (uint16_t)val;
            lock_guard<mutex> lock(memoryMutex);
            memcpy(&physFrames[frame][offset], &v16, sizeof(uint16_t));
        }

        instructionPointer++;
        executedInstructions++;
    }
};

struct CPUCore {
    int id;
    shared_ptr<Process> currentProcess;
    int quantumRemaining; // for round robin

    int activeTicks;
    int totalTicks;

    CPUCore(int coreId) : id(coreId), currentProcess(nullptr), quantumRemaining(0),
        activeTicks(0), totalTicks(0) {
    }

    bool isIdle() const {
        return currentProcess == nullptr;
    }
};

enum SchedulerType { FCFS, RR }; // FCFS, round robin

class Scheduler {
private:
    SchedulerType type;
    int numCPUs;
    int quantumCycles;
    int tickCounter;

    queue<shared_ptr<Process>> readyQueue;
    vector<CPUCore> cores;
    vector<shared_ptr<Process>> finishedList;

    mutex schedMutex; // for general safety

public:
    Scheduler() : type(FCFS), numCPUs(1), quantumCycles(0), tickCounter(0) {}
    shared_ptr<Process> findProcessByName(const string& name) {
        lock_guard<mutex> lock(schedMutex);
        for (auto& core : cores) {
            if (!core.isIdle() && core.currentProcess->getName() == name) {
                return core.currentProcess;
            }
        }
        queue<shared_ptr<Process>> temp = readyQueue;
        while (!temp.empty()) {
            auto p = temp.front();
            temp.pop();
            if (p->getName() == name) {
                return p;
            }
        }
        for (auto& p : finishedList) {
            if (p->getName() == name) {
                return p;
            }
        }
        return nullptr;
    }
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

        // Allocate memory for the process
        if (!allocateMemoryForProcess(p.get())) {
            cout << "[Error] Failed to allocate memory for process " << p->getName() << endl;
            return;
        }

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
                core.currentProcess->setState(RUNNING);
                core.quantumRemaining = quantumCycles;
            }
        }

        for (auto& core : cores) {
            if (!core.isIdle()) {
                core.activeTicks++;
                auto& p = core.currentProcess;

                if (p->getState() == SLEEPING) {
                    // Handle sleep ticks if needed
                }
                else if (p) {
                    try {
                        p->executeNextInstruction();
                        if (p->isMemoryFaulted()) {
                            cout << "[Memory Fault] Process " << p->getName()
                                << " accessed invalid address: " << p->getFaultAddress()
                                << " at " << p->getFaultTime() << endl;
                            p->setState(FINISHED);
                        }
                    }
                    catch (...) {
                        std::cerr << "[Unknown error] in process " << p->getName() << std::endl;
                        p->setState(FINISHED);
                    }

                    if (p->getState() == FINISHED) {
                        // Release memory when process finishes
                        releaseProcessMemory(p.get());
                        finishedList.push_back(p);
                        core.currentProcess = nullptr;
                        continue;
                    }

                    if (type == RR) {
                        core.quantumRemaining--;
                        if (core.quantumRemaining <= 0) {
                            p->setState(READY);
                            readyQueue.push(p);
                            core.currentProcess = nullptr;
                        }
                    }
                }
            }
        }
    }

    struct SchedulerTickSnapshot {
        long long activeTicks = 0;
        long long totalTicks = 0;
    };

    SchedulerTickSnapshot getTickSnapshot() {
        lock_guard<mutex> lock(schedMutex);
        SchedulerTickSnapshot snap;
        for (auto& core : cores) {
            snap.activeTicks += core.activeTicks;
            snap.totalTicks += core.totalTicks;
        }
        return snap;
    }

    vector<string> generateRandomInstructions(int count) {
        vector<string> instr;
        instr.push_back("DECLARE x 5");

        vector<string> possible = {
            "ADD x x 1",
            "SUBTRACT x x 1",
            "PRINT Hello world",
            "SLEEP 2",
            "READ y 0x100",
            "WRITE 0x200 42"
        };

        for (int i = 1; i < count; ++i)
            instr.push_back(possible[rand() % possible.size()]);
        return instr;
    }

    vector<string> generateTestInstructions(int count) {
        vector<string> instr;
        instr.push_back("DECLARE x 0");

        for (int i = 1; i < count; ++i) {
            if (i % 2 == 1) {
                instr.push_back("PRINT value from: x");
            }
            else {
                int addVal = rand() % 10 + 1;
                instr.push_back("ADD x x " + to_string(addVal));
            }
        }
        return instr;
    }

    double getCurrentCPUUtilization() {
        int coresUsed = 0;
        for (auto& core : cores) {
            if (!core.isIdle()) coresUsed++;
        }

        if (cores.empty()) return 0.0;
        if (!schedulerRunning) return 0.0;
        return (static_cast<double>(coresUsed) / cores.size()) * 100.0;
    }

    void printUtilization() {
        lock_guard<mutex> lock(schedMutex);
        cout << "\nCPU Utilization Report\n";

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

    void printStatus() {
        lock_guard<mutex> lock(schedMutex);
        cout << "\nScheduler Status \n";
        cout << "Ready Queue: " << readyQueue.size() << " process(es)\n";
        cout << "Finished: " << finishedList.size() << "\n";
        cout << "Memory Used: " << g_memoryStats.usedMemory << "/" << g_memoryStats.totalMemory
            << " bytes (" << g_memoryStats.pageFaults << " page faults)\n";

        for (auto& core : cores) {
            cout << "Core " << core.id << ": ";
            if (core.isIdle()) cout << "Idle\n";
            else cout << "Running " << core.currentProcess->getName()
                << " (" << core.currentProcess->getExecutedInstructions()
                << "/" << core.currentProcess->getTotalInstructions() << ")\n";
        }
        cout << "------------------------------\n";
    }

    string saveUtilizationFile(const string& filename) {
        lock_guard<mutex> lock(schedMutex);
        ofstream logfile(filename, ios::app);

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

        int coresUsed = 0;
        for (auto& core : cores) {
            if (!core.isIdle()) coresUsed++;
        }

        size_t coresAvailable = cores.size() - static_cast<size_t>(coresUsed);
        double cpuUtil = (cores.empty()) ? 0.0
            : (static_cast<double>(coresUsed) / cores.size()) * 100.0;

        if (!schedulerRunning)
            cpuUtil = 0.0;

        auto now = chrono::system_clock::now();
        time_t time_now = chrono::system_clock::to_time_t(now);

        tm timeinfo{};
        localtime_s(&timeinfo, &time_now);
        char headerTime[30];
        strftime(headerTime, sizeof(headerTime), "%m/%d/%Y %I:%M:%S%p", &timeinfo);

        logfile << "\n\n\n";
        logfile << "------------------------------" << headerTime << "------------------------------" << "\n";
        logfile << "CPU Utilization: " << cpuUtil << "%\n";
        logfile << "Cores used: " << coresUsed << "\n";
        logfile << "Cores available: " << coresAvailable << "\n";
        logfile << "Memory: " << g_memoryStats.usedMemory << "/" << g_memoryStats.totalMemory
            << " bytes, Page Faults: " << g_memoryStats.pageFaults << "\n\n";
        logfile << "Core Utilization: \n";
        logfile << "------------------------------\n";
        logfile << "Running processes:\n";

        for (auto& core : cores) {
            if (!core.isIdle()) {
                auto p = core.currentProcess;
                tm timeinfo{};
                localtime_s(&timeinfo, &time_now);
                char timeStr[30];
                strftime(timeStr, sizeof(timeStr), "%m/%d/%Y %I:%M:%S%p", &timeinfo);

                logfile << p->getName() << " (" << timeStr << ")"
                    << "\tCore: " << core.id << "\t"
                    << p->getExecutedInstructions() << "/" << p->getTotalInstructions()
                    << "\tMem: " << p->getMemory().memSizeBytes << " bytes\n";
            }
        }

        logfile << "\n Finished processes:\n";
        for (auto& p : finishedList) {
            tm timeinfo{};
            localtime_s(&timeinfo, &time_now);
            char timeStr[30];
            strftime(timeStr, sizeof(timeStr), "%m/%d/%Y %I:%M:%S%p", &timeinfo);
            logfile << p->getName() << " (" << timeStr << ")"
                << "\tFinished\t"
                << p->getExecutedInstructions() << "/" << p->getTotalInstructions()
                << "\n";
        }

        logfile << "------------------------------\n";
        logfile.close();

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
            cout << " " << p->getName() << " (" << p->getExecutedInstructions() << "/"
                << p->getTotalInstructions() << ") Mem: " << p->getMemory().memSizeBytes << " bytes\n";
        }
    }

    queue<shared_ptr<Process>> getReadyQueue() {
        lock_guard<mutex> lock(schedMutex);
        return readyQueue;
    }

    vector<shared_ptr<Process>> getProcessList() {
        lock_guard<mutex> lock(schedMutex);
        vector<shared_ptr<Process>> allProcesses;

        // Get processes from ready queue
        queue<shared_ptr<Process>> temp = readyQueue;
        while (!temp.empty()) {
            allProcesses.push_back(temp.front());
            temp.pop();
        }

        // Get running processes
        for (auto& core : cores) {
            if (!core.isIdle()) {
                allProcesses.push_back(core.currentProcess);
            }
        }

        // Get finished processes
        for (auto& p : finishedList) {
            allProcesses.push_back(p);
        }

        return allProcesses;
    }
};

// Global scheduler instance
Scheduler scheduler;

// Memory Management Functions Implementation

void initializeMemorySystem() {
    lock_guard<mutex> lock(memoryMutex);

    // Calculate total frames
    totalFrames = config_maxOverallMem / config_memPerFrame;

    // Initialize physical frames
    physFrames.resize(totalFrames);
    for (int i = 0; i < totalFrames; i++) {
        physFrames[i].resize(config_memPerFrame, 0);
    }

    // Initialize frame table (all free initially)
    frameTable.assign(totalFrames, -1);
    frameLastUsed.assign(totalFrames, 0);

    // Initialize memory stats
    g_memoryStats.totalMemory = config_maxOverallMem;
    g_memoryStats.freeMemory = config_maxOverallMem;
    g_memoryStats.usedMemory = 0;

    // Open backing store
    backingStore.open("csopesy-backing-store.txt", ios::in | ios::out | ios::binary | ios::trunc);
    if (!backingStore.is_open()) {
        cerr << "[Error] Failed to open backing store file." << endl;
    }

    // cout << "[Memory] Initialized " << totalFrames << " frames of "
       // << config_memPerFrame << " bytes each. Total: "
        // << config_maxOverallMem << " bytes." << endl;
}

void writePageToBackingStore(int frameIndex, long offset) {
    lock_guard<mutex> lock(backingStoreMutex);

    if (!backingStore.is_open()) {
        cerr << "[Error] Backing store not open for writing." << endl;
        return;
    }

    backingStore.seekp(offset);
    backingStore.write(reinterpret_cast<char*>(physFrames[frameIndex].data()), config_memPerFrame);
    backingStore.flush();
}

void readPageFromBackingStore(int frameIndex, long offset) {
    lock_guard<mutex> lock(backingStoreMutex);

    if (!backingStore.is_open()) {
        cerr << "[Error] Backing store not open for reading." << endl;
        return;
    }

    backingStore.seekg(offset);
    backingStore.read(reinterpret_cast<char*>(physFrames[frameIndex].data()), config_memPerFrame);
}

bool allocateMemoryForProcess(Process* p) {
    lock_guard<mutex> lock(memoryMutex);

    // Calculate required frames (rounded up)
    int requiredFrames = (p->getMemory().memSizeBytes + config_memPerFrame - 1) / config_memPerFrame;

    if (requiredFrames > totalFrames) {
        cout << "[Error] Process requires " << requiredFrames
            << " frames but only " << totalFrames << " available total." << endl;
        return false;
    }

    if (p->getMemory().memSizeBytes < config_minMemPerProc || p->getMemory().memSizeBytes > config_maxMemPerProc) {
        cout << "[Error] Process memory size " << p->getMemory().memSizeBytes
            << " bytes is outside allowed range [" << config_minMemPerProc
            << ", " << config_maxMemPerProc << "]." << endl;
        return false;
    }

    // Assign backing store offsets to each page
    static long nextBackingStoreOffset = 0;
    for (int i = 0; i < p->getMemory().numPages; i++) {
        p->getMemory().pageTable[i].backingStoreOffset = nextBackingStoreOffset;
        nextBackingStoreOffset += config_memPerFrame;
    }

    //cout << "[Memory] Allocated " << p->getMemory().memSizeBytes << " bytes ("
        // << p->getMemory().numPages << " pages) for process " << p->getName() << endl;

    return true;
}

void releaseProcessMemory(Process* p) {
    lock_guard<mutex> lock(memoryMutex);

    // Write out dirty pages
    for (int i = 0; i < p->getMemory().numPages; i++) {
        if (p->getMemory().pageTable[i].valid && p->getMemory().pageTable[i].dirty) {
            writePageToBackingStore(p->getMemory().pageTable[i].frameIndex,
                p->getMemory().pageTable[i].backingStoreOffset);
            g_memoryStats.numPagedOut++;
        }

        // Free the frame
        if (p->getMemory().pageTable[i].valid) {
            int frame = p->getMemory().pageTable[i].frameIndex;
            frameTable[frame] = -1;
            g_memoryStats.freeMemory += config_memPerFrame;
            g_memoryStats.usedMemory -= config_memPerFrame;
        }
    }

    // cout << "[Memory] Released memory for process " << p->getName() << endl;
}

int handlePageFault(Process* p, int page) {
    lock_guard<mutex> lock(memoryMutex);

    g_memoryStats.pageFaults++;

    // find a free frame
    int freeFrame = -1;
    for (int i = 0; i < totalFrames; i++) {
        if (frameTable[i] == -1) {
            freeFrame = i;
            break;
        }
    }

    // if no free frame, evict one using LRU
    if (freeFrame == -1) {
        // Find least recently used frame
        long long oldest = LLONG_MAX;
        for (int i = 0; i < totalFrames; i++) {
            if (frameLastUsed[i] < oldest) {
                oldest = frameLastUsed[i];
                freeFrame = i;
            }
        }

        // evict the victim frame
        int victimPID = frameTable[freeFrame];

        // find the process that owns this frame and write out if dirty
        auto allProcesses = scheduler.getProcessList();
        for (auto& victimProc : allProcesses) {
            if (victimProc->getPid() == victimPID) {
                for (int i = 0; i < victimProc->getMemory().numPages; i++) {
                    if (victimProc->getMemory().pageTable[i].valid &&
                        victimProc->getMemory().pageTable[i].frameIndex == freeFrame) {

                        if (victimProc->getMemory().pageTable[i].dirty) {
                            writePageToBackingStore(freeFrame,
                                victimProc->getMemory().pageTable[i].backingStoreOffset);
                            g_memoryStats.numPagedOut++;
                        }

                        victimProc->getMemory().pageTable[i].valid = false;
                        victimProc->getMemory().pageTable[i].frameIndex = -1;
                        break;
                    }
                }
                break;
            }
        }
    }

    // load page into the frame
    readPageFromBackingStore(freeFrame, p->getMemory().pageTable[page].backingStoreOffset);

    // update thepage table and frame table
    p->getMemory().pageTable[page].valid = true;
    p->getMemory().pageTable[page].frameIndex = freeFrame;
    p->getMemory().pageTable[page].referenced = true;
    p->getMemory().pageTable[page].lastUsed = globalTick;

    frameTable[freeFrame] = p->getPid();
    frameLastUsed[freeFrame] = globalTick;

    // update memory stats
    g_memoryStats.numPagedIn++;
    g_memoryStats.usedMemory += config_memPerFrame;
    g_memoryStats.freeMemory -= config_memPerFrame;

    // cout << "[Page Fault] Process " << p->getName() << " page " << page
       // << " loaded into frame " << freeFrame << endl;

    return freeFrame;
}

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

    // join everything into one big string
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

// configuration/initialization
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
        else if (key == "max-overall-mem")  config_maxOverallMem = stoi(value);
        else if (key == "mem-per-frame")    config_memPerFrame = stoi(value);
        else if (key == "min-mem-per-proc") config_minMemPerProc = stoi(value);
        else if (key == "max-mem-per-proc") config_maxMemPerProc = stoi(value);
    }

    cout << "[Config] Loaded successfully from " << filename << ":\n";
    cout << " CPUs=" << config_numCPU
        << " Type=" << (config_schedType == RR ? "RR" : "FCFS")
        << " Quantum=" << config_quantumCycles
        << " BatchProcessFreq=" << batchProcessFreq
        << " Minins=" << minIns
        << " Maxins=" << maxIns
        << " Delay=" << delayPerExec << "ms"
        << " MaxMem=" << config_maxOverallMem
        << " FrameSize=" << config_memPerFrame
        << " MinMemProc=" << config_minMemPerProc
        << " MaxMemProc=" << config_maxMemPerProc
        << endl;

    // initialize memory system after loading config
    initializeMemorySystem();
}

// ----- MARQUEE LOGIC -----
void marqueeHandler() {
    int offset = 0;
    while (running) {
        if (marqueeRunning) {
            string asciiText;
            {
                lock_guard<mutex> lock(queue_mutex);
                asciiText = marqueeText;
            }

            // split asciiText into lines
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

void schedulerHandler() {

    while (running) {
        if (schedulerRunning) {
            scheduler.tick();

            globalTick++;
            if (globalTick % batchProcessFreq == 0) {
                int pid = ++processCounter;
                int instrCount = rand() % (maxIns - minIns + 1) + minIns;
                auto p = make_shared<Process>(pid, "autoP" + to_string(pid), instrCount);

                // Assign random memory size within bounds
                int memSize = rand() % (config_maxMemPerProc - config_minMemPerProc + 1) + config_minMemPerProc;
                // Round to nearest power of 2 for simplicity
                memSize = 1 << (int)ceil(log2(memSize));
                p->setMemorySize(memSize);

                if (doingnumfour)
                    p->setInstructions(scheduler.generateTestInstructions(instrCount)); // for #4 demo
                else {
                    p->setInstructions(scheduler.generateRandomInstructions(instrCount)); // instruction engine-related
                }

                scheduler.addProcess(p);

                // cout << "[Scheduler] Auto-created process " << p->getName()
                   // << " with " << instrCount << " instructions, "
                   // << memSize << " bytes memory.\n";
            }

            this_thread::sleep_for(chrono::milliseconds(delayPerExec));
        }
        else {
            // pause it
            if (doingnumfour) scheduler.tick();
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

// ----- COMMAND INTERPRETER -----
void commandInterpreter() {
    shared_ptr<Process> currentProcess = nullptr; // active process context
    bool inProcessContext = false;

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
            if (!systemInitialized && command != "initialize" && command != "help" && command != "exit") {
                cout << "[Error] Please run 'initialize' first before any other command.\n";
                cout << "\nCommand> " << flush;
                continue;
            }

            //begin command handling
            if (command == "help") {
                system("cls");
                cout << "\nAvailable commands:\n"
                    << " help                  - Show this help menu\n"
                    << " screen -s [name] [mem]- Creates a new process with memory size\n"
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
                    << " meminfo               - Show memory information\n"
                    << " vmstat                - Show memory and CPU tick stats\n"
                    << " exit                  - Quit the emulator\n";
                cout << "\nCommand> " << flush;
            }

            else if (command == "initialize") {
                loadConfig("config.txt");
                scheduler.initialize(config_schedType, config_numCPU, config_quantumCycles);
                systemInitialized = true; // mark as ready
                cout << "[OK] Scheduler initialized. CPUs=" << config_numCPU
                    << " Type=" << (config_schedType == RR ? "RR" : "FCFS")
                    << " Quantum=" << config_quantumCycles << endl;
                cout << "\nCommand> " << flush;
            }

            else if (command.rfind("screen -s", 0) == 0 || command.rfind("screen -c", 0) == 0) {
                stringstream ss(command);
                string tmp, name, instructionsStr;
                int memSize = 0;
                char mode;

                ss >> tmp >> tmp;
                mode = tmp.back();

                ss >> name;

                if (mode == 's') {
                    ss >> memSize;
                }
                else if (mode == 'c') {
                    ss >> memSize;
                    string rest;
                    getline(ss, rest);
                    rest.erase(0, rest.find_first_not_of(" \t"));
                    rest.erase(rest.find_last_not_of(" \t") + 1);
                    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
                        instructionsStr = rest.substr(1, rest.size() - 2);
                    }
                    else {
                        cout << "[Error] Instructions for 'screen -c' must be enclosed in double quotes.\n";
                        cout << "\nCommand> " << flush;
                        continue;
                    }
                }
                else {
                    cout << "Usage: screen -s <name> <memory_size_in_bytes> OR screen -c <name> <memory_size_in_bytes> \"<instructions>\"\n";
                    cout << "\nCommand> " << flush;
                    continue;
                }
                if (name.empty() || memSize == 0) {
                    cout << "Usage: screen -s <name> <memory_size_in_bytes> OR screen -c <name> <memory_size_in_bytes> \"<instructions>\"\n";
                    cout << "\nCommand> " << flush;
                    continue;
                }
                if (memSize < config_minMemPerProc || memSize > config_maxMemPerProc) {
                    cout << "[Error] Memory size must be between " << config_minMemPerProc
                        << " and " << config_maxMemPerProc << " bytes\n";
                    cout << "\nCommand> " << flush;
                    continue;
                }
                if ((memSize & (memSize - 1)) != 0) {
                    cout << "[Error] Invalid memory allocation. Memory size must be a power of 2.\n";
                    cout << "\nCommand> " << flush;
                    continue;
                }
                vector<string> instructions;
                if (mode == 'c') {
                    stringstream instrSS(instructionsStr);
                    string instr;
                    while (getline(instrSS, instr, ';')) {
                        instr.erase(0, instr.find_first_not_of(" \t"));
                        instr.erase(instr.find_last_not_of(" \t") + 1);
                        if (!instr.empty()) {
                            instructions.push_back(instr);
                        }
                    }

                    if (instructions.size() < 1 || instructions.size() > 50) {
                        cout << "[Error] Invalid command. Instruction count must be between 1 and 50.\n";
                        cout << "\nCommand> " << flush;
                        continue;
                    }
                }

                int pid = ++processCounter;
                int instrCount = (mode == 'c') ? instructions.size() : (rand() % (maxIns - minIns + 1) + minIns);
                auto p = make_shared<Process>(pid, name, instrCount);
                p->setMemorySize(memSize);

                if (mode == 'c') {
                    p->setInstructions(instructions);
                }
                else if (doingnumfour) {
                    p->setInstructions(scheduler.generateTestInstructions(instrCount));
                }
                else {
                    p->setInstructions(scheduler.generateRandomInstructions(instrCount));
                }

                scheduler.addProcess(p);

                cout << "[New Screen] Created process: " << name
                    << " (" << instrCount << " instructions, "
                    << memSize << " bytes memory)\n";
                cout << "\nCommand> " << flush;
            }

            else if (command.rfind("screen -r", 0) == 0) {
                string name = command.substr(9);
                name.erase(remove_if(name.begin(), name.end(), ::isspace), name.end());

                if (name.empty()) {
                    cout << "Usage: screen -r [name]\n";
                    cout << "\nCommand> " << flush;
                    continue;
                }

                auto p = scheduler.findProcessByName(name);

                if (p) {
                    if (p->getState() == FINISHED) {
                        if (p->isMemoryFaulted()) {
                            cout << "Process " << p->getName() << " shut down due to memory access violation error that occurred at "
                                << p->getFaultTime() << ". " << p->getFaultAddress() << " invalid.\n";
                        }
                        else {
                            cout << "Process " << p->getName() << " finished execution.\n";
                        }
                        cout << "\nCommand> " << flush;
                    }
                    else if (p->getState() == READY || p->getState() == RUNNING || p->getState() == SLEEPING) {
                        currentProcess = p;
                        inProcessContext = true;
                        cout << "[Reattached] Switched to process: " << name << "\n";
                        cout << "\n[" << name << " @process]> ";
                    }
                }
                else {
                    cout << "Process " << name << " not found.\n";
                    cout << "\nCommand> " << flush;
                }
                }

            else if (command == "screen -ls") {
                scheduler.printStatus();
                scheduler.printUtilization();
                cout << "\nCommand> " << flush;
            }

            else if (command == "meminfo") {
                cout << "\n=== Memory Information ===\n";
                cout << "Total Memory: " << g_memoryStats.totalMemory << " bytes\n";
                cout << "Used Memory: " << g_memoryStats.usedMemory << " bytes\n";
                cout << "Free Memory: " << g_memoryStats.freeMemory << " bytes\n";
                cout << "Page Size: " << config_memPerFrame << " bytes\n";
                cout << "Total Frames: " << totalFrames << "\n";
                cout << "Page Faults: " << g_memoryStats.pageFaults << "\n";
                cout << "Pages Paged In: " << g_memoryStats.numPagedIn << "\n";
                cout << "Pages Paged Out: " << g_memoryStats.numPagedOut << "\n";

                // Show frame table
                cout << "\nFrame Table:\n";
                for (int i = 0; i < totalFrames; i++) {
                    cout << "Frame " << i << ": ";
                    if (frameTable[i] == -1)
                        cout << "Free";
                    else
                        cout << "Process " << frameTable[i];
                    cout << " (Last Used: " << frameLastUsed[i] << ")\n";
                }
                cout << "------------------------------\n";
                cout << "\nCommand> " << flush;
            }
            else if (command == "process-smi") {
                double cpuUtil = scheduler.getCurrentCPUUtilization();

                cout << "\n================= CSOPESY-SMI =================\n";
                cout << "| Memory (Used/Total) : " << g_memoryStats.usedMemory << "B / " << g_memoryStats.totalMemory << "B\n";
                cout << "| Utilization (CPU)   : " << fixed << setprecision(2) << cpuUtil << "%\n";
                cout << "===================================================================\n";
                cout << "| PID | Process Name | State | Memory Usage | Instructions |\n";
                cout << "===================================================================\n";

                auto allProcesses = scheduler.getProcessList();
                for (const auto& p : allProcesses) {
                    string stateStr;
                    ProcessState state = p->getState();
                    if (state == READY) stateStr = "READY";
                    else if (state == RUNNING) stateStr = "RUNNING";
                    else if (state == SLEEPING) stateStr = "SLEEPING";
                    else stateStr = "FINISHED";

                    cout << "| " << setw(3) << p->getPid()
                        << " | " << setw(12) << p->getName()
                        << " | " << setw(5) << stateStr
                        << " | " << setw(12) << p->getMemory().memSizeBytes << " B"
                        << " | " << setw(12) << p->getExecutedInstructions() << "/" << p->getTotalInstructions()
                        << " |\n";
                }
                cout << "===================================================================\n";
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
                        auto p = make_shared<Process>(i, "p" + to_string(i + 1), instrCount);

                        // Assign random memory size
                        int memSize = rand() % (config_maxMemPerProc - config_minMemPerProc + 1) + config_minMemPerProc;
                        memSize = 1 << (int)ceil(log2(memSize));
                        p->setMemorySize(memSize);

                        if (doingnumfour)
                            p->setInstructions(scheduler.generateTestInstructions(instrCount));
                        else {
                            p->setInstructions(scheduler.generateRandomInstructions(instrCount));
                        }

                        scheduler.addProcess(p);
                        // cout << "[Scheduler] Created process " << p->getName()
                           // << " with " << memSize << " bytes memory\n";
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
            else if (command == "vmstat") {
                // snapshot of cpu ticks from scheduler
                auto snap = scheduler.getTickSnapshot();
                g_vmCpuStats.totalCpuTicks = snap.totalTicks;
                g_vmCpuStats.activeCpuTicks = snap.activeTicks;
                g_vmCpuStats.idleCpuTicks = g_vmCpuStats.totalCpuTicks - g_vmCpuStats.activeCpuTicks;
                if (g_vmCpuStats.idleCpuTicks < 0) {
                    g_vmCpuStats.idleCpuTicks = 0;
                }
                cout << "\n=== vmstat ===\n";

                // system-wide memory statistics here
                cout << "Total memory : " << g_memoryStats.totalMemory << " bytes\n";
                cout << "Used memory  : " << g_memoryStats.usedMemory << " bytes\n";
                cout << "Free memory  : " << g_memoryStats.freeMemory << " bytes\n";
                cout << "Paged in     : " << g_memoryStats.numPagedIn << "\n";
                cout << "Paged out    : " << g_memoryStats.numPagedOut << "\n";
                cout << "Page faults  : " << g_memoryStats.pageFaults << "\n\n";

                // CPU tick stats here
                cout << "Idle cpu ticks   : " << g_vmCpuStats.idleCpuTicks << "\n";
                cout << "Active cpu ticks : " << g_vmCpuStats.activeCpuTicks << "\n";
                cout << "Total cpu ticks  : " << g_vmCpuStats.totalCpuTicks << "\n";

                cout << "------------------------------\n";
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
                    cout << "\n=== Process Information ===\n";
                    cout << "Name: " << currentProcess->getName() << "\n";
                    cout << "PID: " << currentProcess->getPid() << "\n";

                    string stateStr;
                    ProcessState state = currentProcess->getState();
                    if (state == READY)
                        stateStr = "READY";
                    else if (state == RUNNING)
                        stateStr = "RUNNING";
                    else if (state == SLEEPING)
                        stateStr = "SLEEPING";
                    else
                        stateStr = "FINISHED";

                    cout << "State: " << stateStr << "\n";
                    cout << "Progress: " << currentProcess->getExecutedInstructions()
                        << "/" << currentProcess->getTotalInstructions() << "\n";
                    cout << "Memory: " << currentProcess->getMemory().memSizeBytes << " bytes\n";
                    cout << "Pages: " << currentProcess->getMemory().numPages << "\n";

                    if (currentProcess->isMemoryFaulted()) {
                        cout << "Status: MEMORY FAULT at address " << currentProcess->getFaultAddress()
                            << " (" << currentProcess->getFaultTime() << ")\n";
                    }

                    // Show page table
                    cout << "\nPage Table:\n";
                    for (int i = 0; i < currentProcess->getMemory().numPages; i++) {
                        auto& entry = currentProcess->getMemory().pageTable[i];
                        cout << "Page " << i << ": ";
                        if (entry.valid) {
                            cout << "Frame " << entry.frameIndex;
                            if (entry.dirty) cout << " (Dirty)";
                            if (entry.referenced) cout << " (Referenced)";
                        }
                        else {
                            cout << "Not in memory";
                        }
                        cout << "\n";
                    }

                    cout << "------------------------------\n";
                    if (currentProcess)
                        cout << "\n[" << currentProcess->getName() << " @process]> ";
                    else
                        cout << "\nCommand> ";
                }
            }
            else {
                cout << "[Error] Unknown command in process context.\n";
                if (currentProcess)
                    cout << "\n[" << currentProcess->getName() << " @process]> ";
                else
                    cout << "\nCommand> ";
            }
        }
    }
}

void printNames() {
    cout << "\n\nWelcome to CSOPESY! Type 'help' for commands.\n"
        << "Group 5 Developers: \n"
        << "Brillantes, Althea\n"
        << "Clavano, Angelica (Jack)\n"
        << "Narito, Ivan\n"
        << "Version Date: November 30, 2025\n"
        << "----------------------------------------\n";
}

int main() {
    srand(time(NULL));
    loadConfig("config.txt");
    loadASCIIfont("letters.txt");
    marqueeText = textToAscii("CSOPESY");

    printNames();
    cout << "Command> ";

    thread kbThread(keyboardHandler);
    thread marqueeThread(marqueeHandler);
    thread commandThread(commandInterpreter);
    thread schedulerThread(schedulerHandler);

    kbThread.join();
    marqueeThread.join();
    commandThread.join();
    schedulerThread.join();

    // Close backing store
    if (backingStore.is_open()) {
        backingStore.close();
    }

    return 0;
}