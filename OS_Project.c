#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#define MAX_CANDIDATES 5
#define MAX_VOTERS 100
#define SHM_NAME "/voting_system_shm"
#define SEM_MUTEX "/voting_mutex"
#define SEM_WRTACCESS "/voting_wrt"
#define SEM_READCNT "/voting_readcnt"
#define LOG_FILE "voting_log.txt"

// Shared memory structure
typedef struct {
    int votes[MAX_CANDIDATES];
    int voter_ids[MAX_VOTERS];
    int voter_count;
    int candidate_count;
    char candidate_names[MAX_CANDIDATES][50];
} VotingData;

// Global variables
VotingData *voting_data = NULL;
sem_t *mutex = NULL;
sem_t *wrt = NULL;
sem_t *readcnt_mutex = NULL;
int *readcount = NULL;
FILE *log_file = NULL;
int running = 1;

// Function prototypes
void initialize_system(int candidate_count, char *candidate_names[]);
void cleanup_system();
void *reader_thread(void *arg);
void *writer_thread(void *arg);
void reader_process(int id);
void writer_process(int id);
void log_message(const char *message);
void signal_handler(int sig);

// Initialize the voting system
void initialize_system(int candidate_count, char *candidate_names[]) {
    // Set up signal handler
    signal(SIGINT, signal_handler);
    
    // Open log file
    log_file = fopen(LOG_FILE, "w");
    if (!log_file) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    // Create shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(EXIT_FAILURE);
    }
    
    // Set size of shared memory
    if (ftruncate(shm_fd, sizeof(VotingData) + sizeof(int)) == -1) {
        perror("ftruncate failed");
        exit(EXIT_FAILURE);
    }
    
    // Map shared memory
    voting_data = (VotingData *)mmap(NULL, sizeof(VotingData), 
                                    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (voting_data == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }
    
    // Map readcount in shared memory
    readcount = (int *)mmap(NULL, sizeof(int), 
                           PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, sizeof(VotingData));
    if (readcount == MAP_FAILED) {
        perror("mmap failed for readcount");
        exit(EXIT_FAILURE);
    }
    
    // Initialize voting data
    memset(voting_data, 0, sizeof(VotingData));
    voting_data->candidate_count = candidate_count;
    for (int i = 0; i < candidate_count && i < MAX_CANDIDATES; i++) {
        strncpy(voting_data->candidate_names[i], candidate_names[i], 49);
        voting_data->candidate_names[i][49] = '\0';
    }
    
    // Initialize readcount
    *readcount = 0;
    
    // Create semaphores
    mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);
    wrt = sem_open(SEM_WRTACCESS, O_CREAT, 0666, 1);
    readcnt_mutex = sem_open(SEM_READCNT, O_CREAT, 0666, 1);
    
    if (mutex == SEM_FAILED || wrt == SEM_FAILED || readcnt_mutex == SEM_FAILED) {
        perror("sem_open failed");
        cleanup_system();
        exit(EXIT_FAILURE);
    }
    
    log_message("Voting system initialized");
}

// Clean up resources
void cleanup_system() {
    // Close and unlink semaphores
    sem_close(mutex);
    sem_close(wrt);
    sem_close(readcnt_mutex);
    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_WRTACCESS);
    sem_unlink(SEM_READCNT);
    
    // Unmap and unlink shared memory
    if (voting_data != MAP_FAILED && voting_data != NULL) {
        munmap(voting_data, sizeof(VotingData));
    }
    if (readcount != MAP_FAILED && readcount != NULL) {
        munmap(readcount, sizeof(int));
    }
    shm_unlink(SHM_NAME);
    
    // Close log file
    if (log_file) {
        fclose(log_file);
    }
    
    log_message("Voting system cleaned up");
}

// Log messages with timestamp
void log_message(const char *message) {
    if (!log_file) return;
    
    time_t now = time(NULL);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(log_file, "[%s] %s\n", timestamp, message);
    fflush(log_file);
}

// Reader thread function
void *reader_thread(void *arg) {
    int id = *((int *)arg);
    char log_buf[256];
    
    while (running) {
        // Entry section
        sem_wait(readcnt_mutex);
        (*readcount)++;
        if (*readcount == 1) {
            sem_wait(wrt);  // First reader blocks writers
        }
        sem_post(readcnt_mutex);
        
        // Critical section - read votes
        sprintf(log_buf, "Reader %d: Reading vote counts", id);
        log_message(log_buf);
        
        printf("\n--- Current Vote Counts (Reader %d) ---\n", id);
        for (int i = 0; i < voting_data->candidate_count; i++) {
            printf("%s: %d votes\n", voting_data->candidate_names[i], voting_data->votes[i]);
        }
        printf("Total voters: %d\n", voting_data->voter_count);
        
        // Simulate reading time
        usleep(rand() % 500000 + 100000);  // 0.1-0.6 seconds
        
        // Exit section
        sem_wait(readcnt_mutex);
        (*readcount)--;
        if (*readcount == 0) {
            sem_post(wrt);  // Last reader unblocks writers
        }
        sem_post(readcnt_mutex);
        
        // Random sleep before next read
        usleep(rand() % 2000000 + 1000000);  // 1-3 seconds
    }
    
    free(arg);
    return NULL;
}

// Writer thread function
void *writer_thread(void *arg) {
    int id = *((int *)arg);
    char log_buf[256];
    
    while (running) {
        // Generate a random voter ID (1-1000)
        int voter_id = rand() % 1000 + 1;
        
        // Entry section - request exclusive access
        sem_wait(wrt);
        
        // Critical section - cast vote
        int duplicate = 0;
        
        // Check for duplicate votes
        for (int i = 0; i < voting_data->voter_count; i++) {
            if (voting_data->voter_ids[i] == voter_id) {
                duplicate = 1;
                break;
            }
        }
        
        if (!duplicate && voting_data->voter_count < MAX_VOTERS) {
            // Choose a random candidate
            int candidate = rand() % voting_data->candidate_count;
            
            // Cast vote
            voting_data->votes[candidate]++;
            voting_data->voter_ids[voting_data->voter_count++] = voter_id;
            
            sprintf(log_buf, "Writer %d: Voter %d voted for %s", 
                    id, voter_id, voting_data->candidate_names[candidate]);
            log_message(log_buf);
            
            printf("\nWriter %d: Voter %d cast a vote for %s\n", 
                   id, voter_id, voting_data->candidate_names[candidate]);
        } else if (duplicate) {
            sprintf(log_buf, "Writer %d: Voter %d attempted duplicate vote", id, voter_id);
            log_message(log_buf);
            printf("\nWriter %d: Voter %d attempted to vote again (rejected)\n", id, voter_id);
        } else {
            sprintf(log_buf, "Writer %d: Maximum voter limit reached", id);
            log_message(log_buf);
            printf("\nWriter %d: Maximum voter limit reached\n", id);
        }
        
        // Simulate writing time
        usleep(rand() % 1000000 + 500000);  // 0.5-1.5 seconds
        
        // Exit section - release exclusive access
        sem_post(wrt);
        
        // Random sleep before next write
        usleep(rand() % 3000000 + 2000000);  // 2-5 seconds
    }
    
    free(arg);
    return NULL;
}

// Reader process function
void reader_process(int id) {
    srand(time(NULL) ^ (id * 1000));
    char log_buf[256];
    
    // Map shared memory
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Child: shm_open failed");
        exit(EXIT_FAILURE);
    }
    
    voting_data = (VotingData *)mmap(NULL, sizeof(VotingData), 
                                    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    readcount = (int *)mmap(NULL, sizeof(int), 
                           PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, sizeof(VotingData));
    
    // Open semaphores
    mutex = sem_open(SEM_MUTEX, 0);
    wrt = sem_open(SEM_WRTACCESS, 0);
    readcnt_mutex = sem_open(SEM_READCNT, 0);
    
    // Open log file
    log_file = fopen(LOG_FILE, "a");
    
    sprintf(log_buf, "Reader process %d started", id);
    log_message(log_buf);
    
    // Reader loop
    for (int i = 0; i < 5; i++) {  // Each process reads 5 times
        // Entry section
        sem_wait(readcnt_mutex);
        (*readcount)++;
        if (*readcount == 1) {
            sem_wait(wrt);  // First reader blocks writers
        }
        sem_post(readcnt_mutex);
        
        // Critical section - read votes
        sprintf(log_buf, "Reader process %d: Reading vote counts", id);
        log_message(log_buf);
        
        printf("\n--- Current Vote Counts (Reader Process %d) ---\n", id);
        for (int j = 0; j < voting_data->candidate_count; j++) {
            printf("%s: %d votes\n", voting_data->candidate_names[j], voting_data->votes[j]);
        }
        printf("Total voters: %d\n", voting_data->voter_count);
        
        // Simulate reading time
        usleep(rand() % 500000 + 100000);  // 0.1-0.6 seconds
        
        // Exit section
        sem_wait(readcnt_mutex);
        (*readcount)--;
        if (*readcount == 0) {
            sem_post(wrt);  // Last reader unblocks writers
        }
        sem_post(readcnt_mutex);
        
        // Random sleep before next read
        usleep(rand() % 2000000 + 1000000);  // 1-3 seconds
    }
    
    sprintf(log_buf, "Reader process %d finished", id);
    log_message(log_buf);
    
    // Clean up
    if (log_file) fclose(log_file);
    sem_close(mutex);
    sem_close(wrt);
    sem_close(readcnt_mutex);
    munmap(voting_data, sizeof(VotingData));
    munmap(readcount, sizeof(int));
    
    exit(EXIT_SUCCESS);
}

// Writer process function
void writer_process(int id) {
    srand(time(NULL) ^ (id * 2000));
    char log_buf[256];
    
    // Map shared memory
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Child: shm_open failed");
        exit(EXIT_FAILURE);
    }
    
    voting_data = (VotingData *)mmap(NULL, sizeof(VotingData), 
                                    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    // Open semaphores
    wrt = sem_open(SEM_WRTACCESS, 0);
    
    // Open log file
    log_file = fopen(LOG_FILE, "a");
    
    sprintf(log_buf, "Writer process %d started", id);
    log_message(log_buf);
    
    // Writer loop
    for (int i = 0; i < 3; i++) {  // Each process writes 3 times
        // Generate a random voter ID (1-1000)
        int voter_id = rand() % 1000 + 1;
        
        // Entry section - request exclusive access
        sem_wait(wrt);
        
        // Critical section - cast vote
        int duplicate = 0;
        
        // Check for duplicate votes
        for (int j = 0; j < voting_data->voter_count; j++) {
            if (voting_data->voter_ids[j] == voter_id) {
                duplicate = 1;
                break;
            }
        }
        
        if (!duplicate && voting_data->voter_count < MAX_VOTERS) {
            // Choose a random candidate
            int candidate = rand() % voting_data->candidate_count;
            
            // Cast vote
            voting_data->votes[candidate]++;
            voting_data->voter_ids[voting_data->voter_count++] = voter_id;
            
            sprintf(log_buf, "Writer process %d: Voter %d voted for %s", 
                    id, voter_id, voting_data->candidate_names[candidate]);
            log_message(log_buf);
            
            printf("\nWriter process %d: Voter %d cast a vote for %s\n", 
                   id, voter_id, voting_data->candidate_names[candidate]);
        } else if (duplicate) {
            sprintf(log_buf, "Writer process %d: Voter %d attempted duplicate vote", id, voter_id);
            log_message(log_buf);
            printf("\nWriter process %d: Voter %d attempted to vote again (rejected)\n", id, voter_id);
        } else {
            sprintf(log_buf, "Writer process %d: Maximum voter limit reached", id);
            log_message(log_buf);
            printf("\nWriter process %d: Maximum voter limit reached\n", id);
        }
        
        // Simulate writing time
        usleep(rand() % 1000000 + 500000);  // 0.5-1.5 seconds
        
        // Exit section - release exclusive access
        sem_post(wrt);
        
        // Random sleep before next write
        usleep(rand() % 3000000 + 2000000);  // 2-5 seconds
    }
    
    sprintf(log_buf, "Writer process %d finished", id);
    log_message(log_buf);
    
    // Clean up
    if (log_file) fclose(log_file);
    sem_close(wrt);
    munmap(voting_data, sizeof(VotingData));
    
    exit(EXIT_SUCCESS);
}

// Signal handler for clean termination
void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\nReceived interrupt signal. Cleaning up and exiting...\n");
        running = 0;
        sleep(2);  // Give threads time to finish
        cleanup_system();
        exit(EXIT_SUCCESS);
    }
}

// Display menu and handle user input
void display_menu() {
    printf("\n===== Voting System Menu =====\n");
    printf("1. Show current vote counts\n");
    printf("2. Cast a vote\n");
    printf("3. Start simulation (threads)\n");
    printf("4. Start simulation (processes)\n");
    printf("5. Exit\n");
    printf("Enter your choice: ");
}

// Main function
int main(int argc, char *argv[]) {
    srand(time(NULL));
    
    // Default candidate names
    char *candidate_names[MAX_CANDIDATES] = {
        "Alice", "Bob", "Charlie", "Dave", "Eve"
    };
    
    // Initialize the voting system
    initialize_system(MAX_CANDIDATES, candidate_names);
    
    int choice;
    pthread_t threads[20];  // Array to hold thread IDs
    int thread_count = 0;
    pid_t child_pids[10];   // Array to hold child process IDs
    int process_count = 0;
    
    printf("Welcome to the Voting System!\n");
    printf("This system demonstrates the Readers-Writers synchronization problem.\n");
    
    while (1) {
        display_menu();
        if (scanf("%d", &choice) != 1) {
            // Clear input buffer on invalid input
            while (getchar() != '\n');
            printf("Invalid input. Please try again.\n");
            continue;
        }
        
        switch (choice) {
            case 1: {
                // Show current vote counts (reader operation)
                printf("\n--- Current Vote Counts ---\n");
                
                // Entry section
                sem_wait(readcnt_mutex);
                (*readcount)++;
                if (*readcount == 1) {
                    sem_wait(wrt);  // First reader blocks writers
                }
                sem_post(readcnt_mutex);
                
                // Critical section - read votes
                for (int i = 0; i < voting_data->candidate_count; i++) {
                    printf("%s: %d votes\n", voting_data->candidate_names[i], voting_data->votes[i]);
                }
                printf("Total voters: %d\n", voting_data->voter_count);
                
                // Exit section
                sem_wait(readcnt_mutex);
                (*readcount)--;
                if (*readcount == 0) {
                    sem_post(wrt);  // Last reader unblocks writers
                }
                sem_post(readcnt_mutex);
                
                break;
            }
            case 2: {
                // Cast a vote (writer operation)
                int voter_id, candidate;
                
                printf("Enter voter ID (1-1000): ");
                if (scanf("%d", &voter_id) != 1 || voter_id < 1 || voter_id > 1000) {
                    printf("Invalid voter ID. Must be between 1 and 1000.\n");
                    while (getchar() != '\n');  // Clear input buffer
                    break;
                }
                
                printf("Available candidates:\n");
                for (int i = 0; i < voting_data->candidate_count; i++) {
                    printf("%d. %s\n", i+1, voting_data->candidate_names[i]);
                }
                
                printf("Enter candidate number: ");
                if (scanf("%d", &candidate) != 1 || 
                    candidate < 1 || candidate > voting_data->candidate_count) {
                    printf("Invalid candidate number.\n");
                    while (getchar() != '\n');  // Clear input buffer
                    break;
                }
                candidate--;  // Adjust to 0-based index
                
                // Entry section - request exclusive access
                sem_wait(wrt);
                
                // Critical section - cast vote
                int duplicate = 0;
                
                // Check for duplicate votes
                for (int i = 0; i < voting_data->voter_count; i++) {
                    if (voting_data->voter_ids[i] == voter_id) {
                        duplicate = 1;
                        break;
                    }
                }
                
                if (!duplicate && voting_data->voter_count < MAX_VOTERS) {
                    // Cast vote
                    voting_data->votes[candidate]++;
                    voting_data->voter_ids[voting_data->voter_count++] = voter_id;
                    
                    char log_buf[256];
                    sprintf(log_buf, "Manual vote: Voter %d voted for %s", 
                            voter_id, voting_data->candidate_names[candidate]);
                    log_message(log_buf);
                    
                    printf("Vote successfully cast for %s\n", 
                           voting_data->candidate_names[candidate]);
                } else if (duplicate) {
                    printf("Error: Voter %d has already voted.\n", voter_id);
                } else {
                    printf("Error: Maximum voter limit reached.\n");
                }
                
                // Exit section - release exclusive access
                sem_post(wrt);
                
                break;
            }
            case 3: {
                // Start simulation with threads
                int num_readers, num_writers;
                
                printf("Enter number of reader threads (1-10): ");
                if (scanf("%d", &num_readers) != 1 || num_readers < 1 || num_readers > 10) {
                    printf("Invalid number. Using default of 3 readers.\n");
                    num_readers = 3;
                }
                
                printf("Enter number of writer threads (1-10): ");
                if (scanf("%d", &num_writers) != 1 || num_writers < 1 || num_writers > 10) {
                    printf("Invalid number. Using default of 2 writers.\n");
                    num_writers = 2;
                }
                
                printf("Starting simulation with %d readers and %d writers...\n", 
                       num_readers, num_writers);
                
                // Create reader threads
                for (int i = 0; i < num_readers; i++) {
                    int *id = malloc(sizeof(int));
                    *id = i + 1;
                    if (pthread_create(&threads[thread_count++], NULL, reader_thread, id) != 0) {
                        perror("Failed to create reader thread");
                        free(id);
                    } else {
                        printf("Reader thread %d created\n", *id);
                    }
                }
                
                // Create writer threads
                for (int i = 0; i < num_writers; i++) {
                    int *id = malloc(sizeof(int));
                    *id = i + 1;
                    if (pthread_create(&threads[thread_count++], NULL, writer_thread, id) != 0) {
                        perror("Failed to create writer thread");
                        free(id);
                    } else {
                        printf("Writer thread %d created\n", *id);
                    }
                }
                
                printf("Simulation running in background. Press Enter to continue...\n");
                while (getchar() != '\n');  // Clear any existing input
                getchar();  // Wait for Enter
                
                break;
            }
            case 4: {
                // Start simulation with processes
                int num_readers, num_writers;
                
                printf("Enter number of reader processes (1-5): ");
                if (scanf("%d", &num_readers) != 1 || num_readers < 1 || num_readers > 5) {
                    printf("Invalid number. Using default of 2 readers.\n");
                    num_readers = 2;
                }
                
                printf("Enter number of writer processes (1-5): ");
                if (scanf("%d", &num_writers) != 1 || num_writers < 1 || num_writers > 5) {
                    printf("Invalid number. Using default of 2 writers.\n");
                    num_writers = 2;
                }
                
                printf("Starting simulation with %d reader processes and %d writer processes...\n", 
                       num_readers, num_writers);
                
                // Create reader processes
                for (int i = 0; i < num_readers; i++) {
                    pid_t pid = fork();
                    
                    if (pid == -1) {
                        perror("Fork failed");
                    } else if (pid == 0) {
                        // Child process
                        reader_process(i + 1);
                        // Should not return
                        exit(EXIT_SUCCESS);
                    } else {
                        // Parent process
                        child_pids[process_count++] = pid;
                        printf("Reader process %d created (PID: %d)\n", i + 1, pid);
                    }
                }
                
                // Create writer processes
                for (int i = 0; i < num_writers; i++) {
                    pid_t pid = fork();
                    
                    if (pid == -1) {
                        perror("Fork failed");
                    } else if (pid == 0) {
                        // Child process
                        writer_process(i + 1);
                        // Should not return
                        exit(EXIT_SUCCESS);
                    } else {
                        // Parent process
                        child_pids[process_count++] = pid;
                        printf("Writer process %d created (PID: %d)\n", i + 1, pid);
                    }
                }
                
                printf("Processes running. Waiting for them to complete...\n");
                
                // Wait for all child processes to complete
                for (int i = 0; i < process_count; i++) {
                    waitpid(child_pids[i], NULL, 0);
                    printf("Process with PID %d has completed\n", child_pids[i]);
                }
                
                process_count = 0;  // Reset process count
                
                break;
            }
            case 5: {
                // Exit the program
                printf("Exiting program...\n");
                
                // Set running flag to false to stop threads
                running = 0;
                
                // Wait for threads to finish
                for (int i = 0; i < thread_count; i++) {
                    pthread_join(threads[i], NULL);
                }
                
                // Clean up resources
                cleanup_system();
                return EXIT_SUCCESS;
            }
            default:
                printf("Invalid choice. Please try again.\n");
        }
    }
    
    return EXIT_SUCCESS;
}