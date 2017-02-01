# @author : torden <https://github.com/torden/>
CMD_SUDO    :=$(shell which sudo)
CMD_RM      :=$(shell which rm)
CC          :=$(shell which gcc)
CFLAGS		:=-O2 -g -lrt -W -Wall -lpthread
OS_DIST		:=$(shell lsb_release -is)
ifeq ($(OS_DIST),Ubuntu)
	ECHO=echo
else
	ECHO=echo -e
endif

all:: clean cpulimit

cpulimit: cpulimit.c
	@$(ECHO) "\033[1;40;32mNormal mode compiling..\033[01;m"
	@$(CC) -o $@ $< $(CFLAGS) -fomit-frame-pointer -funroll-loops
	@$(ECHO) "\033[1;40;36mComplete\033[01;m"

debug:: setenv debugcomp

debugcomp: cpulimit.c
	@$(ECHO) "\033[1;40;32mDebug mode compiling..\033[01;m"
	@$(CC) -o cpulimit $< $(CFLAGS) -pg -pipe -fprefetch-loop-arrays -ffast-math -fforce-addr -falign-functions=4 -funroll-loops
	@$(ECHO) "\033[1;40;36mComplete\033[01;m"

clean:
	@$(ECHO) "\033[1;40;32mRemoving Garbage Files..\033[01;m"
	@$(CMD_RM) -f *~ cpulimit *.core *.swp
	@$(ECHO) "\033[1;40;36mComplete\033[01;m"

setenv::
	@$(ECHO) "\033[1;40;32mSet Debug mode enviroment..\033[01;m"
	@$(CMD_SUDO) sysctl -e -q -w kernel.core_pattern="/tmp/%e.core.%u" kernel.suid_dumpable=1 fs.suid_dumpable=1
	@$(CMD_SUDO) sysctl -q -w kernel.core_uses_pid=1;
	@$(ECHO) "\tkernel.core_pattern=/tmp/%e.core.%u"
	@$(ECHO) "\tkernel.suid_dumpable=1"
	@$(ECHO) "\tfs.suid_dumpable=1"
	@$(ECHO) "\tkernel.core_users_pid=1"
	@$(ECHO) "\033[1;40;36mComplete\033[01;m"
