# cpulimit

[![Build Status](https://github.com/torden/cpulimit/actions/workflows/go.yml/badge.svg)](https://github.com/torden/cpulimit/actions)


This CPULimit Project is not Original. it does patched a few features based on CPULimit Project (v1.1) in sf.org

```
cpulimit is written by Angelo Marletta. Thx Angelo, your project helped to my task
```

## How to Work

* Real-Time Monitoring the Target process's cpu resource usage (-p) (expressed in percentage, not in cpu time)
* If target process cpu usage is over the limit (-l), "STOP" signal send to target process.
* If target process cpu usage is under the limit (-l), "CONTINUE" signal send to stopped process.

## List of Patches 

* Support Daemonization
* Support Detecting Multiple CPU Limit working about same process
* Support Logging to SysLog
* Support Make a static binary for use on the multiple platform
* Support Few features control by command line options
* Patched very longer kernel time issue
* Patched Minor bugs and Issue

## Tested

* CnetOS(RHEL) 2, 3, 4, 6, 7, 8
* Ubuntu

## Requirement

* Linux
* gcc 3.x or Higher (gcc 2.95 not tested)
* glibc
* make

## Compile

### compile for generally

* make

```bash
# make
Removing Garbage Files..
Complete
Normal compiling..
Complete
```

### compile for generally

* make static

```bash
Static compiling..
Warnning..
Caution : Binary likely not working on Kernel 4.8.0-30-generic / Glibc 2.24 under version..
Complete
```


### compile for debugging

* make debug

```bash
Set Debug mode enviroment..
    kernel.core_pattern=/tmp/%e.core.%u
    kernel.suid_dumpable=1
    fs.suid_dumpable=1
    kernel.core_uses_pid=1
Complete
Debug compiling..
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

Moved from bitbucket.org.

According to Original Project's License.

See the this project based on CPULimit Project : http://cpulimit.sourceforge.net/
