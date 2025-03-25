/**
 * Test program for the kernel logging subsystem
 *
 * This program demonstrates how to write to and read from the kernel log buffer
 * using the character device interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define DEVICE_PATH "/dev/klogbuf"
#define NUM_WRITERS 5
#define NUM_READERS 3
#define NUM_WRITES 10
#define WRITE_SIZE 128
#define READ_SIZE 1024

/* Writer thread function */
void *writer_thread(void *arg)
{
    int id = *((int *)arg);
    int fd;
    char buffer[WRITE_SIZE];
    int i;

    /* Open the device for writing */
    fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0)
    {
        perror("Failed to open device for writing");
        return NULL;
    }

    /* Write to the device multiple times */
    for (i = 0; i < NUM_WRITES; i++)
    {
        /* Create a message with timestamp, thread ID, and iteration */
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[20];
        strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S", tm_info);

        snprintf(buffer, WRITE_SIZE, "[%s] Writer %d, Iteration %d: This is a test message to the kernel log buffer.\n",
                 timestamp, id, i);

        /* Write the message to the device */
        ssize_t bytes_written = write(fd, buffer, strlen(buffer));
        if (bytes_written < 0)
        {
            perror("Failed to write to device");
            break;
        }

        printf("Writer %d wrote %zd bytes\n", id, bytes_written);

        /* Sleep for a random time between 100-500ms */
        usleep((rand() % 400 + 100) * 1000);
    }

    /* Close the device */
    close(fd);

    return NULL;
}

/* Reader thread function */
void *reader_thread(void *arg)
{
    int id = *((int *)arg);
    int fd;
    char buffer[READ_SIZE];
    int i;

    /* Open the device for reading */
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0)
    {
        perror("Failed to open device for reading");
        return NULL;
    }

    /* Read from the device multiple times */
    for (i = 0; i < NUM_WRITES; i++)
    {
        /* Read from the device */
        memset(buffer, 0, READ_SIZE);
        ssize_t bytes_read = read(fd, buffer, READ_SIZE - 1);

        if (bytes_read < 0)
        {
            perror("Failed to read from device");
            break;
        }
        else if (bytes_read == 0)
        {
            printf("Reader %d: End of file reached\n", id);
        }
        else
        {
            buffer[bytes_read] = '\0';
            printf("Reader %d read %zd bytes:\n%s\n", id, bytes_read, buffer);
        }

        /* Sleep for a random time between 200-800ms */
        usleep((rand() % 600 + 200) * 1000);
    }

    /* Close the device */
    close(fd);

    return NULL;
}

/* Main function */
int main()
{
    pthread_t writers[NUM_WRITERS];
    pthread_t readers[NUM_READERS];
    int writer_ids[NUM_WRITERS];
    int reader_ids[NUM_READERS];
    int i;

    /* Seed the random number generator */
    srand(time(NULL));

    /* Create reader threads */
    for (i = 0; i < NUM_READERS; i++)
    {
        reader_ids[i] = i;
        if (pthread_create(&readers[i], NULL, reader_thread, &reader_ids[i]) != 0)
        {
            perror("Failed to create reader thread");
            return 1;
        }
    }

    /* Create writer threads */
    for (i = 0; i < NUM_WRITERS; i++)
    {
        writer_ids[i] = i;
        if (pthread_create(&writers[i], NULL, writer_thread, &writer_ids[i]) != 0)
        {
            perror("Failed to create writer thread");
            return 1;
        }
    }

    /* Wait for writer threads to finish */
    for (i = 0; i < NUM_WRITERS; i++)
    {
        pthread_join(writers[i], NULL);
    }

    /* Wait for reader threads to finish */
    for (i = 0; i < NUM_READERS; i++)
    {
        pthread_join(readers[i], NULL);
    }

    /* Read from /proc entry */
    FILE *proc_file = fopen("/proc/klogbuf", "r");
    if (proc_file)
    {
        char buffer[4096];
        size_t bytes_read;

        printf("\nContents of /proc/klogbuf:\n");
        while ((bytes_read = fread(buffer, 1, sizeof(buffer) - 1, proc_file)) > 0)
        {
            buffer[bytes_read] = '\0';
            printf("%s", buffer);
        }

        fclose(proc_file);
    }
    else
    {
        perror("Failed to open /proc/klogbuf");
    }

    return 0;
}
