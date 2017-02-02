# @author : torden <https://github.com/torden/>
CMD_SUDO    	:=$(shell which sudo)
CMD_RM      	:=$(shell which rm)
CMD_CC      	:=$(shell which gcc)
CMD_STRIP		:=$(shell which strip)
CMD_SYSCTL		:=$(shell which sysctl)
CMD_LDD			:=$(shell which ldd)
CMD_TEST		:=$(shell which test)
CFLAGS			:=-O2 -g -lrt -W -Wall -lpthread
INFOV_GLIB		:=`ldd --version | head -n 1 | awk -F') ' '{print $$2}'`
INFOV_KERNEL 	:=$(shell uname -r)

BINARY_NAME	:=cpulimit
OS_DIST		:=$(shell lsb_release -is)
ifeq ($(OS_DIST),Ubuntu)
	ECHO=echo
else
	ECHO=echo -e
endif

all:: clean cpulimit

cpulimit: cpulimit.c
	@$(ECHO) "\033[1;40;32mNormal compiling..\033[01;m"
	@$(CMD_CC) -o $(BINARY_NAME) $< $(CFLAGS) -fomit-frame-pointer -funroll-loops 
	@$(ECHO) "\033[1;40;36mComplete\033[01;m"

static: cpulimit.c
	@$(ECHO) "\033[1;40;32mStatic compiling..\033[0m"
	@$(ECHO) "\033[1;40;31mWarnning..\033[0m"
	@$(ECHO) "\033[1;40;33mCaution : Binary likely not working on Kernel $(INFOV_KERNEL) / Glibc $(INFOV_GLIB) under version..\033[0m"
	@$(CMD_CC) -o $(BINARY_NAME) $< $(CFLAGS) -pipe -fprefetch-loop-arrays -ffast-math -fforce-addr -falign-functions=4 -funroll-loops -static -static-libgcc 
	@$(ECHO) "\033[1;40;36mComplete\033[0m"

strip: 
	@$(ECHO) "\033[1;40;32mDiscard symbols..\033[0m"
	@$(CMD_TEST) -e $(BINARY_NAME) && $(CMD_STRIP) -s $(BINARY_NAME)  || $(ECHO) "\033[1;40;31mfirst of all, build a binary (ex: make or make debugcomp or make static)..\033[0m" 
	@$(ECHO) "\033[1;40;36mComplete\033[0m"

debug:: setenv debugcomp

debugcomp: cpulimit.c
	@$(ECHO) "\033[1;40;32mDebug compiling..\033[0m"
	@$(CMD_CC) -o $(BINARY_NAME) $< $(CFLAGS) -pg -pipe -fprefetch-loop-arrays -ffast-math -fforce-addr -falign-functions=4 -funroll-loops
	@$(ECHO) "\033[1;40;36mComplete\033[0m"

clean:
	@$(ECHO) "\033[1;40;32mRemoving Garbage Files..\033[0m"
	@$(CMD_RM) -f *~ $(BINARY_NAME) *.core *.swp
	@$(ECHO) "\033[1;40;36mComplete\033[0m"

setenv::
	@$(ECHO) "\033[1;40;32mSet Debug mode enviroment..\033[0m"
	@$(CMD_SUDO) $(CMD_SYSCTL) -e -q -w kernel.core_pattern="/tmp/%e.core.%u" kernel.suid_dumpable=1 fs.suid_dumpable=1
	@$(CMD_SUDO) $(CMD_SYSCTL) -q -w kernel.core_uses_pid=1;
	@$(ECHO) "\tkernel.core_pattern=/tmp/%e.core.%u"
	@$(ECHO) "\tkernel.suid_dumpable=1"
	@$(ECHO) "\tfs.suid_dumpable=1"
	@$(ECHO) "\tkernel.core_uses_pid=1"
	@$(ECHO) "\033[1;40;36mComplete\033[0m"
