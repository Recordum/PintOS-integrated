#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);
void halt();
void exit(int status);
// pid_t fork (const char *thread_name);
// int exec (const char *file); 
// int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
void close (int fd);
int read(int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
};
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
	struct list_elem lock_elem;
};

struct lock filesys_lock;

syscall_init (void) {
	lock_init(&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	
	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}
// intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
/* The main system call interface */

void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	// syscall_number에 따라 systemcall(HALT,exec,./..)
	/*check validation*/
	struct thread* curr = thread_current();
	uint64_t syscall_number = f->R.rax;
	uint64_t rsp = f->rsp;
	uint64_t ARG0 = f->R.rdi;
	uint64_t ARG1 = f->R.rsi;
	uint64_t ARG2 = f->R.rdx;
	uint64_t ARG3 = f->R.r10;
	uint64_t ARG4 = f->R.r8;
	uint64_t ARG5 = f->R.r9;

	check_address(rsp);

	switch (syscall_number)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(ARG0);
		break;
	case SYS_FORK:
		break;
	case SYS_EXEC:
		break;
	case SYS_WAIT:
		break;
	case SYS_CREATE:
		create(ARG0,ARG1);
		break;
	case SYS_REMOVE:
		remove(ARG0);
		break;
	case SYS_OPEN:
		open(ARG0);
		break;
	case SYS_FILESIZE:
		filesize(ARG0);
		break;
	case SYS_READ:
		read(ARG0, ARG1, ARG2);
		break;
	case SYS_WRITE:
		write(ARG0, ARG1, ARG2);
		break;
	case SYS_SEEK:
		break;
	case SYS_TELL:
		break;
	case SYS_CLOSE:
		close(ARG0);
		break;
	}

	// printf ("system call!\n");
	// thread_exit ();
}


void
check_address(void *addr) {
	struct thread *t = thread_current();
	if (!is_user_vaddr(addr)||addr == NULL||pml4_get_page(t->pml4, addr)== NULL) {								
		exit(-1);
	}
}

void
halt(){
	power_off();
}

void
exit(int status){
	char* name = thread_current()->name;
	printf("%s: exit(%d)\n",name, status);
	thread_exit();
}

// pid_t
// fork (const char *thread_name){
// }
// ​
// /*현재 프로세스를 cmd_line에서 지정된 인수를 전달하여 이름이 지정된 실행 파일로 변경*/
// int
// exec (const char *file) {
// 	check_address(file);
// }


bool
create (const char *file, unsigned initial_size) {	
	return filesys_create(file, initial_size);
}

bool
remove (const char *file) {	
	return filesys_remove(file);				//파일 시스템에서 file 이름을 가진 파일을 삭제하는 함수
}

int 
open (const char *file) {
	
	struct file *open_file = filesys_open(file);
	struct thread *current_thread = thread_current();
	struct file** fdt = current_thread->file_fdt;
	
	if (open_file == NULL){
		return -1;
	}

	for (int fd = 0; fd < sizeof(fdt); fd++)
	{
		if (fdt[fd] == NULL){
			fdt[fd] = open_file;
			// current_thread->next_fd = fd;
			return fd;
		}
	}		
}

void 
close (int fd){
	struct thread* current_thread = thread_current();
	current_thread->file_fdt[fd] = NULL;
}

int
read(int fd, void *buffer, unsigned size){
	if(fd == 0){
		input_getc();
		return size;
	}
	struct thread* current_thread = thread_current();
	struct file *file_object = current_thread->file_fdt[fd];
	return file_read(file_object, buffer, size);
}

int
write (int fd, const void *buffer, unsigned size){
	if(fd == 1){
		putbuf(buffer, size);
		return size;
	}
	struct thread* current_thread = thread_current();
	struct file *file_object = current_thread->file_fdt[fd];
	return file_write(file_object, buffer, size);
}

int
filesize(int fd){
	struct thread* current_thread = thread_current();
	struct file *file_object = current_thread->file_fdt[fd];
	return (int)file_length(file_object);
}
