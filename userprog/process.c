#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/synch.h"
#define VM
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (evoid);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
void push_argument(char **argv, int argc, struct intr_frame *_if);
struct thread* find_child(tid_t child_tid);
struct lazy_load_arg
{
	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
};
/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;
	/*parse_file_name*/
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	char* saveptr;
	/* Create a new thread to execute FILE_NAME. */
	file_name = strtok_r(file_name, " ", &saveptr);
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. 
 * 현재 프로세스를 이름으로 복제합니다. 새 프로세스의 스레드 ID를 반환하거나 스레드를 만들 수 없는 경우 TID_ERROR를 반환합니다.
 */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* Clone current thread to new thread.*/
	struct thread *current_thread = thread_current();
	sema_init(&(current_thread->fork_sema), 0);
	/* 전달받은 intr_frame을 현재 parent_if에 복사 */
	memcpy(&(current_thread->parent_if), if_, sizeof(struct intr_frame));
	tid_t pid = thread_create(name, PRI_DEFAULT, __do_fork, current_thread);
	// printf("fork_seman_down\n");
	sema_down(&(current_thread->fork_sema));
	// printf("sema_free?\n");
	struct thread* child_thread = find_child(pid);
	if (pid == TID_ERROR ) {
		return TID_ERROR;
	}

	if (child_thread->exit_status == -2){
		// sema_up(&(child_thread->exit_sema));
		// printf("sema_up?1\n");
		return TID_ERROR;
	}
	
	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. 
 * 이 함수를 pml4_for_each에 전달하여 부모 주소 공간을 복제합니다. 이것은 프로젝트 2에만 해당됩니다. 
 * */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. 
		parent_page가 커널 페이지이면 즉시 반환합니다.*/
	if is_kernel_vaddr(va) 
		return true;
	
	/* 2. Resolve VA from the parent's page map level 4. 
		상위 페이지 맵 레벨 4에서 VA를 해결합니다.*/
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. 
	 	자식에 대한 새 PAL_USER 페이지를 할당하고 결과를 NEWPAGE로 설정합니다.*/
	newpage = palloc_get_page(PAL_USER);

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). 
	 * 부모 페이지를 새 페이지로 복제하고 부모 페이지가 쓰기 가능한지 확인합니다(결과에 따라 WRITABLE 설정).*/
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. 
		  WRITABLE 권한이 있는 주소 VA의 자식 페이지 테이블에 새 페이지를 추가합니다.*/
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. 
			페이지 삽입에 실패하면 오류 처리를 수행하십시오. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. 
 * */
/*부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * 힌트) parent->tf는 프로세스의 사용자 및 컨텍스트를 보유하지 않습니다. 즉, process_fork의 두 번째 인수를 이 함수에 전달해야 합니다.
 자식 프로세스가 실행하는 함수로, 부모 프로세스의 자료구조를 복제하도록 구현
 메모리 복제는 duplicate_pte() 
 */
static void
 __do_fork (void *aux) {
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame parent_if = parent->parent_if;
	struct intr_frame _if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&_if, &parent_if, sizeof (struct intr_frame));
	_if.R.rax = 0;
	
	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL){
		goto error;
	}
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)){
		goto error;
	}
#endif

	if (parent->last_fd >= MAX_FILE_DESCRIPTOR){
		goto error;
	}

	for (size_t i = 2; i < MAX_FILE_DESCRIPTOR; i++)
	{
		if (parent->file_fdt[i] != NULL){
			current->file_fdt[i] = file_duplicate(parent->file_fdt[i]);
		}
	}
	
	current->last_fd = parent->last_fd;

	
	
	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	sema_up(&(parent->fork_sema));
	process_init ();
	/* Finally, switch to the newly created process. */
	if (succ){
		do_iret (&_if);
	}
error:
	// printf("fork_error\n");
	sema_up(&(parent->fork_sema));
	exit(-2);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	const int MAX_ARGUMENTS = 128;
	struct thread* current_thread = thread_current();

	char *file_name;
	bool success;
	char *token;
	int argc = 0;
	char *saveptr;
	char *argv[MAX_ARGUMENTS];
	
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;
	
	/* We first kill the current context */
	process_cleanup ();
	
	
	current_thread = thread_current();
	
	argv[argc] = strtok_r(f_name, " ", &saveptr);
	argc++;

	while(true){
		argv[argc] = strtok_r(NULL, " ", &saveptr);
 		if (argv[argc] == NULL || argc >= MAX_ARGUMENTS){
			break;
		}
		argc++;
	}
	
	file_name = argv[0];
	/* And then load the binary */
	lock_acquire(&filesys_lock);
	success = load (file_name, &_if);
	lock_release(&filesys_lock);
	/* If load failed, quit. */
	if (!success)
		// palloc_free_page (file_name);					
    	return -1;

	push_argument(argv ,argc, &_if);
	
	palloc_free_page (file_name);
	/* Start switched process. */
	do_iret (&_if);					
	NOT_REACHED ();
}

void
push_argument(char** argv, int argc, struct intr_frame *_if){
	uint8_t padding;

	/*argument*/
	for (int i = argc - 1 ; i > -1; i--){
		_if->rsp = _if->rsp - strlen(argv[i]) - 1;
		strlcpy(_if->rsp, argv[i], strlen(argv[i]) + 1);
		argv[i] = (char*)_if->rsp;
	}

	/*padding*/
	if(_if->rsp % 8 != 0){
		padding = _if->rsp % 8;
		_if->rsp = _if->rsp  - padding;
		memset(_if->rsp, 0, padding);
	}
	
	
	/*argument addresss*/
	for (int i = argc; i >= 0; i--){
		_if->rsp = _if->rsp - sizeof(char*);
		if (i == argc){
			memset (_if->rsp, 0, sizeof(char*));
		}
		memcpy(_if->rsp, &argv[i], sizeof(char*));
	}

	/*return address*/
	_if->rsp = _if->rsp - sizeof(void*);
	memset (_if->rsp, 0, sizeof(void*));

	_if->R.rdi = argc;
	_if->R.rsi = _if->rsp + sizeof(void*);

}



/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	struct thread* current_thread = thread_current();
	struct thread* child_thread ;
	if (current_thread->wait_success_tid == child_tid){
		return -1;
	}
	if (!list_empty(&(current_thread->child_list)) ){
		sema_down(&(current_thread->wait_sema));
	}
	if (strcmp(current_thread->name, "main") != 0){

		child_thread = find_child(child_tid);
		if (child_thread == NULL){
			// printf("current_thread_name : %s\n", current_thread->name);
			return -1;
		}
		sema_up(&(child_thread->exit_sema));
	}
	if (!list_empty(&(current_thread->child_list)) && strcmp(current_thread->name, "main") != 0 ){
		sema_down(&(current_thread->status_sema));
	}
	current_thread->wait_success_tid = child_tid;
	return current_thread->exit_status;
}

struct thread*
find_child(tid_t child_tid){
	struct thread* current_thread = thread_current();
	struct list_elem *child_element = list_begin(&(current_thread->child_list));
	if (child_element == list_end(&(current_thread->child_list))){
		return NULL;
	}
	while(true){
		struct thread *child_thread = list_entry(child_element, struct thread, child_elem);
		if (child_thread->tid == child_tid){
			return child_thread;
		}
		child_element = list_next(child_element);
	}
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread* current_thread = thread_current();
	sema_up(&(current_thread->parent->wait_sema));
	struct list_elem *child_element = list_begin(&(current_thread->parent->child_list));
	while(true){
		if (child_element == list_end(&(current_thread->parent->child_list))){
			break;
		}
		struct thread *child_thread = list_entry(child_element, struct thread, child_elem);
		if (child_thread == current_thread){
			sema_down(&(current_thread->exit_sema));
			current_thread->parent->exit_status = current_thread->exit_status;
			list_remove(child_element);
			sema_up(&(current_thread->parent->status_sema));
			break;
		}
		child_element = list_next(child_element);
	}
	for (int i = 2 ; i < MAX_FILE_DESCRIPTOR ; i++){
		close(i);
	}
	if (current_thread->open_file != NULL){
		file_close(current_thread->open_file);
	}
	palloc_free_multiple(current_thread->file_fdt, 3);
	process_cleanup ();
	
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	
	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	file_deny_write(file);
	thread_current()->open_file = file;

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)aux;
	struct file *file = lazy_load_arg->file;
	off_t ofs = lazy_load_arg->ofs;
	uint32_t page_read_bytes = lazy_load_arg->read_bytes;
	uint32_t page_zero_bytes = lazy_load_arg->zero_bytes;
	
	lock_acquire(&lazy_load_lock);
	file_seek(file, ofs);
	if (file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes)
	{
		palloc_free_page(page->frame->kva);
		return false;
	}
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	lock_release(&lazy_load_lock);
	
	// free(lazy_load_arg);
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct lazy_load_arg *aux = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		aux->file = file;
		aux->ofs = ofs;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment, aux)){
			return false;
		}	

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);
	bool writable = true;
	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	if (vm_alloc_page(VM_ANON | VM_MARKER_0 , stack_bottom, writable))
	{
		success = vm_claim_page(stack_bottom);
		if (success)
			if_->rsp = USER_STACK;
	}

	return success;
}
#endif /* VM */
