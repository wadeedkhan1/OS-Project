# 🗳️ Voting System with Readers-Writers Synchronization (C/Linux)

This C program implements a **voting system** that demonstrates the classic **Readers-Writers synchronization problem** using **POSIX threads**, **semaphores**, and **shared memory** on **Linux**.

## 🚀 Features

- ✅ Multiple candidates (up to **5**)
- ✅ Supports up to **100** voters
- ✅ Proper synchronization using semaphores
- ✅ Concurrent **reading** and **writing** access
- ✅ Reader-preference pattern
- ✅ Clean resource handling and termination
- ✅ Both **thread-based** and **process-based** simulations

---

## 🧠 Core Concepts Used

- **POSIX Threads (`pthread`)**
- **Semaphores (`sem_t`)**
- **Shared Memory (`shm_open`, `mmap`)**
- **Processes (`fork`)**
- **Readers-Writers Synchronization**
- **Signal Handling (`SIGINT`)**

---

## 🔧 Synchronization Approach

- 🧑‍🏫 **Reader-preference** pattern:
  - Multiple readers can access the data simultaneously.
  - Writers get **exclusive access**.
- 🧵 Uses `sem_t`:
  - `mutex`: protects reader count
  - `write_lock`: ensures mutual exclusion for writers

---

## 📂 Functional Overview

### `initialize_system(int candidate_count, char *candidate_names[])`

- Sets up the signal handler
- Creates and initializes:
  - Shared memory
  - Semaphores
  - Candidate vote counters
- Opens log file

### `cleanup_system()`

- Releases all system resources:
  - Unlinks semaphores
  - Unmaps/unlinks shared memory
  - Closes log file

### `reader_thread(void *arg)`

- Thread function for **reader** operations
- Follows reader-writer protocol
- Displays current vote counts (no modifications)

### `writer_thread(void *arg)`

- Thread function for **writer** operations
- Casts votes for random candidates
- Checks for duplicates and respects voter limits

### `reader_process(int id)`

- Forked process version of the reader
- Maps shared memory independently
- Reads vote counts 5 times before exiting

### `writer_process(int id)`

- Forked process version of the writer
- Casts 3 votes before exiting

### `log_message(const char *message)`

- Logs timestamped events to a file

### `signal_handler(int sig)`

- Handles `SIGINT` (Ctrl+C)
- Triggers `cleanup_system()` gracefully

---

## 🧪 Simulation Modes

1. **Manual Mode**
   - View vote counts (Reader)
   - Cast votes (Writer)

2. **Thread-Based Simulation**
   - Spawns multiple threads for concurrent readers/writers

3. **Process-Based Simulation**
   - Spawns separate processes (via `fork()`) for readers and writers

---

## ⚙️ Build & Run

### 🔨 Compile

```bash
gcc OS_Project.c -o voting_system -pthread -lrt
```
###▶️ Run
```bash
./voting_system
```

