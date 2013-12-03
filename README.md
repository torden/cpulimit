# cpulimit

---

This project is CPULimit Patch for VM's CPU resource control,Forked from cpulimit project in sf.org

See the this project based on CPULimit Project : http://cpulimit.sourceforge.net/

```
original cpulimit is written by Angelo Marletta.
```

## How to Work

* Monitoring Target process usage cpu resource (kernel time)
* If target process cpu usage is 100% over, "STOP" signal send to target process
* If target process cpu usage is 100% under, "CONTINUE" signal send to target process

## List of Patches 

* Support Daemonization
* Support Detecting Multi CPU Limit working about same process
* Support SysLog
* Support make a full static binary for use on the multi platform
* Support Some feature control command line options
* Patched very long kernel time issue
* Patched minor bug and issue

## Tested

* CentOS(RHEL) 2,3,4,6
* Ubuntu

## Requirement

* Linux
* gcc 3.x or Higher (gcc 2.95 not tested)
* glibc
* gmake

## Compile

### compile for production

* gmake

```bash
# gmake
Removing Old files..
/bin/rm -f *~ cpulimit
Complete
Normal mode compiling..
/usr/bin/gcc -o cpulimit cpulimit.c -lrt -W -Wall -O2 -g -fomit-frame-pointer -funroll-loops
Complete
```

### compile for debugging

* gmake debug

```bash
# gmake debug
Set Debug mode enviroment..
        kernel.core_pattern=/tmp/%e.core.%u
        kernel.suid_dumpable=1
        fs.suid_dumpable=1
        kernel.core_users_pid=1
Complete
Debug mode compiling..
Complete
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
* -L or --logpath : CPULImit logfile full path
* -l N or --limit=N : CPU Allow max usage(0 to 100) default value is 100%

### Optional Parameters

* -e or --exe=FILE : Executable Program File
* -P or --path=PATH : Absolute Path anem of Excutable Program File
* -v or --verbose : show control statistics
* -z or --lazy : exit if there is no suitable target process or if it was die
* -h or --help : display this help and exit
* -d or --daemonize : damonization , if it does not use, cpulimited using foreground mode
* -f or --force : forcing running mode , killing prevent process with forcing multiple run locking

## How to Use

```bash
## find target process id
# pgrep crond
1234

## run
# ./cpulimit -p 1234 -l 10 -L /tmp/cpulimit.crond.log -d
1234 is not working, please first check pid

## force running when kill -9 `cpulimit` or after system shutdown or reboot or other
# ./cpulimit -p 1234 -l 10 -L /tmp/cpulimit.crond.log -d
# ./cpulimit -p 1234 -l 10 -L /tmp/cpulimit.crond.log -d
Another CPULimit daemon working.. (PID:1825), please first check it(1)
# ./cpulimit -p 1234 -l 10 -L /tmp/cpulimit.crond.log -d -f

# pgrep cpulimit
1828

# looking cpulimit log
# tail /tmp/cpulimit.crond.log
[2013-12-03 15:07:08 KST] Starting TARGET:1234,ME:1828,LIMIT:10
```

---

Moved from bitbucket.org
According to Original Project's License
