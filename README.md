cpulimit
========

This project is cpulimit original project forked and patched for vm cpu resource control
See the this project based on CPULimit Project : http://cpulimit.sourceforge.net/

* How to Work
 - Monitoring Target process usage cpu resource (kernel time)
 - if target process cpu usage is 100% over, "STOP" signal to target process
 - if target process cpu usage is 100% under, "CONTINUE" signal to target process
 
* Patch Log
 - Support Daemonization
 - Support Detecting Multi CPU Limit working about same process
 - Support SysLog
 - Support make a full static binary for use on the multi platform
 - Support Some feature control command line options
 - Patched Hurge kernel time issue
 - Patched minor bug and issue

* Tested
 - Some hosting service using this for cpulimit to high load kvm processing in vm hosting
 - CentOS(RHEL) 2 or higher

* Requirement
 - 

* How to Use
 
