cpulimit
==============================

This project is cpulimit original project forked and patched for vm cpu resource control
See the this project based on CPULimit Project : http://cpulimit.sourceforge.net/

## How to Work
 * Monitoring Target process usage cpu resource (kernel time)
 * if target process cpu usage is 100% over, "STOP" signal send to target process
 * if target process cpu usage is 100% under, "CONTINUE" signal send to target process
 
## Patch Log
 * Support Daemonization
 * Support Detecting Multi CPU Limit working about same process
 * Support SysLog
 * Support make a full static binary for use on the multi platform
 * Support Some feature control command line options
 * Patched Hurge kernel time issue
 * Patched minor bug and issue

## Tested
 * Some hosting service using this for cpu usage limitation to high load kvm processor in vm hosting servers
 * CentOS(RHEL) 2 or higher

## Requirement
 * Linux (CentOS 4/5/6 Tested)
 * gcc 3.x or Higher (gcc 2.95 not tested)
 * glibc
 * gmake
 
## Preparing Compile
 * git clone https://torden@bitbucket.org/torden/cpulimit.git
 * cd cpulimit
 
### compile for production
 * gmake
```bash
[root@localhost cpulimit]# gmake
Removing Old files..
/bin/rm -f *~ cpulimit
Complete
Normal mode compiling..
/usr/bin/gcc -o cpulimit cpulimit.c -lrt -W -Wall -O2 -g -fomit-frame-pointer -funroll-loops
Complete
[root@localhost cpulimit]# 
```

### compile for debuging
 * gmake debug
```bash
[root@localhost cpulimit]# gmake debug
Set Debug mode enviroment..
        kernel.core_pattern=/tmp/%e.core.%u
        kernel.suid_dumpable=1
        fs.suid_dumpable=1
        kernel.core_users_pid=1
Complete
Debug mode compiling..
/usr/bin/gcc -o cpulimit cpulimit.c -lrt -g -pg -W -Wall -O2 -pipe -fprefetch-loop-arrays -ffast-math -fforce-addr -falign-functions=4 -funroll-loops
Complete
[root@localhost cpulimit]# 
```
 

## Support Parameters

```bash
Usage: cpulimit TARGET [OPTIONS...]
  TARGET must be exactly one of these:
      -p, --pid=N        pid of the process
      -e, --exe=FILE     name of the executable program file
      -P, --path=PATH    absolute path name of the executable program file
      -L, --logpath      logfile path
   OPTIONS
      -l, --limit=N      percentage of cpu allowed from 0 to 100 (mandatory)
      -v, --verbose      show control statistics
      -z, --lazy         exit if there is no suitable target process, or if it die
      -h, --help         display this help and exit
      -d, --daemonize    damonization
      -f, --fore         force run, killing prevent process with forcing muti run lock
      
```
### Madantory Parameters
 * -p or --pid=N : Target Process ID
 * -e or --exe=FILE : Executable Program File
 * -P or --path=PATH : Absolute Path anem of Excutable Program File
 * -L or --logpath : CPULImit logfile full path
 
### Optional Parameters
 * -l N or --limit=N : CPU Allow max usage(0 to 100) default value is 100%
 * -v or --verbose : show control statistics
 * -z or --lazy : exit if there is no suitable target process or if it was die
 * -h or --help : display this help and exit
 * -d or --daemonize : damonization , if it not use, cpulimited using forground mode
 * -f or --force : forcing running mode , killing prevent process with forcing multiple run locking
 
