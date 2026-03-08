/**
 * @file cpulimit.c
 * @brief CPU Limit - A tool to limit CPU usage of processes
 * 
 * Original Author: Angelo Marletta
 * Original Date: 26/06/2005
 * Original Version: 1.1
 * 
 * Patched by: torden <https://github.com/torden/>
 * Version: 1.0.1-p1
 * 
 * This program limits the CPU usage of a target process by periodically
 * sending SIGSTOP and SIGCONT signals.
 */

/* Feature test macros for POSIX functions */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/resource.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <syslog.h>
#include <pwd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/param.h>

/* ============================================================================
 * Platform Abstraction
 * ============================================================================ */

#ifdef __linux__
    #define PROC_FILESYSTEM_PATH "/proc"
#elif defined(__FreeBSD__)
    #define PROC_FILESYSTEM_PATH "/compat/linux/proc"
#else
    #define PROC_FILESYSTEM_PATH "/proc"  /* Default to Linux */
#endif

/* ============================================================================
 * Named Constants - Exit Codes
 * ============================================================================ */

#define EXIT_SUCCESS_CODE           0   /* Successful execution */
#define EXIT_NO_PROCESS             2   /* No suitable target process found */
#define EXIT_PERMISSION_DENIED      3   /* Permission denied */
#define EXIT_CONFIG_ERROR           4   /* Configuration error */
#define EXIT_SYSTEM_ERROR           5   /* System error */

/* ============================================================================
 * Named Constants - Buffer Sizes
 * ============================================================================ */

#define LOG_MESSAGE_BUFFER_SIZE     8192    /* Size for syslog message buffer */
#define LOG_BODY_BUFFER_SIZE        7168    /* Size for log message body buffer */
#define CPU_USAGE_SAMPLE_BUFFER_SIZE 10     /* Circular buffer size for CPU samples */
#define SEMAPHORE_NAME_BUFFER_SIZE  256     /* Buffer size for semaphore names */

/* ============================================================================
 * Named Constants - Priority Values
 * ============================================================================ */

#define PRIORITY_LOW                19      /* Lowest process priority */
#define PRIORITY_HIGH               -20     /* Highest process priority */

/* ============================================================================
 * Named Constants - Program Information
 * ============================================================================ */

#define PGNAME                      "CPULimit"
#define VERSION                     "1.0.1-p1"
#define MUTI_RUN_DETECT_PREFIX_SEM_NAME "CPULIMIT"

/* ============================================================================
 * Named Constants - Utility Macros
 * ============================================================================ */

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

/* ============================================================================
 * Global Variables
 * ============================================================================ */

/* pid of the controlled process */
static unsigned int pid = 0;

/* executable file name */
static char *program_name;

/* verbose mode */
static unsigned short verbose = 0;

/* lazy mode */
static unsigned short lazy = 0;

/* daemonize mode */
static unsigned short daemonize = 0;
static unsigned short daemond = 0;

/* log file */
static FILE *pLogFileFD;

/* my forked pid */
static unsigned int pid_me;

/* logfile path */
static const char *logpath = NULL;

/* check multi run */
static char semName[SEMAPHORE_NAME_BUFFER_SIZE];
static char *psemName = semName;
static sem_t *duplCheckSem;

/* force mode */
static unsigned short force = 0;

/* Signal handler safety flag */
static volatile sig_atomic_t received_signal = 0;

/* Runtime-detected HZ value */
static long g_hz = 0;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get the system clock ticks per second (HZ) at runtime
 * 
 * Uses sysconf() to detect the actual HZ value instead of hardcoding.
 * Falls back to 100 if sysconf fails.
 * 
 * @return The HZ value (clock ticks per second)
 */
static long get_hz(void) {
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) {
        hz = 100;  /* Fallback to default */
    }
    return hz;
}

/**
 * @brief Portable implementation of memrchr (reverse memory search)
 * 
 * Searches for the last occurrence of character c in the first n bytes of s.
 * This is a non-standard function, so we provide a portable implementation.
 * 
 * @param s Pointer to the memory block
 * @param c Character to search for (as int)
 * @param n Number of bytes to search
 * @return Pointer to the last occurrence of c, or NULL if not found
 */
static void *portable_memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    
    if (n == 0) {
        return NULL;
    }
    
    /* Start from the end and search backwards */
    for (size_t i = n; i > 0; i--) {
        if (p[i - 1] == (unsigned char)c) {
            return (void *)(p + i - 1);
        }
    }
    
    return NULL;
}

/**
 * @brief Build a semaphore name from prefix and PID
 * 
 * Creates a unique semaphore name by combining the prefix with the target PID.
 * This eliminates code duplication in semaphore name construction.
 * 
 * @param buffer Output buffer for the semaphore name
 * @param size Size of the output buffer
 * @param target_pid PID to include in the semaphore name
 */
static void build_semaphore_name(char *buffer, size_t size, pid_t target_pid) {
    snprintf(buffer, size, "%s_%d", MUTI_RUN_DETECT_PREFIX_SEM_NAME, target_pid);
}

/**
 * @brief Calculate the difference between two timespec values in microseconds
 * 
 * Computes ta - tb with overflow protection.
 * 
 * @param ta Pointer to the first timespec (minuend)
 * @param tb Pointer to the second timespec (subtrahend)
 * @return Difference in microseconds, or 0 if negative/overflow
 */
static inline long timediff(const struct timespec *ta, const struct timespec *tb) {
    /* Check for negative time difference (overflow protection) */
    if (ta->tv_sec < tb->tv_sec) {
        return 0;
    }

    long sec_diff = ta->tv_sec - tb->tv_sec;

    /* Check for overflow in seconds to microseconds conversion */
    if (sec_diff > 2000000) {  /* More than ~2000 seconds would overflow */
        return 2000000000000L; /* Return max reasonable value */
    }

    long us_sec = sec_diff * 1000000L;
    long us_nsec = (ta->tv_nsec / 1000) - (tb->tv_nsec / 1000);

    /* Check for negative nanosecond difference */
    if (us_nsec < 0 && us_sec > 0) {
        us_sec -= 1000000;
        us_nsec += 1000000;
    }

    long total_us = us_sec + us_nsec;

    /* Final overflow check */
    if (total_us < 0) {
        return 0;
    }

    return total_us;
}

/**
 * @brief Check if a PID is currently alive
 * 
 * Uses kill(pid, 0) to check if the process exists without sending a signal.
 * 
 * @param ckpid The PID to check
 * @return 1 if the process is alive, 0 otherwise
 */
unsigned short isLivePID(unsigned int ckpid) {
    int saved_errno;

    if (0 == kill(ckpid, 0)) {
        return 1;
    }

    /* Preserve errno immediately after kill() call */
    saved_errno = errno;

    if (ESRCH == saved_errno) {
        return 0;
    } else {
        return 0;
    }
}

/**
 * @brief Check if a PID exists by verifying /proc/<pid>/stat
 * 
 * Uses fopen() instead of access() to avoid TOCTOU race conditions.
 * 
 * @param ckpid The PID to check
 * @return 1 if the process exists, 0 otherwise
 */
short checkPid(unsigned int ckpid) {
    char buf[128];  /* Local buffer for thread safety */
    FILE *f;

    snprintf(buf, sizeof(buf), PROC_FILESYSTEM_PATH "/%d/stat", ckpid);

    /* Use fopen() instead of access() to avoid TOCTOU race condition */
    f = fopen(buf, "r");
    if (f != NULL) {
        fclose(f);
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check for existing cpulimit instances using semaphores
 * 
 * Detects if another cpulimit instance is already running for the target PID.
 * Handles stale semaphores and force mode cleanup.
 */
void checkExistsRunOnSem(void) {
    int cpulimitPid = 0;
    
    build_semaphore_name(psemName, sizeof(semName), pid);

    /* First try to open existing semaphore */
    duplCheckSem = sem_open(psemName, O_RDONLY);
    if (SEM_FAILED != duplCheckSem) {
        if (-1 == sem_getvalue(duplCheckSem, &cpulimitPid)) {
            fprintf(stdout, "System failure: Cannot read named semaphore %s: %s\n", 
                    psemName, strerror(errno));
            sem_close(duplCheckSem);
            duplCheckSem = NULL;
            exit(EXIT_SYSTEM_ERROR);
        }

        if (0 < cpulimitPid && 0 == checkPid(cpulimitPid)) {
            if (duplCheckSem != NULL) {
                sem_close(duplCheckSem);
                duplCheckSem = NULL;
            }
            sem_unlink(psemName);
            memset(psemName, 0x00, sizeof(semName));
            return;
        }

        if (0 < cpulimitPid && 1 == force) {
            if (0 != kill(cpulimitPid, SIGKILL)) {
                fprintf(stdout, "System failure: Cannot run in force mode, target PID: %d: %s\n", 
                        cpulimitPid, strerror(errno));
                exit(EXIT_SYSTEM_ERROR);
            } else {
                if (duplCheckSem != NULL) {
                    sem_close(duplCheckSem);
                    duplCheckSem = NULL;
                }
                sem_unlink(psemName);
                memset(psemName, 0x00, sizeof(semName));
            }
        } else {
            fprintf(stdout, "Another CPULimit daemon is running (PID:%d). Please stop it first.\n", 
                    cpulimitPid);
            exit(EXIT_CONFIG_ERROR);
        }
    } else {
        if (duplCheckSem != NULL) {
            sem_close(duplCheckSem);
            duplCheckSem = NULL;
        }
    }
}

/**
 * @brief Create a semaphore for multi-run detection
 * 
 * Creates a unique semaphore to prevent multiple cpulimit instances
 * from controlling the same target process.
 */
void setRunOnSem(void) {
    int cpulimitPid = 0;
    
    build_semaphore_name(psemName, sizeof(semName), pid);

    /* Clean up any stale semaphore before creating new one */
    sem_unlink(psemName);

    /* Use O_CREAT | O_EXCL to atomically create semaphore */
    duplCheckSem = sem_open(psemName, O_CREAT | O_EXCL, 
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, pid_me);
    if (SEM_FAILED == duplCheckSem) {
        if (EEXIST == errno) {
            fprintf(stdout, "Another CPULimit daemon is running (PID:%d). Please stop it first.\n", 
                    cpulimitPid);
            exit(EXIT_CONFIG_ERROR);
        } else {
            fprintf(stdout, "System failure: Cannot create semaphore: %s\n", strerror(errno));
            exit(EXIT_SYSTEM_ERROR);
        }
    }
}

/**
 * @brief Destroy the semaphore used for multi-run detection
 * 
 * Closes and unlinks the semaphore to clean up resources.
 */
void destroyExstsRunOnSem(void) {
    if (duplCheckSem != NULL && duplCheckSem != SEM_FAILED) {
        sem_close(duplCheckSem);
        duplCheckSem = SEM_FAILED;
    }
    if (psemName[0] != '\0') {
        sem_unlink(psemName);
    }
}

/**
 * @brief Daemonize the process
 * 
 * Forks the process, redirects standard file descriptors to /dev/null,
 * and creates a new session. The parent process exits.
 */
void setDaemonize(void) {
    pid_t pidthis = fork();

    if (0 > pidthis) {
        /* Fork failed - cleanup semaphore before exit */
        destroyExstsRunOnSem();
        fprintf(stderr, "Error: Fork failed: %s\n", strerror(errno));
        exit(EXIT_SYSTEM_ERROR);
    } else if (pidthis != 0) {
        /* Parent process - cleanup semaphore and exit */
        destroyExstsRunOnSem();
        exit(EXIT_SUCCESS_CODE);
    }

    /* Child process continues as daemon */

    /* Redirect stdin, stdout, stderr to /dev/null for proper daemonization */
    int devnull_fd = open("/dev/null", O_RDWR);
    if (devnull_fd >= 0) {
        dup2(devnull_fd, STDIN_FILENO);
        dup2(devnull_fd, STDOUT_FILENO);
        dup2(devnull_fd, STDERR_FILENO);
        if (devnull_fd > 2) {
            close(devnull_fd);
        }
    } else {
        /* Fallback: close standard file descriptors */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    setsid();
    daemond = 1;
}

/**
 * @brief Log a message to syslog
 * 
 * @param logtype Syslog priority level (LOG_EMERG to LOG_DEBUG)
 * @param logfmt Printf-style format string
 * @param ... Format arguments
 */
void setSyslog(unsigned short logtype, const char *logfmt, ...) {
    /*
     * LOG_EMERG   0   system is unusable
     * LOG_ALERT   1   action must be taken immediately
     * LOG_CRIT    2   critical conditions
     * LOG_ERR     3   error conditions
     * LOG_WARNING 4   warning conditions
     * LOG_NOTICE  5   normal but significant condition
     * LOG_INFO    6   informational
     * LOG_DEBUG   7   debug-level messages
     */
    if (7 >= logtype) {
        openlog("cpulimit", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

        char msgbuf[LOG_MESSAGE_BUFFER_SIZE];
        char msgbody[LOG_BODY_BUFFER_SIZE];

        va_list args;
        va_start(args, logfmt);
        vsnprintf(msgbody, sizeof(msgbody), logfmt, args);
        va_end(args);

        snprintf(msgbuf, sizeof(msgbuf), "%s\n", msgbody);

        /* Fix format string vulnerability - use %s format specifier */
        syslog(logtype, "%s", msgbuf);
        closelog();
    }
}

/**
 * @brief Log a message to the configured log file
 * 
 * @param logfmt Printf-style format string
 * @param ... Format arguments
 * @return 1 on success, 0 on failure
 */
int setLogging(const char *logfmt, ...) {
    if (NULL == logpath) {
        return 1;
    }

    time_t rawtime;
    struct tm *timeinfo;
    char msgbuf[LOG_MESSAGE_BUFFER_SIZE];
    char *pmsgbuf;

    pmsgbuf = msgbuf;
    memset(pmsgbuf, 0x00, sizeof(msgbuf));

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    /* log filename */
    char fn[30] = {0x00,};
    char *pfn = fn;
    strftime(pfn, sizeof(fn), "%Y-%m-%d-%H", timeinfo);

    /* logging time */
    char tbuf[30] = {0x00,};
    char *ptbuf = tbuf;
    strftime(ptbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S %Z", timeinfo);

    /* user logmessage */
    char msgbody[LOG_BODY_BUFFER_SIZE];
    char *pmsgbody = msgbody;
    va_list args;
    va_start(args, logfmt);
    vsnprintf(pmsgbody, sizeof(msgbody), logfmt, args);
    va_end(args);

    /* merge full */
    snprintf(pmsgbuf, sizeof(msgbuf), "[%s] %s\n", ptbuf, pmsgbody);

    pLogFileFD = fopen(logpath, "a");
    if (NULL == pLogFileFD) {
        setSyslog(LOG_ERR, "Cannot open logfile %s: %s", logpath, strerror(errno));
        return 0;
    }

    size_t ret = fwrite(pmsgbuf, strlen(pmsgbuf), 1, pLogFileFD);

    if (1 > ret) {
        setSyslog(LOG_ERR, "Cannot write to logfile %s: %s", logpath, strerror(errno));
        fclose(pLogFileFD);
        return 0;
    }

    fclose(pLogFileFD);
    return 1;
}

/**
 * @brief Cleanup and exit the program
 * 
 * Performs cleanup operations before exiting:
 * - Sends SIGCONT to target process if daemonized
 * - Logs exit message
 * - Destroys semaphore
 * 
 * @param retcode Exit code to return
 */
void setExit(int retcode) {
    if (1 == daemond) {
        kill(pid, SIGCONT);
    }
    setLogging("Exiting cpulimit.");
    setSyslog(LOG_INFO, "Exiting cpulimit (PID: %d)", (int)getpid());
    destroyExstsRunOnSem();
    exit(retcode);
}

/**
 * @brief Wait for a target process to appear
 * 
 * Scans /proc for the target PID and waits if not found.
 * Sets appropriate process priorities during operation.
 * 
 * @param pid The PID to wait for
 * @return 0 on success, -1 on error
 */
int waitforpid(int pid) {
    /* Switch to low priority */
    if (setpriority(PRIO_PROCESS, getpid(), PRIORITY_LOW) != 0) {
        setLogging("Warning: Cannot renice to low priority: %s", strerror(errno));
    }

    int i = 0;

    while (1) {
        DIR *dip;
        struct dirent *dit;

        /* Open a directory stream to /proc directory */
        if ((dip = opendir(PROC_FILESYSTEM_PATH)) == NULL) {
            setSyslog(LOG_ERR, "Cannot open %s directory: %s", 
                      PROC_FILESYSTEM_PATH, strerror(errno));
            return -1;
        }

        /* Read from /proc and seek for process dirs */
        while ((dit = readdir(dip)) != NULL) {
            /* Get pid */
            if (pid == atoi(dit->d_name)) {
                /* PID detected */
                if (kill(pid, SIGSTOP) == 0 && kill(pid, SIGCONT) == 0) {
                    /* Process is ok! */
                    goto done;
                } else {
                    setLogging("Error: Process %d detected, but you don't have permission to control it: %s",
                               pid, strerror(errno));
                }
            }
        }

        /* Close the dir stream and check for errors */
        if (closedir(dip) == -1) {
            setSyslog(LOG_ERR, "Cannot close %s directory: %s", 
                      PROC_FILESYSTEM_PATH, strerror(errno));
            return -1;
        }

        /* No suitable target found */
        if (i++ == 0) {
            if (lazy) {
                setLogging("No process found with PID %d", pid);
                setExit(EXIT_NO_PROCESS);
            } else {
                setLogging("Warning: No target process (PID %d) found. Waiting for it...", pid);
            }
        }
        /* Sleep for a while */
        sleep(2);
    }

done:
    /* Now set high priority, if possible */
    if (setpriority(PRIO_PROCESS, getpid(), PRIORITY_HIGH) != 0) {
        setLogging("Warning: Cannot renice to high priority: %s.\n"
                   "To work better, you should run this program as root.", strerror(errno));
    }
    return 0;
}

/**
 * @brief Find a process by executable name or path
 * 
 * Scans /proc/<pid>/exe links to find a matching process.
 * 
 * @param process The executable name or absolute path to search for
 * @return PID of the found process, or -1 on error
 */
int getpidof(const char *process) {
    /* Set low priority */
    if (setpriority(PRIO_PROCESS, getpid(), PRIORITY_LOW) != 0) {
        setLogging("Warning: Cannot renice to low priority: %s", strerror(errno));
    }

    char exelink[PATH_MAX];
    char exepath[PATH_MAX + 1];
    pid_t found_pid = 0;
    int i = 0;

    while (1) {
        DIR *dip;
        struct dirent *dit;

        /* Open a directory stream to /proc directory */
        if ((dip = opendir(PROC_FILESYSTEM_PATH)) == NULL) {
            setSyslog(LOG_ERR, "Cannot open %s directory: %s",
                      PROC_FILESYSTEM_PATH, strerror(errno));
            return -1;
        }

        /* Read from /proc and seek for process dirs */
        while ((dit = readdir(dip)) != NULL) {
            /* Get pid */
            found_pid = atoi(dit->d_name);
            if (found_pid > 0) {
                snprintf(exelink, sizeof(exelink), PROC_FILESYSTEM_PATH "/%d/exe", found_pid);
                ssize_t size = readlink(exelink, exepath, sizeof(exepath) - 1);
                if (size > 0) {
                    int found = 0;
                    exepath[size] = '\0';  /* Null-terminate the path */

                    if (process[0] == '/') {
                        /* Process starts with / then it's an absolute path */
                        /* Support partial path matching: /app/python/bin/python3 matches /app/python/bin/python3.13 */
                        size_t process_len = strlen(process);
                        if (strncmp(exepath, process, process_len) == 0) {
                            /* Exact match or partial match (e.g., python3 matches python3.13) */
                            found = 1;
                        }
                    } else {
                        /* Process is the name of the executable file */
                        /* Use basename matching: extract basename from exepath and compare */
                        const char *exebase = portable_memrchr(exepath, '/', size);
                        exebase = exebase ? exebase + 1 : exepath;
                        if (strstr(exebase, process) != NULL) {
                            found = 1;
                        }
                    }
                    if (found == 1) {
                        if (kill(found_pid, SIGSTOP) == 0 && kill(found_pid, SIGCONT) == 0) {
                            /* Process is ok! */
                            goto done;
                        } else {
                            setLogging("Error: Process %d detected, but you don't have permission to control it: %s",
                                       found_pid, strerror(errno));
                        }
                    }
                }
            }
        }

        /* Close the dir stream and check for errors */
        if (closedir(dip) == -1) {
            setSyslog(LOG_ERR, "Cannot close %s directory: %s", 
                      PROC_FILESYSTEM_PATH, strerror(errno));
            return -1;
        }

        /* No suitable target found */
        if (i++ == 0) {
            if (lazy) {
                setLogging("No process found matching '%s'", process);
                setExit(EXIT_NO_PROCESS);
            } else {
                setLogging("Warning: No target process found matching '%s'. Waiting for it...", process);
            }
        }

        /* Sleep for a while */
        sleep(2);
    }

done:
    setLogging("Process %d detected", found_pid);
    /* Now set high priority, if possible */
    if (setpriority(PRIO_PROCESS, getpid(), PRIORITY_HIGH) != 0) {
        setLogging("Warning: Cannot renice to high priority: %s.\n"
                   "To work better, you should run this program as root.", strerror(errno));
    }
    return found_pid;
}

/**
 * @brief Signal handler for SIGINT and SIGTERM
 * 
 * Sets a flag for safe signal handling. Only atomic operations
 * are performed in the signal handler.
 * 
 * @param sig Signal number received
 */
void setQuit(int sig) {
    /* Signal-safe: only set atomic flag, don't call other functions */
    received_signal = sig;
}

/**
 * @brief Get jiffies count from /proc/<pid>/stat
 * 
 * Reads the process statistics file and extracts the total jiffies
 * (user + kernel mode).
 * 
 * @param pid The process ID to read jiffies for
 * @return Total jiffies count, or -1 on error
 */
long getjiffies(int pid) {
    char stat[32];      /* Local buffer for thread safety */
    char buffer[1024];  /* Local buffer for thread safety */
    
    snprintf(stat, sizeof(stat), PROC_FILESYSTEM_PATH "/%d/stat", pid);
    FILE *f = fopen(stat, "r");
    if (f == NULL) {
        setLogging("Cannot open %s: %s", stat, strerror(errno));
        return -1;
    }

    if (fgets(buffer, sizeof(buffer), f) == NULL) {
        setLogging("Cannot read %s: %s", stat, strerror(errno));
        fclose(f);
        return -1;
    }

    fclose(f);
    char *p = buffer;
    p = memchr(p + 1, ')', sizeof(buffer) - (p - buffer) - 1);
    if (p == NULL) {
        setLogging("Invalid %s format (no ')')", stat);
        return -1;
    }

    int sp = 12;
    while (sp--) {
        p = memchr(p + 1, ' ', sizeof(buffer) - (p - buffer));
        if (p == NULL) {
            setLogging("Invalid %s format (missing field)", stat);
            return -1;
        }
    }
    /* User mode jiffies */
    long utime = atol(p + 1);
    p = memchr(p + 1, ' ', sizeof(buffer) - (p - buffer));
    if (p == NULL) {
        setLogging("Invalid %s format (ktime field)", stat);
        return -1;
    }
    /* Kernel mode jiffies */
    long ktime = atol(p + 1);
    return utime + ktime;
}

/**
 * @brief Process instant photo - timestamp and jiffies snapshot
 */
struct process_screenshot {
    struct timespec when;   /* Timestamp */
    long jiffies;           /* Jiffies count of the process */
    int cputime;            /* Microseconds of work from previous screenshot to current */
};

/**
 * @brief Extracted process statistics
 */
struct cpu_usage {
    float pcpu;             /* CPU usage percentage (0.0 - 1.0) */
    float workingrate;      /* Rate at which process is kept active */
};

/**
 * @brief Compute CPU usage of a process
 * 
 * This function is an autonomous dynamic system that estimates CPU usage
 * by maintaining a circular buffer of process screenshots. It should be
 * called periodically for accurate results.
 * 
 * @param pid Process ID to monitor
 * @param last_working_quantum Last working time in microseconds
 * @param pusage Pointer to cpu_usage structure to fill
 * @return 0 on success, -1 if process does not exist
 */
int compute_cpu_usage(int pid, int last_working_quantum, struct cpu_usage *pusage) {
    /*
     * Circular buffer containing last CPU_USAGE_SAMPLE_BUFFER_SIZE process screenshots.
     * 
     * Buffer size calculation:
     * - MEM_ORDER (10) samples provide a good balance between responsiveness and stability
     * - Each sample captures process state at a point in time
     * - The circular nature allows continuous monitoring without memory growth
     * - Size 10 means we average over the last 10 measurement intervals
     */
    static struct process_screenshot ps[CPU_USAGE_SAMPLE_BUFFER_SIZE];
    
    /* The last screenshot recorded in the buffer */
    static int front = -1;
    
    /* The oldest screenshot recorded in the buffer */
    static int tail = 0;

    if (pusage == NULL) {
        /* Reinit static variables */
        front = -1;
        tail = 0;
        return 0;
    }

    /* Let's advance front index and save the screenshot */
    front = (front + 1) % CPU_USAGE_SAMPLE_BUFFER_SIZE;
    long j = getjiffies(pid);
    if (j >= 0) {
        ps[front].jiffies = j;
    } else {
        setLogging("Cannot get jiffies for PID %d", pid);
        return -1;  /* Error: pid does not exist */
    }
    clock_gettime(CLOCK_REALTIME, &(ps[front].when));
    ps[front].cputime = last_working_quantum;

    /* Buffer actual size is: (front-tail+MEM_ORDER)%MEM_ORDER+1 */
    int size = (front - tail + CPU_USAGE_SAMPLE_BUFFER_SIZE) % CPU_USAGE_SAMPLE_BUFFER_SIZE + 1;

    if (size == 1) {
        /* Not enough samples taken (it's the first one!), return -1 */
        pusage->pcpu = -1;
        pusage->workingrate = 1;
        return 0;
    } else {
        /* Now we can calculate cpu usage, interval dt and dtwork are expressed in microseconds */
        long dt = timediff(&(ps[front].when), &(ps[tail].when));
        long dtwork = 0;
        int i = (tail + 1) % CPU_USAGE_SAMPLE_BUFFER_SIZE;
        int max = (front + 1) % CPU_USAGE_SAMPLE_BUFFER_SIZE;
        do {
            dtwork += ps[i].cputime;
            i = (i + 1) % CPU_USAGE_SAMPLE_BUFFER_SIZE;
        } while (i != max);
        
        long used = ps[front].jiffies - ps[tail].jiffies;
        
        /* Division by zero protection */
        if (dtwork == 0) {
            pusage->pcpu = 0;
            pusage->workingrate = 0;
            return 0;
        }
        
        float usage = (used * 1000000.0 / g_hz) / dtwork;
        pusage->workingrate = 1.0 * dtwork / dt;
        pusage->pcpu = usage * pusage->workingrate;
        if (size == CPU_USAGE_SAMPLE_BUFFER_SIZE)
            tail = (tail + 1) % CPU_USAGE_SAMPLE_BUFFER_SIZE;
        return 0;
    }
}

/**
 * @brief Print the caption for verbose output
 */
void print_caption(void) {
    setLogging("%%CPU\twork quantum\tsleep quantum\tactive rate");
}

/**
 * @brief Print usage information
 * 
 * @param stream Output stream (stdout or stderr)
 * @param exit_code Exit code to use
 */
void print_usage(FILE *stream, int exit_code) {
    fprintf(stream, "Usage: %s TARGET [OPTIONS...]\n", program_name);
    fprintf(stream, "   TARGET must be exactly one of these:\n");
    fprintf(stream, "      -p, --pid=N        PID of the process\n");
    fprintf(stream, "      -L, --logpath      Logfile path\n");
    fprintf(stream, "      -l, --limit=N      Percentage of CPU allowed (0-100, mandatory)\n");
    fprintf(stream, "   OPTIONS:\n");
    fprintf(stream, "      -e, --exe=FILE     Name of the executable program file\n");
    fprintf(stream, "      -P, --path=PATH    Absolute path name of the executable program file\n");
    fprintf(stream, "      -v, --verbose      Show control statistics\n");
    fprintf(stream, "      -z, --lazy         Exit if there is no suitable target process\n");
    fprintf(stream, "      -h, --help         Display this help and exit\n");
    fprintf(stream, "      -d, --daemonize    Run as a daemon\n");
    fprintf(stream, "      -f, --force        Force run, killing existing cpulimit process\n");
    exit(exit_code);
}

/**
 * @brief Validate logpath for security
 * 
 * Checks that the path is absolute and doesn't contain directory traversal.
 * 
 * @param path The path to validate
 * @return 1 if valid, 0 if invalid
 */
static int validate_logpath(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    /* Must be absolute path */
    if (path[0] != '/') {
        return 0;
    }

    /* Check for directory traversal attempts */
    if (strstr(path, "..") != NULL) {
        return 0;
    }

    return 1;
}

/**
 * @brief Safe integer parsing with strtol and error checking
 * 
 * @param str String to parse
 * @param result Pointer to store the result
 * @param min_val Minimum allowed value
 * @param max_val Maximum allowed value
 * @return 1 on success, 0 on failure
 */
static int safe_strtol(const char *str, long *result, int min_val, int max_val) {
    char *endptr;
    long val;

    errno = 0;
    val = strtol(str, &endptr, 10);

    /* Check for conversion errors */
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return 0;
    }

    /* Check range */
    if (val < min_val || val > max_val) {
        return 0;
    }

    *result = val;
    return 1;
}

/**
 * @brief Set resource limits for the process
 * 
 * Configures RLIMIT_NOFILE and RLIMIT_CORE to reasonable values.
 */
static void set_resource_limits(void) {
    struct rlimit rl;

    /* Set RLIMIT_NOFILE to a reasonable limit */
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = min(rl.rlim_cur, 1024);
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            setSyslog(LOG_WARNING, "Cannot set RLIMIT_NOFILE: %s", strerror(errno));
        }
    }

    /* Set RLIMIT_CORE to 0 to prevent core dumps in production */
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) != 0) {
        setSyslog(LOG_WARNING, "Cannot set RLIMIT_CORE: %s", strerror(errno));
    }
}

/**
 * @brief Main entry point
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code
 */
int main(int argc, char **argv) {
    /* Detect HZ at runtime */
    g_hz = get_hz();

    /* Get program name using portable memrchr alternative */
    char *p = (char *)portable_memrchr(argv[0], (unsigned int)'/', strlen(argv[0]));
    program_name = p == NULL ? argv[0] : (p + 1);

    /* Parse arguments */
    int next_option;
    /* A string listing valid short options letters. */
    const char *short_options = "p:e:P:l:vzhdL:f";
    /* An array describing valid long options. */
    const struct option long_options[] = {
        { "pid", 1, NULL, 'p' },
        { "exe", 1, NULL, 'e' },
        { "path", 1, NULL, 'P' },
        { "limit", 1, NULL, 'l' },
        { "verbose", 0, NULL, 'v' },
        { "lazy", 0, NULL, 'z' },
        { "help", 0, NULL, 'h' },
        { "daemonize", 0, NULL, 'd' },
        { "logpath", 1, NULL, 'L' },
        { "force", 0, NULL, 'f' },
        { NULL, 0, NULL, 0 }
    };

    /* Check for help flag first (before privilege check) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(stdout, EXIT_SUCCESS_CODE);
        }
    }

    /* Fix privilege check - use geteuid() instead of getpwuid() */
    /* Check if running as root */
    if (geteuid() != 0) {
        fprintf(stderr, "%s needs root privileges. Please run via sudo or as root user.\n", 
                program_name);
        exit(EXIT_PERMISSION_DENIED);
    }

    /* Argument variables */
    const char *exe = NULL;
    const char *path = NULL;
    unsigned short perclimit = 0;
    unsigned short pid_ok = 0;
    unsigned short process_ok = 0;
    unsigned short limit_ok = 0;
    unsigned short logpath_ok = 0;
    long parsed_val;

    do {
        next_option = getopt_long(argc, argv, short_options, long_options, NULL);

        switch (next_option) {
            case 'p':
                /* Replace atoi() with strtol() and error checking */
                if (!safe_strtol(optarg, &parsed_val, 1, 4194303)) {
                    fprintf(stderr, "Error: Invalid PID value '%s'\n", optarg);
                    print_usage(stderr, EXIT_CONFIG_ERROR);
                }
                pid = (unsigned int)parsed_val;
                pid_ok = 1;
                break;
            case 'e':
                exe = optarg;
                process_ok = 1;
                break;
            case 'P':
                path = optarg;
                process_ok = 1;
                break;
            case 'l':
                /* Replace atoi() with strtol() and error checking */
                if (!safe_strtol(optarg, &parsed_val, 0, 100)) {
                    fprintf(stderr, "Error: Invalid limit value '%s' (must be 0-100)\n", optarg);
                    print_usage(stderr, EXIT_CONFIG_ERROR);
                }
                perclimit = (unsigned short)parsed_val;
                limit_ok = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'z':
                lazy = 1;
                break;
            case 'd':
                daemonize = 1;
                break;
            case 'L':
                /* Add path validation for logpath */
                if (!validate_logpath(optarg)) {
                    fprintf(stderr, "Error: Invalid logpath '%s' (must be absolute path, no '..')\n", 
                            optarg);
                    print_usage(stderr, EXIT_CONFIG_ERROR);
                }
                logpath = optarg;
                logpath_ok = 1;
                break;
            case 'f':
                force = 1;
                break;
            case 'h':
                print_usage(stdout, EXIT_SUCCESS_CODE);
                break;
            case '?':
                print_usage(stdout, EXIT_CONFIG_ERROR);
                break;
            case -1:
                break;
            default:
                abort();
        }

    } while (next_option != -1);

    if (!process_ok && !pid_ok) {
        printf("Error: You must specify a target process\n");
        print_usage(stderr, EXIT_CONFIG_ERROR);
    }
    if ((exe != NULL && path != NULL) || (pid_ok && (exe != NULL || path != NULL))) {
        printf("Error: You must specify exactly one target process\n");
        print_usage(stderr, EXIT_CONFIG_ERROR);
    }
    if (!limit_ok) {
        printf("Error: You must specify a CPU limit\n");
        print_usage(stderr, EXIT_CONFIG_ERROR);
    }

    float limit = perclimit / 100.0;
    if (limit < 0 || limit > 1) {
        printf("Error: Limit must be in the range 0-100\n");
        print_usage(stderr, EXIT_CONFIG_ERROR);
    }

    if (1 == logpath_ok) {
        pLogFileFD = fopen(logpath, "a");
        if (NULL == pLogFileFD) {
            printf("Error: Cannot open/write logging path file '%s': %s\n", 
                   logpath, strerror(errno));
            print_usage(stderr, EXIT_CONFIG_ERROR);
        }

        fclose(pLogFileFD);
    }

    /* Check live pid */
    if (0 == isLivePID(pid)) {
        fprintf(stderr, "PID %d is not running. Please check the PID.\n", pid);
        exit(EXIT_NO_PROCESS);
    }

    /* Check multi run */
    checkExistsRunOnSem();

    pid_me = (unsigned int)getpid();
    if (1 == daemonize) {
        setDaemonize();
        pid_me = (unsigned int)getpid();
    }

    /* Set resource limits */
    set_resource_limits();

    /* Time quantum in microseconds. It's split into a working period and a sleeping one */
    unsigned int period = 100000;  /* 100ms */

    struct timespec twork, tsleep;  /* Working and sleeping intervals */
    memset(&twork, 0, sizeof(struct timespec));
    memset(&tsleep, 0, sizeof(struct timespec));

    /* Set lock */
    setRunOnSem();

    fprintf(stdout, "TARGET:%d, ME:%d, LIMIT:%d\n", pid, pid_me, perclimit);
    fflush(stdout);
    close(1);  /* Closing stdout */

    setLogging("Starting TARGET:%d, ME:%d, LIMIT:%d", pid, pid_me, perclimit);

    /* Fix: Replace signal() with sigaction() for better signal handling */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = setQuit;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    /* Note: SIGKILL cannot be caught */

    setSyslog(LOG_INFO, 
              "Starting cpulimit - target PID: %d, limit: %d%%, lazy: %d, verbose: %d, logfile: %s",
              pid, perclimit, lazy, verbose, logpath);

wait_for_process:

    /* Look for the target process..or wait for it */
    if (exe != NULL)
        pid = getpidof(exe);
    else if (path != NULL)
        pid = getpidof(path);
    else {
        waitforpid(pid);
    }
    /* Process detected...let's play */

    /* Init compute_cpu_usage internal stuff */
    compute_cpu_usage(0, 0, NULL);
    /* Main loop counter */
    int i = 0;

    struct timespec startwork, endwork;
    long workingtime = 0;  /* Last working time in microseconds */

    if (verbose) print_caption();

    float pcpu_avg = 0;

    /* Here we should already have high priority, for time precision */
    while (1) {
        /* Check for received signals (signal handler safety) */
        if (received_signal != 0) {
            /* Let the process continue if it's stopped */
            kill(pid, SIGCONT);
            setLogging("Exiting due to signal: %d", received_signal);
            setSyslog(LOG_INFO, "Exiting cpulimit (PID: %d) due to signal %d", 
                      (int)getpid(), received_signal);
            destroyExstsRunOnSem();
            exit(EXIT_SUCCESS_CODE);
        }

        /* Estimate how much the controlled process is using the CPU in its working interval */
        struct cpu_usage cu;
        if (compute_cpu_usage(pid, workingtime, &cu) == -1) {
            setLogging("Process %d has terminated!", pid);
            if (lazy) setExit(EXIT_NO_PROCESS);
            /* Wait until our process appears */
            goto wait_for_process;
        }

        /* CPU actual usage of process (range 0-1) */
        float pcpu = cu.pcpu;
        /* Rate at which we are keeping active the process (range 0-1) */
        float workingrate = cu.workingrate;

        /* Adjust work and sleep time slices */
        if (pcpu > 0) {
            twork.tv_nsec = min(period * limit * 1000 / pcpu * workingrate, period * 1000);
        } else if (pcpu == 0) {
            twork.tv_nsec = period * 1000;
        } else if (pcpu == -1) {
            /* Not yet a valid idea of CPU usage */
            pcpu = limit;
            workingrate = limit;
            twork.tv_nsec = min(period * limit * 1000, period * 1000);
        }

        tsleep.tv_nsec = period * 1000 - twork.tv_nsec;

        /* Update average usage */
        pcpu_avg = (pcpu_avg * i + pcpu) / (i + 1);

        if (verbose && i % 10 == 0 && i > 0) {
            setLogging("%0.2f%%\t%6ld us\t%6ld us\t%0.2f%%",
                       pcpu * 100, twork.tv_nsec / 1000, tsleep.tv_nsec / 1000, workingrate * 100);
        }

        if (limit < 1 && limit > 0) {
            /* Resume process */
            if (kill(pid, SIGCONT) != 0) {
                setLogging("Process %d has terminated: %s", pid, strerror(errno));
                if (lazy) setExit(EXIT_NO_PROCESS);
                /* Wait until our process appears */
                goto wait_for_process;
            }
        }

        clock_gettime(CLOCK_REALTIME, &startwork);
        /* Fix: Check nanosleep() return value and handle EINTR */
        while (nanosleep(&twork, NULL) == -1 && errno == EINTR) {
            /* Check for signals during sleep */
            if (received_signal != 0) {
                kill(pid, SIGCONT);
                setLogging("Exiting due to signal: %d", received_signal);
                setSyslog(LOG_INFO, "Exiting cpulimit (PID: %d) due to signal %d", 
                          (int)getpid(), received_signal);
                destroyExstsRunOnSem();
                exit(EXIT_SUCCESS_CODE);
            }
            /* Continue sleeping if interrupted by non-signal */
        }
        clock_gettime(CLOCK_REALTIME, &endwork);
        workingtime = timediff(&endwork, &startwork);

        if (limit < 1) {
            /* Stop process, it has worked enough */
            if (kill(pid, SIGSTOP) != 0) {
                setLogging("Process %d has terminated: %s", pid, strerror(errno));
                if (lazy) setExit(EXIT_NO_PROCESS);
                /* Wait until our process appears */
                goto wait_for_process;
            }
            /* Fix: Check nanosleep() return value and handle EINTR */
            while (nanosleep(&tsleep, NULL) == -1 && errno == EINTR) {
                /* Check for signals during sleep */
                if (received_signal != 0) {
                    kill(pid, SIGCONT);
                    setLogging("Exiting due to signal: %d", received_signal);
                    setSyslog(LOG_INFO, "Exiting cpulimit (PID: %d) due to signal %d", 
                              (int)getpid(), received_signal);
                    destroyExstsRunOnSem();
                    exit(EXIT_SUCCESS_CODE);
                }
            }
        }
        i++;
    }
}
