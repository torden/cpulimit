CMD_SUDO    :=$(shell which sudo)
CMD_RM      :=$(shell which rm)
CC          :=$(shell which gcc)

all:: clean cpulimit

cpulimit: cpulimit.c
	@echo -e "\033[1;40;32mNormal mode compiling..\033[01;m"
	$(CC) -o $@ $< -lrt -W -Wall -O2 -g -fomit-frame-pointer -funroll-loops
	@echo -e "\033[1;40;36mComplete\033[01;m"

debug:: setenv debugcomp

debugcomp: cpulimit.c
	@echo -e "\033[1;40;32mDebug mode compiling..\033[01;m"
	$(CC) -o cpulimit $< -lrt -g -pg -W -Wall -O2 -pipe -fprefetch-loop-arrays -ffast-math -fforce-addr -falign-functions=4 -funroll-loops
	@echo -e "\033[1;40;36mComplete\033[01;m"

clean:
	@echo -e "\033[1;40;32mRemoving Old files..\033[01;m"
	$(CMD_RM) -f *~ cpulimit
	@echo -e "\033[1;40;36mComplete\033[01;m"

setenv::
	@echo -e "\033[1;40;32mSet Debug mode enviroment..\033[01;m"
	@$(CMD_SUDO) sysctl -e -q -w kernel.core_pattern="/tmp/%e.core.%u" kernel.suid_dumpable=1 fs.suid_dumpable=1
	@$(CMD_SUDO) sysctl -q -w kernel.core_uses_pid=1;
	@echo -e "\tkernel.core_pattern=/tmp/%e.core.%u"
	@echo -e "\tkernel.suid_dumpable=1"
	@echo -e "\tfs.suid_dumpable=1"
	@echo -e "\tkernel.core_users_pid=1"
	@echo -e "\033[1;40;36mComplete\033[01;m"
