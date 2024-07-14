// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmapping", "Display mapping information of address", mon_showmapping },
	{ "setperm", "Change permissions of address", mon_setperm},
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t *ebp = (uint32_t *) read_ebp();
	cprintf("Stack backtrace:\n");
	while(ebp) {
		cprintf("ebp %08x eip %08x args ", ebp, ebp[1]);
		for(int i = 2; i <= 6; i++) 
			cprintf("%08x ", ebp[i]);
		cprintf("\n");

		//backtrace
		unsigned int eip = ebp[1];
		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);
		cprintf("%s:%d: ", info.eip_file, info.eip_line);
		for(int i = 0; i < info.eip_fn_namelen; i++) {
			cprintf("%c", info.eip_fn_name[i]);
		}
		cprintf("+%d\n", (int)(ebp[1] - info.eip_fn_addr));

		ebp = (uint32_t *) *ebp;
	}
	return 0;
}

int
mon_showmapping(int argc, char **argv, struct Trapframe *tf) 
{
	if(argc != 3) {
		cprintf("Usage: showmapping [begin_address] [end_address]\n");
		return 0;
	}

	if(!checkhex(argv[1]) || !checkhex(argv[2])) {
		cprintf("Invalid address.\n");
		return 0;
	}

	uint32_t begin = hextoi(argv[1]), end = hextoi(argv[2]);
	for(; begin <= end; begin += PGSIZE) {
		pte_t *pte = pgdir_walk(kern_pgdir, (void *) begin, 1);
		cprintf("va %08x: ", begin);
		if(pte == NULL) {
			panic("error: out of memory");
		}
		if(*pte & PTE_P) {
			cprintf("0x%08x ", PTE_ADDR(*pte));
			perm_print(*pte);
			cprintf("\n");
		}
		else {
			cprintf("page not mapped\n");
		}
	}
	return 0;
}

int 
mon_setperm(int argc, char **argv, struct Trapframe *tf) {
	if(argc < 2 || argc > 5) {
		cprintf("Usage: setperm [address] [PTE_U] [PTE_W] [PTE_P]\n");
		return 0;
	}

	if(!checkhex(argv[1])) {
		cprintf("Invalid address!\n");
		return 0;
	}

	uint32_t addr = hextoi(argv[1]);
	pte_t *pte = pgdir_walk(kern_pgdir, (void *) addr, 1);
	if(pte == NULL) {
		panic("error: Out of memory");
	}

	physaddr_t pa = PTE_ADDR(pte);
	
	int perm = 0;
	if(argv[2][0] == '1') perm |= PTE_U;
	if(argv[3][0] == '1') perm |= PTE_W;
	if(argv[4][0] == '1') perm |= PTE_P;
	boot_map_region(kern_pgdir, addr, PGSIZE, pa, perm);

	cprintf("Before change: ");
	perm_print(*pte); 
	cprintf("\n");

	*pte = (PTE_ADDR(*pte) & ~0x111) | perm;
	cprintf("After change: ");
	perm_print(*pte); 
	cprintf("\n");

	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
