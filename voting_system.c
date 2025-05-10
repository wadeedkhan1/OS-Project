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
 #include <math.h>
 
 #define MAX_CANDIDATES 10
 #define MAX_VOTERS 1000
 #define MAX_OBSERVERS 20
 #define MAX_NAME_LENGTH 50
 #define SHM_NAME "/voting_system_shm"
 #define SEM_MUTEX "/voting_mutex"
 #define SEM_WRIT "/voting_write"
 #define SEM_READ_COUNT "/voting_read_count"
 #define SEM_CONSOLE "/voting_console"
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
 sem_t *console_sem = NULL;  // Controls console output to prevent interleaving
 VotingData *voting_data = NULL;
 int shm_fd = -1;
 FILE *log_file = NULL;
 char log_filename[100];    // To store the unique log filename
 
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
 void create_log_file(const char *mode);
 void initialize_performance_file();
 
 // Function to create a timestamped log filename and open the log file
 void create_log_file(const char *mode) {
     time_t now;
     struct tm *timeinfo;
     char timestamp[30];
     
     time(&now);
    strftime(timestamp, sizeof(timestamp), "%d-%m-%Y_%H-%M-%S", localtime(&now));
     
     // Create a unique filename with timestamp in square brackets and mode
     snprintf(log_filename, sizeof(log_filename), "vote_log_[%s]_%s.txt", timestamp, mode);

     
     // Open log file
     log_file = fopen(log_filename, "w");
     if (log_file == NULL) {
         perror("Failed to open log file");
         exit(EXIT_FAILURE);
     }
     
     // Write header information
     fprintf(log_file, "=================================================\n");
     fprintf(log_file, "VOTING SESSION LOG - %s MODE\n", mode);
     fprintf(log_file, "=================================================\n");
     fprintf(log_file, "Session started at: %s\n", ctime(&now));
     fprintf(log_file, "System information: %s\n", "Synchronized Voting System");
     fprintf(log_file, "-------------------------------------------------\n\n");
     fprintf(log_file, "VOTING RECORD:\n\n");
     fflush(log_file);
 }
 
 // Function to initialize performance data file if it doesn't exist
 void initialize_performance_file() {
     FILE *perf_file = fopen("performance_data.txt", "r");
     if (perf_file) {
         // File already exists, no need to initialize
         fclose(perf_file);
         return;
     }
     
     // Create new performance data file with header
     perf_file = fopen("performance_data.txt", "w");
     if (perf_file == NULL) {
         perror("Failed to create performance data file");
         return;
     }
     
     time_t now;
     time(&now);
     
     fprintf(perf_file, "=================================================\n");
     fprintf(perf_file, "VOTING SYSTEM PERFORMANCE DATA\n");
     fprintf(perf_file, "=================================================\n");
     fprintf(perf_file, "File created: %s", ctime(&now));
     fprintf(perf_file, "\n");
     fprintf(perf_file, "Format: [Timestamp] Mode: voters, observers, seconds, sec/voter, sec/observer\n");
     fprintf(perf_file, "-------------------------------------------------\n\n");
     fclose(perf_file);
 }
 
 // Initialize semaphores and shared memory
 void initialize_resources() {
     // Clean up any existing resources first
     sem_unlink(SEM_MUTEX);
     sem_unlink(SEM_WRIT);
     sem_unlink(SEM_READ_COUNT);
     sem_unlink(SEM_CONSOLE);
     shm_unlink(SHM_NAME);
     
     // Create and initialize semaphores
     mutex = sem_open(SEM_MUTEX, O_CREAT, 0644, 1);
     wrt = sem_open(SEM_WRIT, O_CREAT, 0644, 1);
     read_count_sem = sem_open(SEM_READ_COUNT, O_CREAT, 0644, 1);
     console_sem = sem_open(SEM_CONSOLE, O_CREAT, 0644, 1);
     
     if (mutex == SEM_FAILED || wrt == SEM_FAILED || read_count_sem == SEM_FAILED || console_sem == SEM_FAILED) {
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
 
     // Set up signal handler
     signal(SIGINT, handle_signal);
 }
 
 // Clean up resources
 void cleanup_resources() {
     // Write summary to log file if it exists
     if (log_file != NULL) {
         time_t now;
         time(&now);
         
         fprintf(log_file, "\n-------------------------------------------------\n");
         fprintf(log_file, "VOTING SESSION SUMMARY\n");
         fprintf(log_file, "-------------------------------------------------\n");
         fprintf(log_file, "Session ended at: %s", ctime(&now));
         fprintf(log_file, "Total votes cast: %d\n\n", voting_data->total_votes);
         
         fprintf(log_file, "FINAL RESULTS:\n");
         for (int i = 0; i < voting_data->candidate_count; i++) {
             float percentage = voting_data->total_votes > 0 ? 
                 (float)voting_data->votes[i] / voting_data->total_votes * 100 : 0;
             
             fprintf(log_file, "• %s: %d votes (%.1f%%)\n", 
                    voting_data->candidate_names[i], 
                    voting_data->votes[i],
                    percentage);
         }
         
         fprintf(log_file, "\n=================================================\n");
         fprintf(log_file, "END OF VOTING SESSION LOG\n");
         fprintf(log_file, "=================================================\n");
         
         // Close log file
         fclose(log_file);
         log_file = NULL;
         
         printf("Voting log saved to: %s\n", log_filename);
     }

     // Close and unlink semaphores
     sem_close(mutex);
     sem_close(wrt);
     sem_close(read_count_sem);
     sem_close(console_sem);
     sem_unlink(SEM_MUTEX);
     sem_unlink(SEM_WRIT);
     sem_unlink(SEM_READ_COUNT);
     sem_unlink(SEM_CONSOLE);
     
     // Clean up shared memory
     if (voting_data != NULL) {
         munmap(voting_data, sizeof(VotingData));
     }
     
     if (shm_fd != -1) {
         close(shm_fd);
         shm_unlink(SHM_NAME);
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
             // Use console semaphore to prevent interleaved output
             sem_wait(console_sem);
             printf("❌ Voter ID %d has already voted!\n", voter_id);
             sem_post(console_sem);
             
             // Log failed vote attempt
             if (log_file != NULL) {
                 time_t now;
                 struct tm *timeinfo;
                 char timestamp[30];
                 
                 time(&now);
                 timeinfo = localtime(&now);
                 strftime(timestamp, sizeof(timestamp), "[%d-%m-%Y %H:%M:%S]", timeinfo);
                 
                 fprintf(log_file, "%s FAILED VOTE: VoterID %d attempted to vote again\n", 
                     timestamp, voter_id);
                 fflush(log_file);
             }
             
             writer_exit();
             return;
         }
     }
     
     // Check if candidate exists
     if (candidate_id < 0 || candidate_id >= voting_data->candidate_count) {
         // Use console semaphore to prevent interleaved output
         sem_wait(console_sem);
         printf("❌ Invalid candidate ID!\n");
         sem_post(console_sem);
         
         // Log invalid candidate attempt
         if (log_file != NULL) {
             time_t now;
             struct tm *timeinfo;
             char timestamp[30];
             
             time(&now);
             timeinfo = localtime(&now);
             strftime(timestamp, sizeof(timestamp), "[%d-%m-%Y %H:%M:%S]", timeinfo);
             
             fprintf(log_file, "%s INVALID VOTE: VoterID %d attempted to vote for invalid candidate ID %d\n", 
                 timestamp, voter_id, candidate_id);
             fflush(log_file);
         }
         
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
     
     // Use console semaphore to prevent interleaved output
     sem_wait(console_sem);
     printf("🗳️ Voter %d successfully voted for %s\n", 
            voter_id, voting_data->candidate_names[candidate_id]);
     sem_post(console_sem);
     
     // Log the vote with more details
     if (log_file != NULL) {
         time_t now;
         struct tm *timeinfo;
         char timestamp[30];
         
         time(&now);
         timeinfo = localtime(&now);
         strftime(timestamp, sizeof(timestamp), "[%d-%m-%Y %H:%M:%S]", timeinfo);
         
         fprintf(log_file, "%s SUCCESS: VoterID %d voted for Candidate '%s' (ID: %d)\n", 
             timestamp, voter_id, voting_data->candidate_names[candidate_id], candidate_id);
         fflush(log_file);
     }

     writer_exit();
 }
 
 // View voting results
 void view_results() {
     reader_enter();
     
     // Acquire the console semaphore to prevent interleaved output
     sem_wait(console_sem);
     
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
     
     // Release the console semaphore
     sem_post(console_sem);
     
     reader_exit();
 }
 
 // Manual mode - interactive CLI
 void manual_mode() {
     // Create a log file for this session
     create_log_file("Manual");
     
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
                 if (fgets(input_buffer, sizeof(input_buffer), stdin) != NULL) {
                     // Remove trailing newline if present
                     size_t len = strlen(input_buffer);
                     if (len > 0 && input_buffer[len - 1] == '\n') {
                         input_buffer[len - 1] = '\0';
                     }
                     
                     // Check if input is empty
                     if (strlen(input_buffer) == 0) {
                         printf("⚠️ Empty name. Using default name.\n");
                         sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
                     } else {
                         // Copy with length limit to prevent buffer overflow
                         strncpy(voting_data->candidate_names[i], input_buffer, MAX_NAME_LENGTH-1);
                         voting_data->candidate_names[i][MAX_NAME_LENGTH-1] = '\0'; // Ensure null termination
                     }
                 } else {
                     printf("⚠️ Input error. Using default name.\n");
                     sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
                 }
             }
         }
     }
     
     // Log the candidates setup
     fprintf(log_file, "CANDIDATE SETUP:\n");
     for (int i = 0; i < voting_data->candidate_count; i++) {
         fprintf(log_file, "Candidate %d: %s\n", i, voting_data->candidate_names[i]);
     }
     fprintf(log_file, "\n");
     fprintf(log_file, "MODE DETAILS: Manual interactive mode\n");
     fprintf(log_file, "INTERACTION: User-driven via CLI\n\n");
     fprintf(log_file, "-------------------------------------------------\n\n");
     fflush(log_file);
     
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
     
     // Use console semaphore to prevent interleaved output
     sem_wait(console_sem);
     printf("[Thread %d] Voter %d voting for %s\n", (int)pthread_self(), 
            voter_id, voting_data->candidate_names[candidate_id]);
     sem_post(console_sem);
     
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
         
         // Use console semaphore to prevent interleaved output
         sem_wait(console_sem);
         printf("[Thread %d] Reading current vote count...\n", (int)pthread_self());
         sem_post(console_sem);
         
         view_results();
         
         iterations++;
     }
     
     // Use console semaphore to prevent interleaved output
     sem_wait(console_sem);
     printf("[Thread %d] Observer %d finished observing.\n", (int)pthread_self(), observer_id);
     sem_post(console_sem);
     
     return NULL;
 }
 
 // Thread mode - fix deadlock and resource management
 void thread_mode() {
     // Create a log file for this session
     create_log_file("Thread");
     
     int num_voters, num_observers;
     pthread_t *voter_threads;
     pthread_t *observer_threads;
     int *voter_ids;
     int *observer_ids;
     clock_t start, end;
     char input_buffer[256];  // Buffer for input
     
     // Initialize candidates
     printf("\n👥 Setup Candidates\n");
     printf("Enter the number of candidates (max %d): ", MAX_CANDIDATES);
     scanf("%d", &voting_data->candidate_count);
     
     // Clear input buffer
     while (getchar() != '\n');
     
     if (voting_data->candidate_count <= 0 || voting_data->candidate_count > MAX_CANDIDATES) {
         printf("⚠️ Invalid number. Using default (3 candidates).\n");
         voting_data->candidate_count = 3;
         strcpy(voting_data->candidate_names[0], "Candidate A");
         strcpy(voting_data->candidate_names[1], "Candidate B");
         strcpy(voting_data->candidate_names[2], "Candidate C");
     } else {
         for (int i = 0; i < voting_data->candidate_count; i++) {
             printf("Enter name for candidate %d: ", i);
             if (fgets(input_buffer, sizeof(input_buffer), stdin) != NULL) {
                 // Remove trailing newline if present
                 size_t len = strlen(input_buffer);
                 if (len > 0 && input_buffer[len - 1] == '\n') {
                     input_buffer[len - 1] = '\0';
                 }
                 
                 // Check if input is empty
                 if (strlen(input_buffer) == 0) {
                     printf("⚠️ Empty name. Using default name.\n");
                     sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
                 } else {
                     // Copy with length limit to prevent buffer overflow
                     strncpy(voting_data->candidate_names[i], input_buffer, MAX_NAME_LENGTH-1);
                     voting_data->candidate_names[i][MAX_NAME_LENGTH-1] = '\0'; // Ensure null termination
                 }
             } else {
                 printf("⚠️ Input error. Using default name.\n");
                 sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
             }
         }
     }
     
     printf("Enter number of voters to simulate (max %d): ", MAX_VOTERS);
     if (scanf("%d", &num_voters) != 1) {
         printf("⚠️ Invalid number. Using default (10 voters).\n");
         num_voters = 10;
         // Clear input buffer
         while (getchar() != '\n');
     } else {
         // Check range and clear input buffer
         if (num_voters <= 0 || num_voters > MAX_VOTERS) {
             printf("⚠️ Invalid number. Using default (10 voters).\n");
             num_voters = 10;
         }
         // Clear input buffer
         while (getchar() != '\n');
     }
     
     printf("Enter number of observers to simulate (max %d): ", MAX_OBSERVERS);
     if (scanf("%d", &num_observers) != 1) {
         printf("⚠️ Invalid number. Using default (3 observers).\n");
         num_observers = 3;
         // Clear input buffer
         while (getchar() != '\n');
     } else {
         // Check range and clear input buffer
         if (num_observers <= 0 || num_observers > MAX_OBSERVERS) {
             printf("⚠️ Invalid number. Using default (3 observers).\n");
             num_observers = 3;
         }
         // Clear input buffer
         while (getchar() != '\n');
     }
     
     // After initializing candidates and getting voter/observer counts
     // Log the thread mode setup details
     fprintf(log_file, "CANDIDATE SETUP:\n");
     for (int i = 0; i < voting_data->candidate_count; i++) {
         fprintf(log_file, "Candidate %d: %s\n", i, voting_data->candidate_names[i]);
     }
     fprintf(log_file, "\n");
     fprintf(log_file, "MODE DETAILS: Thread simulation mode\n");
     fprintf(log_file, "CONFIGURATION: %d voters, %d observers\n", num_voters, num_observers);
     fprintf(log_file, "IMPLEMENTATION: Using POSIX threads (pthread)\n\n");
     fprintf(log_file, "-------------------------------------------------\n\n");
     fflush(log_file);
     
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
     
     // Add timing information to log
     fprintf(log_file, "EXECUTION STATISTICS:\n");
     fprintf(log_file, "Total execution time: %.2f seconds\n\n", elapsed_time);
     fflush(log_file);
     
     printf("\n⏱️ Thread mode completed in %.2f seconds\n", elapsed_time);
     printf("Performance data saved for comparison\n");
     
     // Calculate per-voter and per-observer metrics
     double time_per_voter = num_voters > 0 ? elapsed_time / num_voters : 0;
     double time_per_observer = num_observers > 0 ? elapsed_time / num_observers : 0;
     
     // Get current timestamp
     time_t now;
     struct tm *timeinfo;
     char timestamp[30];
     
     time(&now);
     timeinfo = localtime(&now);
     strftime(timestamp, sizeof(timestamp), "[%d-%m-%Y_%H-%M-%S]", timeinfo);
     
     FILE *perf_file = fopen("performance_data.txt", "a");
     if (perf_file) {
         fprintf(perf_file, "%s Thread mode: %d voters, %d observers, %.6f seconds, %.6f sec/voter, %.6f sec/observer\n", 
                 timestamp, num_voters, num_observers, elapsed_time, time_per_voter, time_per_observer);
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
     console_sem = sem_open(SEM_CONSOLE, 0);
     
     if (mutex == SEM_FAILED || wrt == SEM_FAILED || read_count_sem == SEM_FAILED || console_sem == SEM_FAILED) {
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
     
     // Use console semaphore to prevent interleaved output
     sem_wait(console_sem);
     printf("[Process %d] Voter %d voting for %s\n", getpid(), 
            voter_id, voting_data->candidate_names[candidate_id]);
     sem_post(console_sem);
     
     cast_vote(voter_id, candidate_id);
     
     // Clean up local resources only
     sem_close(mutex);
     sem_close(wrt);
     sem_close(read_count_sem);
     sem_close(console_sem);
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
     console_sem = sem_open(SEM_CONSOLE, 0);
     
     if (mutex == SEM_FAILED || wrt == SEM_FAILED || read_count_sem == SEM_FAILED || console_sem == SEM_FAILED) {
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
         
         // Use console semaphore to prevent interleaved output
         sem_wait(console_sem);
         printf("[Process %d] Reading votes...\n", getpid());
         sem_post(console_sem);
         
         view_results();
     }
     
     // Clean up local resources only
     sem_close(mutex);
     sem_close(wrt);
     sem_close(read_count_sem);
     sem_close(console_sem);
     munmap(voting_data, sizeof(VotingData));
     
     // Use console semaphore for final message
     sem_wait(console_sem);
     printf("[Process %d] Observer finished observing.\n", getpid());
     sem_post(console_sem);
     
     exit(EXIT_SUCCESS);
 }
 
 // Process mode - fix potential deadlock issues
 void process_mode() {
     // Create a log file for this session
     create_log_file("Process");
     
     int num_voters, num_observers;
     pid_t *voter_pids;
     pid_t *observer_pids;
     clock_t start, end;
     char input_buffer[256];  // Buffer for input
     
     // Initialize candidates
     printf("\n👥 Setup Candidates\n");
     printf("Enter the number of candidates (max %d): ", MAX_CANDIDATES);
     scanf("%d", &voting_data->candidate_count);
     
     // Clear input buffer
     while (getchar() != '\n');
     
     if (voting_data->candidate_count <= 0 || voting_data->candidate_count > MAX_CANDIDATES) {
         printf("⚠️ Invalid number. Using default (3 candidates).\n");
         voting_data->candidate_count = 3;
         strcpy(voting_data->candidate_names[0], "Candidate A");
         strcpy(voting_data->candidate_names[1], "Candidate B");
         strcpy(voting_data->candidate_names[2], "Candidate C");
     } else {
         for (int i = 0; i < voting_data->candidate_count; i++) {
             printf("Enter name for candidate %d: ", i);
             if (fgets(input_buffer, sizeof(input_buffer), stdin) != NULL) {
                 // Remove trailing newline if present
                 size_t len = strlen(input_buffer);
                 if (len > 0 && input_buffer[len - 1] == '\n') {
                     input_buffer[len - 1] = '\0';
                 }
                 
                 // Check if input is empty
                 if (strlen(input_buffer) == 0) {
                     printf("⚠️ Empty name. Using default name.\n");
                     sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
                 } else {
                     // Copy with length limit to prevent buffer overflow
                     strncpy(voting_data->candidate_names[i], input_buffer, MAX_NAME_LENGTH-1);
                     voting_data->candidate_names[i][MAX_NAME_LENGTH-1] = '\0'; // Ensure null termination
                 }
             } else {
                 printf("⚠️ Input error. Using default name.\n");
                 sprintf(voting_data->candidate_names[i], "Candidate %d", i+1);
             }
         }
     }
     
     printf("Enter number of voters to simulate (max %d): ", MAX_VOTERS);
     if (scanf("%d", &num_voters) != 1) {
         printf("⚠️ Invalid number. Using default (10 voters).\n");
         num_voters = 10;
         // Clear input buffer
         while (getchar() != '\n');
     } else {
         // Check range and clear input buffer
         if (num_voters <= 0 || num_voters > MAX_VOTERS) {
             printf("⚠️ Invalid number. Using default (10 voters).\n");
             num_voters = 10;
         }
         // Clear input buffer
         while (getchar() != '\n');
     }
     
     printf("Enter number of observers to simulate (max %d): ", MAX_OBSERVERS);
     if (scanf("%d", &num_observers) != 1) {
         printf("⚠️ Invalid number. Using default (3 observers).\n");
         num_observers = 3;
         // Clear input buffer
         while (getchar() != '\n');
     } else {
         // Check range and clear input buffer
         if (num_observers <= 0 || num_observers > MAX_OBSERVERS) {
             printf("⚠️ Invalid number. Using default (3 observers).\n");
             num_observers = 3;
         }
         // Clear input buffer
         while (getchar() != '\n');
     }
     
     // After initializing candidates and getting voter/observer counts
     // Log the process mode setup details
     fprintf(log_file, "CANDIDATE SETUP:\n");
     for (int i = 0; i < voting_data->candidate_count; i++) {
         fprintf(log_file, "Candidate %d: %s\n", i, voting_data->candidate_names[i]);
     }
     fprintf(log_file, "\n");
     fprintf(log_file, "MODE DETAILS: Process simulation mode\n");
     fprintf(log_file, "CONFIGURATION: %d voters, %d observers\n", num_voters, num_observers);
     fprintf(log_file, "IMPLEMENTATION: Using fork() for separate processes\n");
     fprintf(log_file, "SYNCHRONIZATION: Shared memory and POSIX semaphores\n\n");
     fprintf(log_file, "-------------------------------------------------\n\n");
     fflush(log_file);
     
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
     
     // Add timing information to log
     fprintf(log_file, "EXECUTION STATISTICS:\n");
     fprintf(log_file, "Total execution time: %.2f seconds\n\n", elapsed_time);
     fflush(log_file);
     
     printf("\n⏱️ Process mode completed in %.2f seconds\n", elapsed_time);
     printf("Performance data saved for comparison\n");
     
     // Calculate per-voter and per-observer metrics
     double time_per_voter = num_voters > 0 ? elapsed_time / num_voters : 0;
     double time_per_observer = num_observers > 0 ? elapsed_time / num_observers : 0;
     
     // Get current timestamp
     time_t now;
     struct tm *timeinfo;
     char timestamp[30];
     
     time(&now);
     timeinfo = localtime(&now);
     strftime(timestamp, sizeof(timestamp), "[%d-%m-%Y_%H-%M-%S]", timeinfo);
     
     FILE *perf_file = fopen("performance_data.txt", "a");
     if (perf_file) {
         fprintf(perf_file, "%s Process mode: %d voters, %d observers, %.6f seconds, %.6f sec/voter, %.6f sec/observer\n", 
                 timestamp, num_voters, num_observers, elapsed_time, time_per_voter, time_per_observer);
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
     // Create a log file specifically for this comparison
     char comparison_log[100];
     time_t now;
     struct tm *timeinfo;
     char timestamp[30];
     
     time(&now);
     timeinfo = localtime(&now);
     strftime(timestamp, sizeof(timestamp), "%d-%m-%Y_%H-%M-%S", timeinfo);
     snprintf(comparison_log, sizeof(comparison_log), "performance_report_[%s].txt", timestamp);
     
     FILE *report_file = fopen(comparison_log, "w");
     if (!report_file) {
         perror("Failed to create performance report file");
         return;
     }
     
     // Open the performance data file
     FILE *perf_file = fopen("performance_data.txt", "r");
     if (!perf_file) {
         printf("No performance data available yet.\n");
         fprintf(report_file, "No performance data available yet.\n");
         fclose(report_file);
         return;
     }
     
     // Write report header
     fprintf(report_file, "=================================================\n");
     fprintf(report_file, "PERFORMANCE ANALYSIS REPORT\n");
     fprintf(report_file, "=================================================\n");
     fprintf(report_file, "Report generated at: %s", ctime(&now));
     fprintf(report_file, "\n");
     fprintf(report_file, "System information: Synchronized Voting System\n");
     fprintf(report_file, "-------------------------------------------------\n\n");
     fprintf(report_file, "PERFORMANCE DATA:\n\n");
     
     // Copy the full performance data file into the report
     char line[512];
     rewind(perf_file);
     while (fgets(line, sizeof(line), perf_file)) {
         fprintf(report_file, "%s", line);
     }
     fprintf(report_file, "\n");
     
     // Structure to store data
     typedef struct {
         int voters;
         int observers;
         double total_time;
         double per_voter_time;
         double per_observer_time;
     } RunData;
     
     // Arrays to store data from each mode
     RunData thread_runs[100] = {0};    // Assuming no more than 100 runs
     RunData process_runs[100] = {0};
     int thread_count = 0;
     int process_count = 0;
     
     // Reset to beginning of file
     rewind(perf_file);
     
     // Parse each line of the performance data
     while (fgets(line, sizeof(line), perf_file)) {
         // Skip header lines
         if (strstr(line, "Thread mode:") == NULL && strstr(line, "Process mode:") == NULL) {
             continue;
         }
         
         // Variables to extract data
         int voters, observers;
         double time, per_voter, per_observer;
         char mode_type[20];
         
         // Try to parse the newer format with per-voter and per-observer metrics
         if (strstr(line, "sec/voter")) {
             if (sscanf(line, "%*s %[^:]: %d voters, %d observers, %lf seconds, %lf sec/voter, %lf sec/observer",
                     mode_type, &voters, &observers, &time, &per_voter, &per_observer) != 6) {
                 continue; // Skip if not in expected format
             }
         } else {
             // Try to parse older format without per-voter and per-observer metrics
             if (sscanf(line, "%*s %[^:]: %d voters, %d observers, %lf seconds",
                     mode_type, &voters, &observers, &time) != 4) {
                 continue; // Skip if not in expected format
             }
             // Calculate per-voter and per-observer metrics
             per_voter = voters > 0 ? time / voters : 0;
             per_observer = observers > 0 ? time / observers : 0;
         }
         
         // Store data in appropriate array
         if (strstr(mode_type, "Thread") != NULL) {
             thread_runs[thread_count].voters = voters;
             thread_runs[thread_count].observers = observers;
             thread_runs[thread_count].total_time = time;
             thread_runs[thread_count].per_voter_time = per_voter;
             thread_runs[thread_count].per_observer_time = per_observer;
             thread_count++;
         } else if (strstr(mode_type, "Process") != NULL) {
             process_runs[process_count].voters = voters;
             process_runs[process_count].observers = observers;
             process_runs[process_count].total_time = time;
             process_runs[process_count].per_voter_time = per_voter;
             process_runs[process_count].per_observer_time = per_observer;
             process_count++;
         }
     }
     
     fclose(perf_file);
     
     // If no data found, report error
     if (thread_count == 0 && process_count == 0) {
         fprintf(report_file, "\nNo valid performance data found in the expected format.\n");
         printf("No valid performance data found.\n");
         fclose(report_file);
         return;
     }
     
     // Compute aggregates for thread mode
     int total_thread_voters = 0;
     int total_thread_observers = 0;
     double total_thread_time = 0.0;
     double sum_thread_per_voter = 0.0;
     double sum_thread_per_observer = 0.0;
     
     for (int i = 0; i < thread_count; i++) {
         total_thread_voters += thread_runs[i].voters;
         total_thread_observers += thread_runs[i].observers;
         total_thread_time += thread_runs[i].total_time;
         sum_thread_per_voter += thread_runs[i].per_voter_time * thread_runs[i].voters; // Weighted sum
         sum_thread_per_observer += thread_runs[i].per_observer_time * thread_runs[i].observers; // Weighted sum
     }
     
     // Compute average per-voter and per-observer times for thread mode
     double avg_thread_per_voter = total_thread_voters > 0 ? 
         sum_thread_per_voter / total_thread_voters : 0;
     double avg_thread_per_observer = total_thread_observers > 0 ? 
         sum_thread_per_observer / total_thread_observers : 0;
     
     // Compute aggregates for process mode
     int total_process_voters = 0;
     int total_process_observers = 0;
     double total_process_time = 0.0;
     double sum_process_per_voter = 0.0;
     double sum_process_per_observer = 0.0;
     
     for (int i = 0; i < process_count; i++) {
         total_process_voters += process_runs[i].voters;
         total_process_observers += process_runs[i].observers;
         total_process_time += process_runs[i].total_time;
         sum_process_per_voter += process_runs[i].per_voter_time * process_runs[i].voters; // Weighted sum
         sum_process_per_observer += process_runs[i].per_observer_time * process_runs[i].observers; // Weighted sum
     }
     
     // Compute average per-voter and per-observer times for process mode
     double avg_process_per_voter = total_process_voters > 0 ? 
         sum_process_per_voter / total_process_voters : 0;
     double avg_process_per_observer = total_process_observers > 0 ? 
         sum_process_per_observer / total_process_observers : 0;
     
     // Write analysis section
     fprintf(report_file, "-------------------------------------------------\n");
     fprintf(report_file, "PERFORMANCE ANALYSIS\n");
     fprintf(report_file, "-------------------------------------------------\n");
     fprintf(report_file, "Total measurements: %d (Thread mode: %d, Process mode: %d)\n\n", 
             thread_count + process_count, thread_count, process_count);
     
     // Thread mode analysis
     if (thread_count > 0) {
         fprintf(report_file, "Thread Mode Analysis:\n");
         fprintf(report_file, "• Total number of voters: %d\n", total_thread_voters);
         fprintf(report_file, "• Total number of observers: %d\n", total_thread_observers);
         fprintf(report_file, "• Total execution time: %.6f seconds\n", total_thread_time);
         fprintf(report_file, "• Average time per voter: %.6f seconds\n", avg_thread_per_voter);
         fprintf(report_file, "• Average time per observer: %.6f seconds\n\n", avg_thread_per_observer);
     }
     
     // Process mode analysis
     if (process_count > 0) {
         fprintf(report_file, "Process Mode Analysis:\n");
         fprintf(report_file, "• Total number of voters: %d\n", total_process_voters);
         fprintf(report_file, "• Total number of observers: %d\n", total_process_observers);
         fprintf(report_file, "• Total execution time: %.6f seconds\n", total_process_time);
         fprintf(report_file, "• Average time per voter: %.6f seconds\n", avg_process_per_voter);
         fprintf(report_file, "• Average time per observer: %.6f seconds\n\n", avg_process_per_observer);
     }
     
     // Performance comparison
     if (thread_count > 0 && process_count > 0) {
         double voter_diff_percent = 0;
         double observer_diff_percent = 0;
         
         // Compare per-voter metrics
         if (avg_thread_per_voter > 0 && avg_process_per_voter > 0) {
             double voter_diff = avg_thread_per_voter - avg_process_per_voter;
             voter_diff_percent = (voter_diff / (voter_diff > 0 ? avg_thread_per_voter : avg_process_per_voter)) * 100;
             
             fprintf(report_file, "PERFORMANCE COMPARISON:\n");
             if (avg_thread_per_voter < avg_process_per_voter) {
                 fprintf(report_file, "• Per Voter: Thread mode is faster by %.6f seconds (%.2f%%)\n", 
                         -voter_diff, fabs(voter_diff_percent));
             } else if (avg_process_per_voter < avg_thread_per_voter) {
                 fprintf(report_file, "• Per Voter: Process mode is faster by %.6f seconds (%.2f%%)\n", 
                         voter_diff, voter_diff_percent);
             } else {
                 fprintf(report_file, "• Per Voter: Both modes have identical performance\n");
             }
             
             // Compare per-observer metrics
             if (avg_thread_per_observer > 0 && avg_process_per_observer > 0) {
                 double observer_diff = avg_thread_per_observer - avg_process_per_observer;
                 observer_diff_percent = (observer_diff / (observer_diff > 0 ? avg_thread_per_observer : avg_process_per_observer)) * 100;
                 
                 if (avg_thread_per_observer < avg_process_per_observer) {
                     fprintf(report_file, "• Per Observer: Thread mode is faster by %.6f seconds (%.2f%%)\n", 
                             -observer_diff, fabs(observer_diff_percent));
                 } else if (avg_process_per_observer < avg_thread_per_observer) {
                     fprintf(report_file, "• Per Observer: Process mode is faster by %.6f seconds (%.2f%%)\n", 
                             observer_diff, observer_diff_percent);
                 } else {
                     fprintf(report_file, "• Per Observer: Both modes have identical performance\n");
                 }
             }
             
             // Overall conclusion (average of voter and observer percentages)
             double overall_percent = (fabs(voter_diff_percent) + fabs(observer_diff_percent)) / 2;
             fprintf(report_file, "\nOVERALL CONCLUSION:\n");
             if (voter_diff_percent > 0 && observer_diff_percent > 0) {
                 fprintf(report_file, "Processes were faster than threads by %.2f%%.\n", overall_percent);
             } else if (voter_diff_percent < 0 && observer_diff_percent < 0) {
                 fprintf(report_file, "Threads were faster than processes by %.2f%%.\n", overall_percent);
             } else {
                 fprintf(report_file, "Mixed results: one mode was faster for voters, the other for observers.\n");
             }
         }
     }
     
     fprintf(report_file, "\n=================================================\n");
     fprintf(report_file, "END OF PERFORMANCE ANALYSIS\n");
     fprintf(report_file, "=================================================\n");
     
     fclose(report_file);
     
     // Display condensed results on console
     printf("\n📊 === Performance Comparison ===\n");
     
     if (thread_count > 0) {
         printf("Thread Mode:\n");
         printf("• Total voters: %d, Total observers: %d\n", total_thread_voters, total_thread_observers);
         printf("• Avg time per voter: %.6f seconds\n", avg_thread_per_voter);
         printf("• Avg time per observer: %.6f seconds\n\n", avg_thread_per_observer);
     }
     
     if (process_count > 0) {
         printf("Process Mode:\n");
         printf("• Total voters: %d, Total observers: %d\n", total_process_voters, total_process_observers);
         printf("• Avg time per voter: %.6f seconds\n", avg_process_per_voter);
         printf("• Avg time per observer: %.6f seconds\n\n", avg_process_per_observer);
     }
     
     if (thread_count > 0 && process_count > 0) {
         if (avg_thread_per_voter < avg_process_per_voter) {
             printf("Per Voter: Thread mode is %.2f%% faster\n", 
                    (avg_process_per_voter - avg_thread_per_voter) / avg_process_per_voter * 100);
         } else {
             printf("Per Voter: Process mode is %.2f%% faster\n", 
                    (avg_thread_per_voter - avg_process_per_voter) / avg_thread_per_voter * 100);
         }
         
         if (avg_thread_per_observer < avg_process_per_observer) {
             printf("Per Observer: Thread mode is %.2f%% faster\n", 
                    (avg_process_per_observer - avg_thread_per_observer) / avg_process_per_observer * 100);
         } else {
             printf("Per Observer: Process mode is %.2f%% faster\n", 
                    (avg_thread_per_observer - avg_process_per_observer) / avg_thread_per_observer * 100);
         }
     }
     
     printf("\nPerformance report saved to: %s\n", comparison_log);
     printf("===========================\n");
 }
 
 int main(int argc, char *argv[]) {
     int mode;
     
     // Initialize resources
     initialize_resources();
     
     // Initialize performance data file if needed
     initialize_performance_file();
     
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
