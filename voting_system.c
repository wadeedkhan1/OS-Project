/**
 * Synchronized Voting System - Implementation of Readers-Writers Problem
 * 
 * This program implements a voting system with three modes:
 * 1. Manual Mode: Interactive CLI for voting/viewing results
 * 2. Thread Mode: Simulates concurrent voters/observers using threads
 * 3. Process Mode: Simulates voters/observers as separate processes
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <semaphore.h>
 #include <pthread.h>
 #include <signal.h>
 #include <time.h>
 #include <errno.h>
 #include <sys/wait.h>
 #include <stdbool.h>
 
 #define MAX_CANDIDATES 10
 #define MAX_VOTERS 1000
 #define MAX_OBSERVERS 20
 #define MAX_NAME_LENGTH 50
 #define SHM_NAME "/voting_system_shm"
 #define SEM_MUTEX "/voting_mutex"
 #define SEM_WRIT "/voting_write"
 #define SEM_READ_COUNT "/voting_read_count"
 #define LOG_FILE "vote_log.txt"
 
 // Shared memory structure
 typedef struct {
     int candidate_count;
     char candidate_names[MAX_CANDIDATES][MAX_NAME_LENGTH];
     int votes[MAX_CANDIDATES];
     int total_votes;
     int voted_ids[MAX_VOTERS];  // Store IDs of voters who have already voted
     int voted_count;
     int reader_count;  // Added to shared memory instead of global
 } VotingData;
 
 // Global variables
 sem_t *mutex = NULL;        // Controls access to read_count
 sem_t *wrt = NULL;          // Controls write access
 sem_t *read_count_sem = NULL; // Controls access to reader count
 VotingData *voting_data = NULL;
 int shm_fd = -1;
 FILE *log_file = NULL;
 
 // Function prototypes
 void initialize_resources();
 void cleanup_resources();
 void handle_signal(int sig);
 void reader_enter();
 void reader_exit();
 void writer_enter();
 void writer_exit();
 void cast_vote(int voter_id, int candidate_id);
 void view_results();
 void manual_mode();
 void thread_mode();
 void process_mode();
 void *voter_thread(void *arg);
 void *observer_thread(void *arg);
 void run_voter_process(int voter_id);
 void run_observer_process();
 void print_performance_comparison();
 
 // Initialize semaphores and shared memory
 void initialize_resources() {
     // Clean up any existing resources first
     sem_unlink(SEM_MUTEX);
     sem_unlink(SEM_WRIT);
     sem_unlink(SEM_READ_COUNT);
     shm_unlink(SHM_NAME);
     
     // Create and initialize semaphores
     mutex = sem_open(SEM_MUTEX, O_CREAT, 0644, 1);
     wrt = sem_open(SEM_WRIT, O_CREAT, 0644, 1);
     read_count_sem = sem_open(SEM_READ_COUNT, O_CREAT, 0644, 1);
     
     if (mutex == SEM_FAILED || wrt == SEM_FAILED || read_count_sem == SEM_FAILED) {
         perror("Semaphore initialization failed");
         exit(EXIT_FAILURE);
     }
     
     // Create and map shared memory
     shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0644);
     if (shm_fd == -1) {
         perror("Shared memory creation failed");
         exit(EXIT_FAILURE);
     }
 
     if (ftruncate(shm_fd, sizeof(VotingData)) == -1) {
         perror("Shared memory sizing failed");
         exit(EXIT_FAILURE);
     }
 
     voting_data = (VotingData *)mmap(NULL, sizeof(VotingData), 
                                      PROT_READ | PROT_WRITE, 
                                      MAP_SHARED, shm_fd, 0);
     
     if (voting_data == MAP_FAILED) {
         perror("Memory mapping failed");
         exit(EXIT_FAILURE);
     }
 
     // Initialize voting data
     memset(voting_data, 0, sizeof(VotingData));
 
     // Open log file
     log_file = fopen(LOG_FILE, "a");
     if (log_file == NULL) {
         perror("Failed to open log file");
         exit(EXIT_FAILURE);
     }
 
     // Set up signal handler
     signal(SIGINT, handle_signal);
 }
 
 // Clean up resources
 void cleanup_resources() {
     // Close and unlink semaphores
         sem_close(mutex);
     sem_close(wrt);
     sem_close(read_count_sem);
         sem_unlink(SEM_MUTEX);
     sem_unlink(SEM_WRIT);
     sem_unlink(SEM_READ_COUNT);
     
     // Clean up shared memory
     if (voting_data != NULL) {
         munmap(voting_data, sizeof(VotingData));
     }
     
     if (shm_fd != -1) {
         close(shm_fd);
         shm_unlink(SHM_NAME);
     }
     
     // Close log file
     if (log_file != NULL) {
         fclose(log_file);
     }
     
     printf("\nResources cleaned up successfully\n");
 }
 
 // Signal handler for SIGINT (Ctrl+C)
 void handle_signal(int sig) {
     static int cleanup_in_progress = 0;
     
     // Prevent multiple cleanup operations
     if (cleanup_in_progress) {
         return;
     }
     
     cleanup_in_progress = 1;
     printf("\nSignal received: Cleaning up...\n");
     cleanup_resources();
     printf("Thank you for using the Voting System.\n");
     exit(EXIT_SUCCESS);
 }
 
 // Reader entry protocol
 void reader_enter() {
     sem_wait(read_count_sem);
     if (++voting_data->reader_count == 1) {
         sem_wait(wrt);  // First reader blocks writers
     }
     sem_post(read_count_sem);
 }
 
 // Reader exit protocol
 void reader_exit() {
     sem_wait(read_count_sem);
     if (--voting_data->reader_count == 0) {
         sem_post(wrt);  // Last reader releases writers
     }
     sem_post(read_count_sem);
 }
 
 // Writer entry protocol
 void writer_enter() {
     sem_wait(wrt);
 }
 
 // Writer exit protocol
 void writer_exit() {
         sem_post(wrt);
     }
 
 // Cast a vote
 void cast_vote(int voter_id, int candidate_id) {
     writer_enter();
     
     // Check if voter has already voted
     for (int i = 0; i < voting_data->voted_count; i++) {
         if (voting_data->voted_ids[i] == voter_id) {
             printf("❌ Voter ID %d has already voted!\n", voter_id);
             writer_exit();
             return;
         }
     }
     
     // Check if candidate exists
     if (candidate_id < 0 || candidate_id >= voting_data->candidate_count) {
         printf("❌ Invalid candidate ID!\n");
         writer_exit();
         return;
     }
     
     // Cast vote
     voting_data->votes[candidate_id]++;
     voting_data->total_votes++;
     
     // Record voter ID
     if (voting_data->voted_count < MAX_VOTERS) {
         voting_data->voted_ids[voting_data->voted_count++] = voter_id;
     }
     
     printf("🗳️ Voter %d successfully voted for %s\n", 
            voter_id, voting_data->candidate_names[candidate_id]);
     
     // Log the vote
     if (log_file != NULL) {
         time_t now;
         struct tm *timeinfo;
         char timestamp[30];
         
         time(&now);
         timeinfo = localtime(&now);
         strftime(timestamp, sizeof(timestamp), "[%d-%m-%Y %H:%M:%S]", timeinfo);
         
         fprintf(log_file, "[%s] VoterID: %d voted for %s\n", 
             timestamp, voter_id, voting_data->candidate_names[candidate_id]);
     fflush(log_file);
     }
 
     writer_exit();
 }
 
 // View voting results
 void view_results() {
     reader_enter();
     
     printf("\n📊 === Current Vote Count ===\n");
     printf("Total votes: %d\n", voting_data->total_votes);
     
     for (int i = 0; i < voting_data->candidate_count; i++) {
         float percentage = voting_data->total_votes > 0 ? 
             (float)voting_data->votes[i] / voting_data->total_votes * 100 : 0;
         
         printf("• %s: %d votes (%.1f%%)\n", 
                voting_data->candidate_names[i], 
                voting_data->votes[i],
                percentage);
     }
     printf("===========================\n");
     
     reader_exit();
 }
 
 // Manual mode - interactive CLI
 void manual_mode() {
     int choice, voter_id, candidate_id;
     char candidate_name[MAX_NAME_LENGTH];
     char input_buffer[256];  // Buffer for input validation
     
     // Initialize candidates
     printf("\n👥 Setup Candidates\n");
     printf("Enter the number of candidates (max %d): ", MAX_CANDIDATES);
     
     // Validate candidate count input
     if (scanf("%d", &voting_data->candidate_count) != 1) {
         printf("⚠️ Invalid input. Using default (3 candidates).\n");
         // Clear input buffer
         while (getchar() != '\n');
         voting_data->candidate_count = 3;
         strcpy(voting_data->candidate_names[0], "Candidate A");
         strcpy(voting_data->candidate_names[1], "Candidate B");
         strcpy(voting_data->candidate_names[2], "Candidate C");
     } else {
         // Validate range
         if (voting_data->candidate_count <= 0 || voting_data->candidate_count > MAX_CANDIDATES) {
             printf("⚠️ Invalid number. Using default (3 candidates).\n");
             voting_data->candidate_count = 3;
             strcpy(voting_data->candidate_names[0], "Candidate A");
             strcpy(voting_data->candidate_names[1], "Candidate B");
             strcpy(voting_data->candidate_names[2], "Candidate C");
         } else {
             // Clear input buffer
             while (getchar() != '\n');
             
             // Get candidate names
             for (int i = 0; i < voting_data->candidate_count; i++) {
                 printf("Enter name for candidate %d: ", i);
                 if (scanf("%49s", voting_data->candidate_names[i]) != 1) {
                     printf("⚠️ Invalid input. Using default name.\n");
                     sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
                     while (getchar() != '\n');
                 }
             }
         }
     }
     
     // Main menu loop
     while (1) {
         printf("\n📋 === Voting Menu ===\n");
         printf("1. Cast Vote\n");
         printf("2. View Results\n");
         printf("3. Exit\n");
         printf("Choice: ");
         
         // Validate menu choice input
         if (scanf("%d", &choice) != 1) {
             printf("❌ Invalid choice. Try again.\n");
             while (getchar() != '\n');
             continue;
         }
         
         // Clear input buffer
         while (getchar() != '\n');
         
         switch (choice) {
             case 1:
                 printf("Enter your voter ID: ");
                 if (scanf("%d", &voter_id) != 1) {
                     printf("❌ Invalid voter ID. Try again.\n");
                     while (getchar() != '\n');
                     continue;
                 }
                 
                 // Clear input buffer
                 while (getchar() != '\n');
                 
                 printf("Available candidates:\n");
                 for (int i = 0; i < voting_data->candidate_count; i++) {
                     printf("%d. %s\n", i, voting_data->candidate_names[i]);
                 }
                 printf("Enter candidate ID: ");
                 
                 if (scanf("%d", &candidate_id) != 1) {
                     printf("❌ Invalid candidate ID. Try again.\n");
                     while (getchar() != '\n');
                     continue;
                 }
                 
                 // Clear input buffer
                 while (getchar() != '\n');
                 
                 // Additional validation of candidate ID range
                 if (candidate_id < 0 || candidate_id >= voting_data->candidate_count) {
                     printf("❌ Invalid candidate ID: %d. Valid range is 0-%d.\n", 
                            candidate_id, voting_data->candidate_count - 1);
                     continue;
                 }
                 
                 cast_vote(voter_id, candidate_id);
                 break;
                 
             case 2:
                 view_results();
                 break;
                 
             case 3:
                 return;
                 
             default:
                 printf("❌ Invalid choice. Try again.\n");
         }
     }
 }
 
 // Thread function for voters
 void *voter_thread(void *arg) {
     int voter_id = *((int *)arg);
     int sleep_time = rand() % 3 + 1;
     int candidate_id = rand() % voting_data->candidate_count;
     
     // Free memory for voter_id
     free(arg);
     
     sleep(sleep_time);
     printf("[Thread %d] Voter %d voting for %s\n", (int)pthread_self(), 
            voter_id, voting_data->candidate_names[candidate_id]);
     cast_vote(voter_id, candidate_id);
     
     return NULL;
 }
 
 // Observer thread function - ensure it can terminate properly
 void *observer_thread(void *arg) {
     int observer_id = *((int *)arg);
     int sleep_time;
     int iterations = 0;
     
     // Free the memory allocated for observer_id
     free(arg);
     
     // Set a maximum number of observations to avoid infinite loops
     while (iterations < 10) {
         sleep_time = rand() % 3 + 1;
         sleep(sleep_time);
         
         printf("[Thread %d] Reading current vote count...\n", (int)pthread_self());
         view_results();
         
         iterations++;
     }
     
     printf("[Thread %d] Observer %d finished observing.\n", (int)pthread_self(), observer_id);
     return NULL;
 }
 
 // Thread mode - fix deadlock and resource management
 void thread_mode() {
     int num_voters, num_observers;
     pthread_t *voter_threads;
     pthread_t *observer_threads;
     int *voter_ids;
     int *observer_ids;
     clock_t start, end;
     
     // Initialize candidates
     printf("\n👥 Setup Candidates\n");
     printf("Enter the number of candidates (max %d): ", MAX_CANDIDATES);
     scanf("%d", &voting_data->candidate_count);
     if (voting_data->candidate_count <= 0 || voting_data->candidate_count > MAX_CANDIDATES) {
         printf("⚠️ Invalid number. Using default (3 candidates).\n");
         voting_data->candidate_count = 3;
         strcpy(voting_data->candidate_names[0], "Candidate A");
         strcpy(voting_data->candidate_names[1], "Candidate B");
         strcpy(voting_data->candidate_names[2], "Candidate C");
     } else {
         for (int i = 0; i < voting_data->candidate_count; i++) {
             printf("Enter name for candidate %d: ", i);
             scanf("%s", voting_data->candidate_names[i]);
         }
     }
     
     printf("Enter number of voters to simulate (max %d): ", MAX_VOTERS);
     scanf("%d", &num_voters);
     if (num_voters <= 0 || num_voters > MAX_VOTERS) {
         printf("⚠️ Invalid number. Using default (10 voters).\n");
         num_voters = 10;
     }
     
     printf("Enter number of observers to simulate (max %d): ", MAX_OBSERVERS);
     scanf("%d", &num_observers);
     if (num_observers <= 0 || num_observers > MAX_OBSERVERS) {
         printf("⚠️ Invalid number. Using default (3 observers).\n");
         num_observers = 3;
     }
     
     // Dynamically allocate arrays
     voter_threads = malloc(num_voters * sizeof(pthread_t));
     observer_threads = malloc(num_observers * sizeof(pthread_t));
     voter_ids = malloc(num_voters * sizeof(int));
     observer_ids = malloc(num_observers * sizeof(int));
     
     if (!voter_threads || !observer_threads || !voter_ids || !observer_ids) {
         perror("Memory allocation failed");
         exit(EXIT_FAILURE);
     }
     
     // Seed random number generator
     srand(time(NULL));
     
     start = clock();
     
     // Create voter threads
     for (int i = 0; i < num_voters; i++) {
         int *id = malloc(sizeof(int));
         if (!id) {
             perror("Memory allocation failed");
             exit(EXIT_FAILURE);
         }
         *id = i + 1;
         voter_ids[i] = *id;
         
         if (pthread_create(&voter_threads[i], NULL, voter_thread, id) != 0) {
             perror("Failed to create voter thread");
             free(id);
             exit(EXIT_FAILURE);
         }
     }
     
     // Create observer threads with limited lifetime
     for (int i = 0; i < num_observers; i++) {
         int *id = malloc(sizeof(int));
         if (!id) {
             perror("Memory allocation failed");
             exit(EXIT_FAILURE);
         }
         *id = i + 1;
         observer_ids[i] = *id;
         
         if (pthread_create(&observer_threads[i], NULL, observer_thread, id) != 0) {
             perror("Failed to create observer thread");
             free(id);
             exit(EXIT_FAILURE);
         }
     }
     
     // Join voter threads
     for (int i = 0; i < num_voters; i++) {
         pthread_join(voter_threads[i], NULL);
     }
     
     end = clock();
     double elapsed_time = ((double)(end - start)) / CLOCKS_PER_SEC;
     
     // Join observer threads (they now exit naturally)
     for (int i = 0; i < num_observers; i++) {
         pthread_join(observer_threads[i], NULL);
     }
     
     printf("\n⏱️ Thread mode completed in %.2f seconds\n", elapsed_time);
     printf("Performance data saved for comparison\n");
     
     FILE *perf_file = fopen("performance_data.txt", "a");
     if (perf_file) {
         fprintf(perf_file, "Thread mode: %d voters, %d observers, %.2f seconds\n", 
                 num_voters, num_observers, elapsed_time);
         fclose(perf_file);
     }
     
     // Display final results
     view_results();
     
     // Free allocated memory
     free(voter_threads);
     free(observer_threads);
     free(voter_ids);
     free(observer_ids);
 }
 
 // Run voter process
 void run_voter_process(int voter_id) {
     // Re-map the shared memory
     voting_data = (VotingData *)mmap(NULL, sizeof(VotingData), 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, shm_fd, 0);
     
     if (voting_data == MAP_FAILED) {
         perror("Memory mapping failed in voter process");
         exit(EXIT_FAILURE);
     }
     
     // Reopen semaphores
     mutex = sem_open(SEM_MUTEX, 0);
     wrt = sem_open(SEM_WRIT, 0);
     read_count_sem = sem_open(SEM_READ_COUNT, 0);
     
     if (mutex == SEM_FAILED || wrt == SEM_FAILED || read_count_sem == SEM_FAILED) {
         perror("Semaphore opening failed in voter process");
         exit(EXIT_FAILURE);
     }
     
     // Don't handle SIGINT in child processes - parent will manage cleanup
     signal(SIGINT, SIG_IGN);
     
     // Use a unique seed for randomization in each process
     srand(time(NULL) ^ getpid() ^ voter_id);
     
     // Sleep for a random time
     int sleep_time = rand() % 3 + 1;
     sleep(sleep_time);
     
     // Cast a vote for a random candidate
     int candidate_id = rand() % voting_data->candidate_count;
     printf("[Process %d] Voter %d voting for %s\n", getpid(), 
            voter_id, voting_data->candidate_names[candidate_id]);
     cast_vote(voter_id, candidate_id);
     
     // Clean up local resources only
     sem_close(mutex);
     sem_close(wrt);
     sem_close(read_count_sem);
     munmap(voting_data, sizeof(VotingData));
     
     exit(EXIT_SUCCESS);
 }
 
 // Run observer process - fix potential hanging
 void run_observer_process() {
     // Re-map the shared memory
     voting_data = (VotingData *)mmap(NULL, sizeof(VotingData), 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, shm_fd, 0);
     
     if (voting_data == MAP_FAILED) {
         perror("Memory mapping failed in observer process");
         exit(EXIT_FAILURE);
     }
     
     // Reopen semaphores
     mutex = sem_open(SEM_MUTEX, 0);
     wrt = sem_open(SEM_WRIT, 0);
     read_count_sem = sem_open(SEM_READ_COUNT, 0);
     
     if (mutex == SEM_FAILED || wrt == SEM_FAILED || read_count_sem == SEM_FAILED) {
         perror("Semaphore opening failed in observer process");
         exit(EXIT_FAILURE);
     }
     
     // Don't handle SIGINT in child processes - parent will manage cleanup
     signal(SIGINT, SIG_IGN);
     
     // Use a unique seed for randomization
     srand(time(NULL) ^ getpid());
     
     // Observe a fixed number of times rather than infinity
     for (int i = 0; i < 5; i++) {
         int sleep_time = rand() % 2 + 1;
         sleep(sleep_time);
         printf("[Process %d] Reading votes...\n", getpid());
         view_results();
     }
     
     // Clean up local resources only
     sem_close(mutex);
     sem_close(wrt);
     sem_close(read_count_sem);
     munmap(voting_data, sizeof(VotingData));
     
     printf("[Process %d] Observer finished observing.\n", getpid());
     exit(EXIT_SUCCESS);
 }
 
 // Process mode - fix potential deadlock issues
 void process_mode() {
     int num_voters, num_observers;
     pid_t *voter_pids;
     pid_t *observer_pids;
     clock_t start, end;
     
     // Initialize candidates
     printf("\n👥 Setup Candidates\n");
     printf("Enter the number of candidates (max %d): ", MAX_CANDIDATES);
     scanf("%d", &voting_data->candidate_count);
     if (voting_data->candidate_count <= 0 || voting_data->candidate_count > MAX_CANDIDATES) {
         printf("⚠️ Invalid number. Using default (3 candidates).\n");
         voting_data->candidate_count = 3;
         strcpy(voting_data->candidate_names[0], "Candidate A");
         strcpy(voting_data->candidate_names[1], "Candidate B");
         strcpy(voting_data->candidate_names[2], "Candidate C");
     } else {
         for (int i = 0; i < voting_data->candidate_count; i++) {
             printf("Enter name for candidate %d: ", i);
             scanf("%s", voting_data->candidate_names[i]);
         }
     }
     
     printf("Enter number of voters to simulate (max %d): ", MAX_VOTERS);
     scanf("%d", &num_voters);
     if (num_voters <= 0 || num_voters > MAX_VOTERS) {
         printf("⚠️ Invalid number. Using default (10 voters).\n");
         num_voters = 10;
     }
     
     printf("Enter number of observers to simulate (max %d): ", MAX_OBSERVERS);
     scanf("%d", &num_observers);
     if (num_observers <= 0 || num_observers > MAX_OBSERVERS) {
         printf("⚠️ Invalid number. Using default (3 observers).\n");
         num_observers = 3;
     }
     
     // Dynamically allocate arrays
     voter_pids = malloc(num_voters * sizeof(pid_t));
     observer_pids = malloc(num_observers * sizeof(pid_t));
     
     if (!voter_pids || !observer_pids) {
         perror("Memory allocation failed");
         exit(EXIT_FAILURE);
     }
     
     // Seed random number generator for parent process only
     // Each child will have its own seed
     srand(time(NULL));
     
     start = clock();
     
     // Create voter processes
     for (int i = 0; i < num_voters; i++) {
         pid_t pid = fork();
         
         if (pid < 0) {
             perror("Fork failed");
             exit(EXIT_FAILURE);
         } else if (pid == 0) {
             // Child process (voter) will set its own random seed
             run_voter_process(i + 1);
             // Should not reach here
             exit(EXIT_SUCCESS);
         } else {
             // Parent process
             voter_pids[i] = pid;
         }
     }
     
     // Create observer processes
     for (int i = 0; i < num_observers; i++) {
         pid_t pid = fork();
         
         if (pid < 0) {
             perror("Fork failed");
             exit(EXIT_FAILURE);
         } else if (pid == 0) {
             // Child process (observer) will set its own random seed
             run_observer_process();
             // Should not reach here
             exit(EXIT_SUCCESS);
         } else {
             // Parent process
             observer_pids[i] = pid;
         }
     }
     
     // Wait for voter processes to complete with timeout
     printf("Waiting for voters to complete...\n");
     for (int i = 0; i < num_voters; i++) {
         int status;
         pid_t result = waitpid(voter_pids[i], &status, 0);
         if (result == -1) {
             perror("Error waiting for voter process");
         }
     }
     
     // Wait for observer processes to complete naturally
     printf("All voters completed. Waiting for observers to finish their work...\n");
     for (int i = 0; i < num_observers; i++) {
         int status;
         pid_t result = waitpid(observer_pids[i], &status, 0);
         if (result == -1) {
             perror("Error waiting for observer process");
         }
     }
     
     end = clock();
     double elapsed_time = ((double)(end - start)) / CLOCKS_PER_SEC;
     
     printf("\n⏱️ Process mode completed in %.2f seconds\n", elapsed_time);
     printf("Performance data saved for comparison\n");
     
     FILE *perf_file = fopen("performance_data.txt", "a");
     if (perf_file) {
         fprintf(perf_file, "Process mode: %d voters, %d observers, %.2f seconds\n", 
                 num_voters, num_observers, elapsed_time);
         fclose(perf_file);
     }
     
     // Display final results
     view_results();
     
     // Free allocated memory
     free(voter_pids);
     free(observer_pids);
 }
 
 // Print comparison of thread vs process performance
 void print_performance_comparison() {
     FILE *perf_file = fopen("performance_data.txt", "r");
     if (!perf_file) {
         printf("No performance data available yet.\n");
         return;
     }
     
     double thread_time = 0.0, process_time = 0.0;
     int thread_voters = 0, thread_observers = 0;
     int process_voters = 0, process_observers = 0;
     char line[256];
     
     while (fgets(line, sizeof(line), perf_file)) {
         if (strstr(line, "Thread mode:")) {
             sscanf(line, "Thread mode: %d voters, %d observers, %lf seconds", 
                   &thread_voters, &thread_observers, &thread_time);
         } else if (strstr(line, "Process mode:")) {
             sscanf(line, "Process mode: %d voters, %d observers, %lf seconds", 
                   &process_voters, &process_observers, &process_time);
         }
     }
     
     fclose(perf_file);
     
     printf("\n== Comparing Threads and Processes ==\n");
     printf("[Threads] Execution Time: %.3fs\n", thread_time);
     printf("[Processes] Execution Time: %.3fs\n", process_time);
     
     if (thread_time > 0 && process_time > 0) {
         if (thread_time < process_time) {
             printf("Threads were faster by %.3fs (%.1f%%)\n", 
                    process_time - thread_time, 
                    (process_time - thread_time) / process_time * 100);
         } else if (process_time < thread_time) {
             printf("Processes were faster by %.3fs (%.1f%%)\n", 
                    thread_time - process_time, 
                    (thread_time - process_time) / thread_time * 100);
         } else {
             printf("Both methods had identical performance\n");
         }
     }
     
     printf("\nFinal Vote Tally:\n");
     for (int i = 0; i < voting_data->candidate_count; i++) {
         printf("%s: %d", voting_data->candidate_names[i], voting_data->votes[i]);
         if (i < voting_data->candidate_count - 1) {
             printf(" | ");
         }
     }
     printf("\n");
 }
 
 int main(int argc, char *argv[]) {
     int mode;
     
     // Initialize resources
     initialize_resources();
     
     printf("===== SYNCHRONIZED VOTING SYSTEM =====\n");
     printf("1. Manual Mode\n");
     printf("2. Thread Mode\n");
     printf("3. Process Mode\n");
     printf("4. View Performance Comparison\n");
     printf("Enter mode: ");
     scanf("%d", &mode);
     
     switch (mode) {
         case 1:
             manual_mode();
             break;
         case 2:
             thread_mode();
             break;
         case 3:
             process_mode();
             break;
         case 4:
             print_performance_comparison();
             break;
         default:
             printf("Invalid mode. Exiting.\n");
     }
     
     // Clean up resources
     cleanup_resources();
     
     return 0;
 }
