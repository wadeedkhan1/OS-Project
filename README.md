# 🗳️ Synchronized Voting System (C/Linux)

This C program implements a **voting system** that demonstrates the classic **Readers-Writers synchronization problem** using **POSIX threads**, **semaphores**, and **shared memory** on **Linux**.

> **Developed as part of the Operating Systems course in the Software Engineering program at FAST NUCES, Karachi.**

## 🚀 Features

- ✅ Multiple candidates (up to **10**)
- ✅ Supports up to **1000** voters
- ✅ Advanced synchronization using named semaphores
- ✅ Concurrent **reading** and **writing** with proper coordination
- ✅ Reader-preference synchronization pattern
- ✅ Comprehensive logging system with timestamps
- ✅ Performance tracking and comparison between modes
- ✅ Prevention of duplicate voting
- ✅ Three operation modes: **Manual**, **Thread**, and **Process**

---

## 🧠 Core Concepts Used

- **POSIX Threads (`pthread`)**
- **Named Semaphores (`sem_open`, `sem_wait`, `sem_post`)**
- **Shared Memory (`shm_open`, `mmap`)**
- **Process Management (`fork`, `waitpid`)**
- **Readers-Writers Synchronization**
- **Signal Handling (`SIGINT`)**
- **File I/O and Performance Tracking**

---

## 🔧 Synchronization Approach

- 🧑‍🏫 **Reader-preference** implementation:
  - Multiple observers (readers) can view results simultaneously
  - Voters (writers) get exclusive access
- 🔒 Uses four semaphores:
  - `mutex`: Protects access to reader count
  - `wrt`: Controls write access to voting data
  - `read_count_sem`: Manages reader count access
  - `console_sem`: Prevents interleaved console output

---

## 📂 System Components

### Resource Management
- `initialize_resources()`: Sets up semaphores and shared memory
- `cleanup_resources()`: Releases all system resources

### Synchronization 
- `reader_enter()`, `reader_exit()`: Controls observation access
- `writer_enter()`, `writer_exit()`: Controls vote casting access

### Core Functionality
- `cast_vote()`: Records votes with proper synchronization
- `view_results()`: Displays current vote tallies safely

### Operational Modes
- `manual_mode()`: Interactive CLI for voting
- `thread_mode()`: Simulates voting using threads
- `process_mode()`: Simulates voting using separate processes

### Performance Analysis
- `print_performance_comparison()`: Analyzes thread vs process efficiency

---

## 🧪 Simulation Modes

1. **Manual Mode**
   - Interactive CLI interface
   - User manually enters voter and candidate IDs
   - View live results at any time

2. **Thread-Based Simulation**
   - Creates multiple threads for voters and observers
   - Automatically simulates concurrent voting
   - Measures and logs performance metrics

3. **Process-Based Simulation**
   - Uses `fork()` to create child processes for voters and observers 
   - Demonstrates IPC via shared memory
   - Allows performance comparison with thread mode

4. **Performance Comparison**
   - Analyzes efficiency differences between thread and process modes
   - Generates detailed performance reports

---

## 📊 Logging System

- Creates timestamped log files for each session
- Records all voting activities with timestamps
- Maintains separate performance tracking for comparison
- Generates detailed performance analysis reports

---

## ⚙️ Build & Run

### 🔨 Build
```bash
make 
```

### 🧹 Clean Up
```bash
make clean       # Removes executable and main log
make clear_logs  # Removes all vote log files
```

### ▶️ Run
```bash
./voting_system
```

