#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "kernel/stdio.h"
#include "threads/init.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_valid_addr(void *addr);
void check_valid_pointer(void *start, size_t size);

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

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void check_valid_addr(void *addr){
	struct thread *curr = thread_current();
	if(!is_user_vaddr(addr)){
		//exit(-1);
		thread_exit();
	}

	if(pml4_get_page(curr->pml4, addr) == NULL){
		thread_exit();
		//exit(-1);
	}
}

void check_valid_pointer(void *start, size_t size){
	uint64_t end = start + (uint64_t)size;
	for(uint64_t *begin = pg_round_down(start); begin <= end; begin += PGSIZE){
		check_valid_addr(begin);
	}
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	uint64_t sys_type = f->R.rax; 
	struct thread *curr = thread_current();
	
	switch(sys_type){
		/*
			Pintos를 종료하는 syscall
			power_off() 를 호출하면 됨.
		*/
		case SYS_HALT: 
			power_off();
			break;
		/*
			현재 user program을 종료하고, 종료 상태 값을 커널에 남긴다.
			부모가 wait 하면 이 status 값을 받아야 한다. 
			관례적으로 0 = 성공, 0 이 아닌 값 = 실패
		*/
		case SYS_EXIT:
			curr->exit_status = f->R.rdi;
			thread_exit();
			break;

		case SYS_FORK:
			break;

		case SYS_EXEC:
			break;

		case SYS_WAIT:
			break;

		case SYS_CREATE:
			break;

		case SYS_REMOVE:
			break;

		case SYS_OPEN:
			break;

		case SYS_FILESIZE:
			break;

		case SYS_READ:
			break;

		case SYS_WRITE:
			uint64_t buf = f->R.rsi;
			uint64_t size = f->R.rdx;

			check_valid_pointer(buf, size - 1);

			if(f->R.rdi == 1) {
				//rsi -> buf, rdx -> size
				putbuf(buf, (size_t)size);

				f->R.rax = size; //rax갱신 
				return;
			}
			break;

		case SYS_SEEK:
			break;

		case SYS_TELL:
			break;

		case SYS_CLOSE:
			break;
			
		default:
			break;
	}
}
