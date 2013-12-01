/**
 * below information are CPU Limit original copyright and header
 * Author:  Angelo Marletta
 * Date:    26/06/2005
 * Version: 1.1
 * Last version at: http://marlon80.interfree.it/cpulimit/index.html
 */

/**
 * Patched : Torden Cho  <ioemen@gmail.com>
 * @(#)File:           $RCSfile: stderr.c,v $
 * @(#)Version:        $Revision: 8.29 $
 * @(#)Last changed:   $Date: 2008/06/02 13:00:00 $
 * @(#)Author(patched by torden <https://github.com/torden/> code): Torden Cho
 */
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
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PGNAME "CPULimit"
#define VERSION "1.0.1-p1"
#define MUTI_RUN_DETECT_PREFIX_SEM_NAME "CPULIMIT"

//kernel time resolution (inverse of one jiffy interval) in Hertz
//i don't know how to detect it, then define to the default (not very clean!)
#define HZ 100

//some useful macro
#define min(a,b) (a<b?a:b)
#define max(a,b) (a>b?a:b)


//pid of the controlled process
unsigned int pid=0;
//executable file name
char *program_name;
//verbose mode
unsigned short verbose=0;
//lazy mode
unsigned short lazy=0;

//daemonize mode
unsigned short daemonize=0;
unsigned short daemond=0;

//log file
FILE *pLogFileFD;

//my forked pid
unsigned int pid_me;

//logfile path
const char *logpath=NULL;

//run user information
struct passwd *run_userinfo;

//check multi run
char semName[256] ={0x00,};
char *psemName = semName;
sem_t *duplCheckSem;

//force mode
unsigned short force=0;

//reverse byte search
void *memrchr(const void *s, int c, size_t n);

//return ta-tb in microseconds (no overflow checks!)
inline long timediff(const struct timespec *ta,const struct timespec *tb) {
    unsigned long us = (ta->tv_sec-tb->tv_sec)*1000000 + (ta->tv_nsec/1000 - tb->tv_nsec/1000);
    return us;
}

unsigned short isLivePID(unsigned int ckpid) {

    if(0 == kill(ckpid, 0)) {
        return 1;
    } else if(ESRCH == errno) {
        return 0;
    } else {
        return 0;
    }
}

void checkExistsRunOnSem(void) {

    int cpulimitPid = 0;
    snprintf(psemName, sizeof semName, "%s_%d", MUTI_RUN_DETECT_PREFIX_SEM_NAME, pid);
    duplCheckSem = sem_open(psemName, O_RDONLY);
    if(SEM_FAILED != duplCheckSem) {
        sem_getvalue(duplCheckSem, &cpulimitPid);
        if(0 == cpulimitPid) {
            fprintf(stdout, "System failure, Can not read named sem : %s\n", psemName);
            exit(1);
        }

        if(0 < cpulimitPid && 1 == force) {
            if(0 != kill(cpulimitPid, SIGKILL)) {
                fprintf(stdout, "System failure, Can not running with force mode, target pid : %d\n", cpulimitPid);
                exit(1);
            } else {
                sem_close(duplCheckSem);
                sem_unlink(psemName);
            }
        } else {
            fprintf(stdout, "Another CPULimit daemon working.. (PID:%d), please first check it\n", cpulimitPid);
            exit(1);
        }
    } else {
        sem_close(duplCheckSem);
    }
}

void setRunOnSem(void) {

    int cpulimitPid = 0;
    snprintf(psemName, sizeof semName, "%s_%d", MUTI_RUN_DETECT_PREFIX_SEM_NAME, pid);
    duplCheckSem = sem_open(psemName, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, pid_me);
    if(SEM_FAILED == duplCheckSem) {
        if(EEXIST == errno) {
            fprintf(stdout, "Another CPULimit daemon working.. (PID:%d), please first check it\n", cpulimitPid);
            exit(1);
        } else {
            fprintf(stdout, "System failure, Can not running with force mode\n");
            exit(1);
        }
    }
}

void destroyExstsRunOnSem(void) {
    sem_close(duplCheckSem);
    sem_unlink(psemName);
}
void setDaemonize() {

    pid_t pidthis = fork();

    if(0 > pidthis) {
        exit(0);
    } else if (pidthis!= 0) {
        exit(0);
    }

    close(0);
    //close(1);
    close(2);
    setsid();

    daemond=1;
}

void setSyslog(unsigned short logtype, const char *logfmt, ...) {

/*
LOG_EMERG   0   system is unusable
LOG_ALERT   1   action must be taken immediately
LOG_CRIT    2   critical conditions
LOG_ERR     3   error conditions
LOG_WARNING 4   warning conditions
LOG_NOTICE  5   normal but significant condition
LOG_INFO    6   informational
LOG_DEBUG   7   debug-level messages
*/
    if(7 >= logtype) {
        openlog("cpulimit", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

        char msgbuf[8088] = {0x00,};
        char *pmsgbuf = msgbuf;

        char msgbody[7000] = {0x00,};
        char *pmsgbody = msgbody;

        va_list args;
        va_start(args, logfmt);
        vsnprintf(pmsgbody, 7000, logfmt, args);
        va_end(args);

        snprintf(pmsgbuf, 8088, "%s\n", pmsgbody);

        syslog(logtype, pmsgbuf);
        closelog();
    }
}

int setLogging(const char *logfmt, ... ) {

    if(NULL == logpath) {
        return 1;
    }

    time_t rawtime;
    struct tm * timeinfo;
    char msgbuf[8088];
    char *pmsgbuf;

    pmsgbuf = msgbuf;
    memset(pmsgbuf, 0x00, 8088);

    time ( &rawtime );
    timeinfo = localtime ( &rawtime );

    //log filename
    char fn[30] = {0x00,};
    char *pfn= fn;
    strftime(pfn,30, "%Y-%m-%d-%H", timeinfo);

    //logging time
    char tbuf[30] = {0x00,};
    char *ptbuf = tbuf;
    strftime(ptbuf,30, "%Y-%m-%d %H:%M:%S %Z", timeinfo);

    //user logmessage
    char msgbody[7000] = {0x00,};
    char *pmsgbody = msgbody;
    va_list args;
    va_start(args, logfmt);
    vsnprintf(pmsgbody, 7000, logfmt, args);
    va_end(args);

    //mergen full
    snprintf(pmsgbuf, 8088, "[%s] %s\n", ptbuf, pmsgbody);

    pLogFileFD = fopen(logpath,"a");
    if(NULL == pLogFileFD) {
        setSyslog(LOG_ERR, "can not open logfile : %s", strerror(errno));
        return 0;
    }

    size_t ret = fwrite(pmsgbuf, strlen(pmsgbuf), 1, pLogFileFD);

    if(1 > ret) {
        setSyslog(LOG_ERR, "can not write msg to logfile : %s", strerror(errno));
        return 0;
    }

    fclose(pLogFileFD);
    return 1;
}

void setExit(int returCode) {
    if(1 == daemond) {
            kill(pid,SIGCONT);
    }
    setLogging("Exit.");
    setSyslog(LOG_INFO, "exit cpulimit : %d", (int)getpid());
    destroyExstsRunOnSem();
    exit(returCode);
}


int waitforpid(int pid) {

    //switch to low priority
    if (setpriority(PRIO_PROCESS,getpid(),19)!=0) {
        setLogging("Warning: cannot renice");
    }

    int i = 0;

    while(1) {

        DIR *dip;
        struct dirent *dit;

        //open a directory stream to /proc directory
        if ((dip = opendir("/proc")) == NULL) {
            setSyslog(LOG_ERR, "can not open /proc directory : %s", strerror(errno));
            return -1;
        }

        //read in from /proc and seek for process dirs
        while ((dit = readdir(dip)) != NULL) {
            //get pid
            if (pid==atoi(dit->d_name)) {
                //pid detected
                if (kill(pid,SIGSTOP)==0 &&  kill(pid,SIGCONT)==0) {
                    //process is ok!
                    goto done;
                } else {
                    setLogging("Error: Process %d detected, but you don't have permission to control it",pid);
                }
            }
        }

        //close the dir stream and check for errors
        if (closedir(dip) == -1) {
            setSyslog(LOG_ERR, "can not close proc directory : %s", strerror(errno));
            return -1;
        }

        //no suitable target found
        if (i++==0) {
            if (lazy) {
                setLogging("No process found");
                setExit(2);
            } else {
                setLogging("Warning: no target process found. Waiting for it...");
            }
        }
        //sleep for a while
        sleep(2);
    }

done:
    //now set high priority, if possible
    if (setpriority(PRIO_PROCESS,getpid(),-20)!=0) {
        setLogging("Warning: cannot renice.\nTo work better you should run this program as root.");
    }
    return 0;
}

//this function periodically scans process list and looks for executable path names
//it should be executed in a low priority context, since precise timing does not matter
//if a process is found then its pid is returned
//process: the name of the wanted process, can be an absolute path name to the executable file
//         or simply its name
//return: pid of the found process
int getpidof(const char *process) {

    //set low priority
    if (setpriority(PRIO_PROCESS,getpid(),19)!=0) {
        setLogging("Warning: cannot renice");
    }

    char exelink[20];
    char exepath[PATH_MAX+1];
    unsigned short int pid=0;
    int i=0;

    while(1) {

        DIR *dip;
        struct dirent *dit;

        //open a directory stream to /proc directory
        if ((dip = opendir("/proc")) == NULL) {
            setSyslog(LOG_ERR, "can not open proc directory : %s", strerror(errno));
            return -1;
        }

        //read in from /proc and seek for process dirs
        while ((dit = readdir(dip)) != NULL) {
            //get pid
            pid=atoi(dit->d_name);
            if (pid>0) {
                sprintf(exelink,"/proc/%d/exe",pid);
                unsigned int size=readlink(exelink,exepath,sizeof(exepath));
                if (size>0) {
                    int found=0;
                    if (process[0]=='/' && strncmp(exepath,process,size)==0 && size==strlen(process)) {
                        //process starts with / then it's an absolute path
                        found=1;
                    }
                    else {
                        //process is the name of the executable file
                        if (strncmp(exepath+size-strlen(process),process,strlen(process))==0) {
                            found=1;
                        }
                    }
                    if (found==1) {
                        if (kill(pid,SIGSTOP)==0 &&  kill(pid,SIGCONT)==0) {
                            //process is ok!
                            goto done;
                        } else {
                            setLogging("Error: Process %d detected, but you don't have permission to control it",pid);
                        }
                    }
                }
            }
        }

        //close the dir stream and check for errors
        if (closedir(dip) == -1) {
            setSyslog(LOG_ERR, "can not close proc directory : %s", strerror(errno));
            return -1;
        }

        //no suitable target found
        if (i++==0) {
            if (lazy) {
                setLogging("No process found");
                setExit(2);
            } else {
                setLogging("Warning: no target process found. Waiting for it...");
            }
        }

        //sleep for a while
        sleep(2);
    }

done:
    setLogging("Process %d detected",pid);
    //now set high priority, if possible
    if (setpriority(PRIO_PROCESS,getpid(),-20)!=0) {
        setLogging("Warning: cannot renice.\nTo work better you should run this program as root.");
    }
    return pid;
}

//SIGINT and SIGTERM signal handler
void setQuit(int sig) {

    //let the process continue if it's stopped
    kill(pid,SIGCONT);
    setLogging("Exit. Signal : %d", sig);
    setSyslog(LOG_INFO, "exit cpulimit : %d", (int)getpid());
    destroyExstsRunOnSem();
    exit(0);
}

//get jiffies count from /proc filesystem
long getjiffies(int pid) {
    static char stat[20];
    static char buffer[1024];
    sprintf(stat,"/proc/%d/stat",pid);
    FILE *f=fopen(stat,"r");
    if (f==NULL) {
        setLogging("Can not open /proc/%d/stat", pid);
        return -1;
    }
    fgets(buffer,sizeof(buffer),f);
    fclose(f);
    char *p=buffer;
    p=memchr(p+1,')',sizeof(buffer)-(p-buffer));

    int sp=12;
    while (sp--)
            p=memchr(p+1,' ',sizeof(buffer)-(p-buffer));
    //user mode jiffies
    long utime=atol(p+1);
    p=memchr(p+1,' ',sizeof(buffer)-(p-buffer));
    //kernel mode jiffies
    long ktime=atol(p+1);
    return utime+ktime;
}

//process instant photo
struct process_screenshot {
    struct timespec when;   //timestamp
    long jiffies;   //jiffies count of the process
    int cputime;    //microseconds of work from previous screenshot to current
};

//extracted process statistics
struct cpu_usage {
    float pcpu;
    float workingrate;
};

//this function is an autonomous dynamic system
//it works with static variables (state variables of the system), that keep memory of recent past
//its aim is to estimate the cpu usage of the process
//to work properly it should be called in a fixed periodic way
//perhaps i will put it in a separate thread...
int compute_cpu_usage(int pid,int last_working_quantum,struct cpu_usage *pusage) {
    #define MEM_ORDER 10
    //circular buffer containing last MEM_ORDER process screenshots
    static struct process_screenshot ps[MEM_ORDER];
    //the last screenshot recorded in the buffer
    static int front=-1;
    //the oldest screenshot recorded in the buffer
    static int tail=0;

    if (pusage==NULL) {
        //reinit static variables
        front=-1;
        tail=0;
        return 0;
    }

    //let's advance front index and save the screenshot
    front=(front+1)%MEM_ORDER;
    long j=getjiffies(pid);
    if (j>=0) {
        ps[front].jiffies=j;
    } else {
        setLogging("Can not Get jifeeies %d" , pid);
        return -1;      //error: pid does not exist
    }
    clock_gettime(CLOCK_REALTIME,&(ps[front].when));
    ps[front].cputime=last_working_quantum;

    //buffer actual size is: (front-tail+MEM_ORDER)%MEM_ORDER+1
    int size=(front-tail+MEM_ORDER)%MEM_ORDER+1;

    if (size==1) {
        //not enough samples taken (it's the first one!), return -1
        pusage->pcpu=-1;
        pusage->workingrate=1;
        return 0;
    } else {
        //now we can calculate cpu usage, interval dt and dtwork are expressed in microseconds
        long dt=timediff(&(ps[front].when),&(ps[tail].when));
        long dtwork=0;
        int i=(tail+1)%MEM_ORDER;
        int max=(front+1)%MEM_ORDER;
        do {
            dtwork+=ps[i].cputime;
            i=(i+1)%MEM_ORDER;
        } while (i!=max);
        long used=ps[front].jiffies-ps[tail].jiffies;
        float usage=(used*1000000.0/HZ)/dtwork;
        pusage->workingrate=1.0*dtwork/dt;
        pusage->pcpu=usage*pusage->workingrate;
        if (size==MEM_ORDER)
            tail=(tail+1)%MEM_ORDER;
        return 0;
    }
    #undef MEM_ORDER
}

void print_caption() {
    setLogging("%%CPU\twork quantum\tsleep quantum\tactive rate");
}

void print_usage(FILE *stream,int exit_code) {

    fprintf(stream, "Usage: %s TARGET [OPTIONS...]\n",program_name);
    fprintf(stream, "   TARGET must be exactly one of these:\n");
    fprintf(stream, "      -p, --pid=N        pid of the process\n");
    fprintf(stream, "      -e, --exe=FILE     name of the executable program file\n");
    fprintf(stream, "      -P, --path=PATH    absolute path name of the executable program file\n");
    fprintf(stream, "      -L, --logpath      logfile path\n");
    fprintf(stream, "   OPTIONS\n");
    fprintf(stream, "      -l, --limit=N      percentage of cpu allowed from 0 to 100 (mandatory)\n");
    fprintf(stream, "      -v, --verbose      show control statistics\n");
    fprintf(stream, "      -z, --lazy         exit if there is no suitable target process, or if it dies\n");
    fprintf(stream, "      -h, --help         display this help and exit\n");
    fprintf(stream, "      -d, --daemonize    damonization\n");
    fprintf(stream, "      -f, --fore         force run, killing prevent process with forcing muti run lock\n");
    exit(exit_code);
}

int main(int argc, char **argv) {

    //get program name
    char *p=(char*)memrchr(argv[0],(unsigned int)'/',strlen(argv[0]));
    program_name = p==NULL?argv[0]:(p+1);

    // check pertmit on run user
    run_userinfo = getpwuid(getuid());
    if(NULL == run_userinfo) {
        fprintf(stderr, "%s can not get run user information\n", program_name);
        exit(1);
    }

    if(0 != (int)run_userinfo->pw_uid) {
        fprintf(stderr, "%s need to root pertmit, please run via sudo or root user\n", program_name);
        exit(1);
    }

    //parse arguments
    int next_option;
    /* A string listing valid short options letters. */
    const char* short_options="p:e:P:l:vzhdL:f";
    /* An array describing valid long options. */
    const struct option long_options[] = {
        { "pid", 0, NULL, 'p' },
        { "exe", 1, NULL, 'e' },
        { "path", 0, NULL, 'P' },
        { "limit", 0, NULL, 'l' },
        { "verbose", 0, NULL, 'v' },
        { "lazy", 0, NULL, 'z' },
        { "help", 0, NULL, 'h' },
        { "daemonize", 0, NULL, 'd' },
        { "logpath", 0, NULL, 'L' },
        { "force", 0, NULL, 'f' },
        { NULL, 0, NULL, 0 }
    };

    //argument variables
    const char *exe=NULL;
    const char *path=NULL;
    unsigned short perclimit=0;
    unsigned short pid_ok=0;
    unsigned short process_ok=0;
    unsigned short limit_ok=0;
    unsigned short logpath_ok=0;

    do {
        next_option = getopt_long (argc, argv, short_options,long_options, NULL);

        switch(next_option) {
            case 'p':
                pid=atoi(optarg);
                pid_ok=1;
                break;
            case 'e':
                exe=optarg;
                process_ok=1;
                break;
            case 'P':
                path=optarg;
                process_ok=1;
                break;
            case 'l':
                perclimit=atoi(optarg);
                limit_ok=1;
                break;
            case 'v':
                verbose=1;
                break;
            case 'z':
                lazy=1;
                break;
            case 'd':
                daemonize=1;
                break;
            case 'L':
                logpath=optarg;
                logpath_ok=1;
                break;
            case 'f':
                force=1;
                break;
            case 'h':
                print_usage (stdout, 1);
                break;
            case '?':
                print_usage (stdout, 1);
                break;
            case -1:
                break;
            default:
                abort();
        }

    } while(next_option != -1);

    if (!process_ok && !pid_ok) {
       printf("Error: You must specify a target process\n");
       print_usage (stderr, 1);
       exit(1);
    }
    if ((exe!=NULL && path!=NULL) || (pid_ok && (exe!=NULL || path!=NULL))) {
       printf("Error: You must specify exactly one target process\n");
       print_usage (stderr, 1);
       exit(1);
    }
    if (!limit_ok) {
       printf("Error: You must specify a cpu limit\n");
       print_usage (stderr, 1);
       exit(1);
    }

    float limit=perclimit/100.0;
    if (limit<0 || limit >1) {
       printf("Error: limit must be in the range 0-100\n");
       print_usage (stderr, 1);
       exit(1);
    }


    if (1 == logpath_ok ) {
        pLogFileFD = fopen(logpath,"a");
        if(NULL == pLogFileFD) {
            printf("Error: can not open/write logging path file\n");
            print_usage (stderr, 1);
            exit(1);
        }

        fclose(pLogFileFD);
    }

    //check live pid
    if(0 == isLivePID(pid)) {
        fprintf(stderr, "%d is not working, please first check pid\n", pid);
        exit(1);
    }

    //check muti dun
    checkExistsRunOnSem();


    //time quantum in microseconds. it's splitted in a working period and a sleeping one
    unsigned int period=100000;
    struct timespec twork,tsleep;   //working and sleeping intervals
    memset(&twork,0,sizeof(struct timespec));
    memset(&tsleep,0,sizeof(struct timespec));

    if(1 == daemonize) {
        setDaemonize();
        pid_me = (unsigned int)getpid();
    }

    //set lock
    setRunOnSem();

    fprintf(stdout,"TARGET:%d,ME:%d,LIMIT:%d\n",pid, pid_me,perclimit);
    fflush(stdout);
    close(1);//closeing stdout

    setLogging("Starting TARGET:%d,ME:%d,LIMIT:%d",pid, pid_me,perclimit);

    //parameters are all ok!
    signal(SIGINT,setQuit);
    signal(SIGTERM,setQuit);
    signal(SIGQUIT,setQuit);
    signal(SIGABRT,setQuit);
    signal(SIGKILL,setQuit);

    setSyslog(LOG_INFO, "starting cpulimit / target pid : %d, limit : %d, lazy : %d, verbose : %d, logfile : %s", pid, perclimit, lazy, verbose, logpath);

wait_for_process:

    //look for the target process..or wait for it
    if (exe!=NULL)
        pid=getpidof(exe);
    else if (path!=NULL)
        pid=getpidof(path);
    else {
        waitforpid(pid);
    }
    //process detected...let's play

    //init compute_cpu_usage internal stuff
    compute_cpu_usage(0,0,NULL);
    //main loop counter
    int i=0;

    struct timespec startwork,endwork;
    long workingtime=0;             //last working time in microseconds

    if (verbose) print_caption();

    float pcpu_avg=0;


    //here we should already have high priority, for time precision
    while(1) {

        //estimate how much the controlled process is using the cpu in its working interval
        struct cpu_usage cu;
        if (compute_cpu_usage(pid,workingtime,&cu)==-1) {
            setLogging("Process %d dead!",pid);
            if (lazy) setExit(2);
            //wait until our process appears
            goto wait_for_process;
        }

        //cpu actual usage of process (range 0-1)
        float pcpu=cu.pcpu;
        //rate at which we are keeping active the process (range 0-1)
        float workingrate=cu.workingrate;

        //adjust work and sleep time slices
        if (pcpu>0) {
            twork.tv_nsec=min(period*limit*1000/pcpu*workingrate,period*1000);
        }
        else if (pcpu==0) {
            twork.tv_nsec=period*1000;
        }
        else if (pcpu==-1) {
            //not yet a valid idea of cpu usage
            pcpu=limit;
            workingrate=limit;
            twork.tv_nsec=min(period*limit*1000,period*1000);
        }

        tsleep.tv_nsec=period*1000-twork.tv_nsec;

        //update average usage
        pcpu_avg=(pcpu_avg*i+pcpu)/(i+1);

        if (verbose && i%10==0 && i>0) {
            setLogging("%0.2f%%\t%6ld us\t%6ld us\t%0.2f%%",pcpu*100,twork.tv_nsec/1000,tsleep.tv_nsec/1000,workingrate*100);
        }

        if (limit<1 && limit>0) {
            //resume process
            if (kill(pid,SIGCONT)!=0) {
                setLogging("Process %d dead!",pid);
                if (lazy) setExit(2);
                //wait until our process appears
                goto wait_for_process;
            }
        }

        clock_gettime(CLOCK_REALTIME,&startwork);
        nanosleep(&twork,NULL);         //now process is working
        clock_gettime(CLOCK_REALTIME,&endwork);
        workingtime=timediff(&endwork,&startwork);

        if (limit<1) {
            //stop process, it has worked enough
            if (kill(pid,SIGSTOP)!=0) {
                setLogging("Process %d dead!",pid);
                if (lazy) setExit(2);
                //wait until our process appears
                goto wait_for_process;
            }
            nanosleep(&tsleep,NULL);        //now process is sleeping
        }
        i++;
    }
}
