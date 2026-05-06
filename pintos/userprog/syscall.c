#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "kernel/stdio.h"
#include "threads/init.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "filesys/file.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_valid_addr(void *addr);
void check_valid_pointer(void *start, size_t size);
void check_valid_str(const char *str);
void handle_sys_halt(struct intr_frame *f);
void handle_sys_exit(struct intr_frame *f);
void handle_sys_fork(struct intr_frame *f);
void handle_sys_exec(struct intr_frame *f);
void handle_sys_create(struct intr_frame *f);
void handle_sys_remove(struct intr_frame *f);
void handle_sys_open(struct intr_frame *f);
void handle_sys_filesize(struct intr_frame *f);
void handle_sys_read(struct intr_frame *f);
void handle_sys_write(struct intr_frame *f);
void handle_sys_seek(struct intr_frame *f);
void handle_sys_tell(struct intr_frame *f);
void handle_sys_close(struct intr_frame *f);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void check_valid_addr(void *addr)
{
	struct thread *curr = thread_current();
	if (!is_user_vaddr(addr))
	{
		// exit(-1);
		thread_exit();
	}

	if (pml4_get_page(curr->pml4, addr) == NULL)
	{
		thread_exit();
		// exit(-1);
	}
}

void check_valid_pointer(void *start, size_t size)
{
	uint64_t end = start + (uint64_t)size;
	for (uint64_t *begin = pg_round_down(start); begin <= end; begin += PGSIZE)
	{
		check_valid_addr(begin);
	}
}

void handle_sys_halt(struct intr_frame *f UNUSED)
{
	/*
		Pintos를 종료하는 syscall
		power_off() 를 호출하면 됨.
	*/
	power_off();
}

void handle_sys_exit(struct intr_frame *f)
{
	/*
		현재 user program을 종료하고, 종료 상태 값을 커널에 남긴다.
		부모가 wait 하면 이 status 값을 받아야 한다.
		관례적으로 0 = 성공, 0 이 아닌 값 = 실패
	*/
	struct thread *curr = thread_current();
	curr->exit_status = f->R.rdi;
	thread_exit();
}

void handle_sys_write(struct intr_frame *f)
{
	int fd = f->R.rdi;
	void *buffer = f->R.rsi;
	unsigned size = f->R.rdx;

	if(size == 0){
		f->R.rax = 0;
		return;
	}

	check_valid_pointer(buffer, size - 1);

	if(fd == 0){
		f->R.rax = -1;
		return;
	}
	else if(fd == 1) {
		putbuf(buffer, (size_t)size);

		f->R.rax = size;
		return;
	}
	else if(fd > 1){
		// fd가 1 이상이라면 해당 fd에 맞는 열려 있는 파일을 찾고
		// buffer 에 있는걸 읽고, 그 파일에 쓴다.
		// 쓴 byte 수(size)를 rax에 반환.
		struct file *file = process_get_file(fd);
		if(file == NULL){
			f->R.rax = -1;
			return;
		}
		off_t real_size = file_write(file, buffer, size);
		
		f->R.rax = real_size;
		return;
	}
	f->R.rax = -1;
	return;
}

void handle_sys_fork(struct intr_frame *f)
{
	const char *name = (const char *)f->R.rdi;
	check_valid_str(name);
	f->R.rax = process_fork(name, f);
}

void handle_sys_exec(struct intr_frame *f)
{
    const char *file = (const char *) f->R.rdi;

    check_valid_str(file);

	// exec는 기존 프로그램의 유저 메모리 공간을 버리고, 새 프로그램의 메모리 공간으로 교체하기 위함
	// exec는 새로운 프로세스를 만드는게 아니다. fork X
	// 현재 프로세스가 실행하던 프로그램을 새 프로그램으로 바꾸는 작업

	/*
		exec() 호출 흐름
		기존 프로그램이 돌아가고 있다 -> CPU가 현재 프로세스의 pml4를 기준으로 가상주소를 매핑하고 있다.
		-> exec()를 호출
		-> process_cleanup() = 기존의 pml4를 해제한다.
		-> load() => 새로운 pml4 를 생성한다(새로운 프로그램의 코드영역/데이터영역/스택영역을 채운다.)
		-> do_iret() => 새롭게 만들어진 프로그램을 유저 영역으로 넘김
	*/

	/*
		cleanup()을 하는 이유?
		-> 기존 프로그램의 pt(가상주소를 물리 주소에 매핑)이 남아있는 상태에서 새 프로그램을 같은 가상주소에 올리게 된다. 이러면 안됨.
	*/

	/*
		그대로 rdi를 안넘겨주고, 카피본을 뜬 다음 그걸 인자로 넘겨주는 이유가 중요하다.
		프로그램이 실행중에 exec("~") 를 호출하면 "~" 문자열은 유저 프로그램의 메모리 어딘가에 남아있다.
		rdi에 "~"의 주소가 들어가 있고, 해당 프로세스의 PT는 "~"의 가상주소를 물리 페이지에 매핑하는 정보를 가지고 있다.
		이 상태에서 rdi 를 복사하지 않고, 그대로 인자로 넘겨버리게 되면 위에서 설명한 cleanup() 후에는 해당 rdi에 있는 값을 찾을 수 없다.
		load() 에서는 불러오고자 하는 file_name을 인자로 받는데, 이렇게 되면 "~" 값이 아니라 invalid한 값을 읽어버리게 되서 잘못된 동작을 하거나 fault 가 발생한다.
	*/
		
	/*
		위와 같은 상황을 막기 위해, file_name을 어딘가에 따로 복사를 해두고 그걸 인자로 넘겨줘야 한다.
		cleanup()을 할 때, 사라지지 않고 안전한 곳 => 커널 영역의 페이지, 즉 palloc_get_page(0)으로 할당한 커널 페이지
		생성한 커널 페이지에 file_name을 복사해두고, 이걸 exec()의 인자로 넘기면 clean 해도 유저 영역만 제거되고 커널 페이지는 살아남는다.
	*/
    char *file_copy = palloc_get_page(0);
    if (file_copy == NULL) {
        f->R.rax = -1;
        return;
    }

    strlcpy(file_copy, file, PGSIZE);

	/*
		아래 조건문을 넣어 주는 이유
		즉, process_exec가 실패해서 돌아왔을때, 기존 유저 프로그램으로 정상적으로 복귀할 수 없기 때문에
		page fault가 발생한다
		그래서 실패하면 thread_exit()으로 종료시킨다.
	*/
    if (process_exec(file_copy) < 0)
	{
		thread_current()->exit_status = -1;
		thread_exit();
	}
}

void handle_sys_create(struct intr_frame *f)
{
	// file 이름 문자열의 주소를 검사?
	const char *file = (const char *)f->R.rdi;
	unsigned initial_size = (unsigned)f->R.rsi;

	check_valid_str(file);
	f->R.rax = filesys_create(file, initial_size);
}

void handle_sys_remove(struct intr_frame *f)
{
	const char *file = (const char *)f->R.rdi;

	check_valid_str(file);
	f->R.rax = filesys_remove(file);
}

void handle_sys_open(struct intr_frame *f)
{
	const char *file = (const char *)f->R.rdi;

	check_valid_str(file);

	// 파일 이름을 받아 파일 시스템에서 해당 파일을 찾고, 열린 파일 객체를 만들어 리턴하는 함수
	struct file *kernel_file = filesys_open(file);
	f->R.rax = process_add_file(kernel_file);
}

void handle_sys_filesize(struct intr_frame *f)
{
	// file.c 에 file_length(file *)
	// file 안에 있는 바이트 개수를 반환하는 함수 사용
	int fd = f->R.rdi;
	struct file *file = process_get_file(fd);

	if (file == NULL)
	{
		f->R.rax = -1;
		return;
	}
	f->R.rax = file_length(file);
}

void handle_sys_read(struct intr_frame *f)
{
	/*
		read(fd, buffer, size)
		fd -> buffer
		fd에서 읽고, buffer에 씀
	*/
	int fd = f->R.rdi;
	char *buffer = (char *)f->R.rsi;
	unsigned size = (unsigned)f->R.rdx;

	if (size == 0)
	{
		f->R.rax = 0;
		return;
	}
	check_valid_pointer(buffer, size - 1);

	if (fd == 1)
	{
		f->R.rax = -1;
		return;
	}
	else if (fd == 0)
	{
		// input_getc() => 사용자로부터 한글자를 입력받음
		// 한 글자씩 받아서 size 만큼 반복하면서 buffer에 채워넣음.
		for (int i = 0; i < size; i++)
		{
			buffer[i] = input_getc();
		}
		// 실제 읽은 바이트 수를 반환해야 하는데?
		// 그냥 size를 rax에 넣으면 안될거같은데
		// 사용자 입력값은 중간에 NULL 을 입력해도 끝까지 입력하니까 상관없다?
		f->R.rax = size;
		return;
	}
	else if (fd > 1)
	{
		// fd가 1보다 크면, 해당 fd에 적혀있는 번호에 맞는 파일을 열어야 함 X => 해당하는 열린 파일을 찾는다?
		// 해당 파일에서 데이터를 읽고 buffer 에 저장한다.
		// OPEN SYSCALL 필요?
		// 일단 구현되어 있다고 하고 사용했음.
		// 주석에 따르면 fd번호로 실제 파일 객체를 찾음
		// 아직 테스트를 못돌림. process_get_file() 이 구현이 안되어 있음.
		struct file *file = process_get_file(fd);
		// 해당 fd를 찾아서 fd 테이블에 가서 파일을 찾았는데 없는 경우가 있을 수도 있음.
		if (file == NULL)
		{
			f->R.rax = -1;
			return;
		}
		f->R.rax = file_read(file, buffer, size);
		return;
	}

	// 잘못된 fd 입력값
	f->R.rax = -1;
}

void handle_sys_seek(struct intr_frame *f)
{
	/*
		fd로 열린 파일의 위치를 position 바이트 지점으로 이동한다
	*/
	int fd = f->R.rdi;
	unsigned position = (unsigned)f->R.rsi;

	if (fd < 2)
	{
		return;
	}

	struct file *file = process_get_file(fd);
	if (file == NULL)
	{
		return;
	}

	// seek() 가 바꾸는건 fd가 가리키는 열린 파일의 현재 offset 위치를 바꾼다.
	file_seek(file, (off_t)position);
}

void handle_sys_tell(struct intr_frame *f)
{
	/*
		열려 있는 파일에서 읽거나 쓸 다음 바이트의 위치를 fd 파일 시작 부분부터 바이트 단위로 반환.
	*/
	int fd = f->R.rdi;

	if (fd < 2)
	{
		f->R.rax = -1;
		return;
	}

	struct file *file = process_get_file(fd);
	if (file == NULL)
	{
		f->R.rax = -1;
		return;
	}

	f->R.rax = file_tell(file);
}

void handle_sys_close(struct intr_frame *f)
{
	process_close_file((int)f->R.rdi); // 해당 fd 닫기
}

void handle_sys_wait(struct intr_frame * f)
{
	f->R.rax = process_wait(f->R.rdi);
}

void check_valid_str(const char *str) {
    for (int i = 0;; i++) {
        check_valid_addr(&str[i]);
        if (str[i] == '\0')
            break;
    }
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	uint64_t sys_type = f->R.rax;

	switch (sys_type)
	{
	case SYS_HALT:
		handle_sys_halt(f);
		break;

	case SYS_EXIT:
		handle_sys_exit(f);
		break;

	case SYS_FORK:
		handle_sys_fork(f);
		break;

	case SYS_EXEC:
		handle_sys_exec(f);
		break;

	case SYS_WAIT:
		handle_sys_wait(f);
		break;

	case SYS_CREATE:
		handle_sys_create(f);
		break;

	case SYS_REMOVE:
		handle_sys_remove(f);
		break;

	case SYS_OPEN:
		handle_sys_open(f);
		break;

	case SYS_FILESIZE:
		handle_sys_filesize(f);
		break;

	case SYS_READ:
		handle_sys_read(f);
		break;

	case SYS_WRITE:
		handle_sys_write(f);
		break;

	case SYS_SEEK:
		handle_sys_seek(f);
		break;

	case SYS_TELL:
		handle_sys_tell(f);
		break;

	case SYS_CLOSE:
		handle_sys_close(f);
		break;

	default:
		break;
	}
}
