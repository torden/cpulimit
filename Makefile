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

help:
	@$(ECHO) "\033[1;40;33mAvailable targets:\033[0m"
	@$(ECHO) "  \033[1;32mall\033[0m       - Build cpulimit (default target)"
	@$(ECHO) "  \033[1;32mcpulimit\033[0m   - Build cpulimit binary"
	@$(ECHO) "  \033[1;32mstatic\033[0m     - Build statically linked binary"
	@$(ECHO) "  \033[1;32mdebug\033[0m      - Build with debug symbols"
	@$(ECHO) "  \033[1;32mstrip\033[0m      - Strip symbols from binary"
	@$(ECHO) "  \033[1;32mclean\033[0m      - Remove build artifacts"
	@$(ECHO) "  \033[1;32mlint\033[0m       - Run static code analysis"
	@$(ECHO) "  \033[1;32mhelp\033[0m       - Display this help message"
	@$(ECHO) ""
	@$(ECHO) "\033[1;40;33mExamples:\033[0m"
	@$(ECHO) "  \033[36mmake\033[0m            - Build cpulimit"
	@$(ECHO) "  \033[36mmake static\033[0m      - Build static binary"
	@$(ECHO) "  \033[36mmake debug\033[0m       - Build with debug info"
	@$(ECHO) "  \033[36mmake lint\033[0m        - Run static analysis"
	@$(ECHO) "  \033[36mmake clean\033[0m       - Clean build files"
	@$(ECHO) ""
	@$(ECHO) "\033[1;40;33mNotes:\033[0m"
	@$(ECHO) "  - Static binary may not work on all systems"
	@$(ECHO) "  - Debug mode requires root privileges for core dump settings"

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

lint: cpulimit.c
	@$(ECHO) "\033[1;40;32mRunning static code analysis..\033[0m"
	@if command -v cppcheck >/dev/null 2>&1; then \
		$(ECHO) "\033[1;33m[cppcheck] Checking C code..\033[0m"; \
		cppcheck --enable=warning,performance,portability \
			--error-exitcode=1 \
			--suppress=missingIncludeSystem \
			--suppress=unusedFunction \
			--suppress=toomanyconfigs \
			--suppress=syntaxError \
			--check-level=normal \
			--check-config \
			-I/usr/include cpulimit.c 2>&1 | grep -v -E "^/usr|^Checking|^extern|^nofile|\^$$" || true; \
		$(ECHO) "\033[1;32m[cppcheck] Complete\033[0m"; \
	else \
		$(ECHO) "\033[1;33m[warning] cppcheck not found. Install for full linting.\033[0m"; \
	fi
	@if command -v shellcheck >/dev/null 2>&1; then \
		$(ECHO) "\033[1;33m[shellcheck] Checking Makefile..\033[0m"; \
		shellcheck -x Makefile || true; \
		$(ECHO) "\033[1;32m[shellcheck] Complete\033[0m"; \
	else \
		$(ECHO) "\033[1;33m[warning] shellcheck not found. Install for full linting.\033[0m"; \
	fi
	@$(ECHO) "\033[1;40;36mLinting complete\033[0m"

setenv::
	@$(ECHO) "\033[1;40;32mSet Debug mode enviroment..\033[0m"
	@$(CMD_SUDO) $(CMD_SYSCTL) -e -q -w kernel.core_pattern="/tmp/%e.core.%u" kernel.suid_dumpable=1 fs.suid_dumpable=1
	@$(CMD_SUDO) $(CMD_SYSCTL) -q -w kernel.core_uses_pid=1;
	@$(ECHO) "\tkernel.core_pattern=/tmp/%e.core.%u"
	@$(ECHO) "\tkernel.suid_dumpable=1"
	@$(ECHO) "\tfs.suid_dumpable=1"
	@$(ECHO) "\tkernel.core_uses_pid=1"
	@$(ECHO) "\033[1;40;36mComplete\033[0m"
