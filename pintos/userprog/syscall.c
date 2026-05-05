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
#include "filesys/filesys.h"
#include "devices/input.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_valid_addr(void *addr);
void check_valid_pointer(void *start, size_t size);
void check_valid_str(char *str);

// 껍데기
int process_add_file(struct file *file); //새로 열린 파일을 fd table에 등록하고 fd번호 리턴
struct file *process_get_file(int fd); //fd번호로 실제 파일 객체를 찾음
void process_close_file(int fd); //fd 하나를 닫음
void process_close_all_files(void); //현재 프로세스가 열어둔 모든 파일을 닫음

// 껍데기
int process_add_file(struct file *file); //새로 열린 파일을 fd table에 등록하고 fd번호 리턴
struct file *process_get_file(int fd); //fd번호로 실제 파일 객체를 찾음
void process_close_file(int fd); //fd 하나를 닫음
void process_close_all_files(void); //현재 프로세스가 열어둔 모든 파일을 닫음

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

void check_valid_str(char *str){
	for(int i = 0;; i++){
		if(str[i] == '\0'){
			break;
		}
		check_valid_addr(&str[i]);
	}
}

int process_add_file(struct file *file) // 새로 열린 파일을 fd table에 등록하고 fd번호 리턴. 실패시 -1 리턴
{
	if(file == NULL) { return -1; } //잘못된 파일 들어오면
	struct thread * t = thread_current(); //현재 스레드지정.
	for (int idx = 2; idx < fdt_size; idx++)	  // 현재 fdt 인덱스. 원래 next_fd로 할랬는데 그냥 어차피 빈칸 찾으려면 선형탐색을 해야해서 그냥 스레드 구조체도 수정
	{
		if(t -> fdt[idx] == NULL) // 비었으면
		{
			t -> fdt[idx] = file;
			return idx;
		}
	}
	//다 돌았으면 빈곳이 없었다는거니까 -1 리턴
	return -1;
}

struct file *process_get_file(int fd) //fd번호로 실제 파일 객체를 찾음
{
	if(fd <= 1 || fdt_size <= fd) { return  NULL; } // 잘못된 번호 들어오면
	struct thread *t = thread_current();
	return (t -> fdt[fd]);
}

void process_close_file(int fd)
{
	//if(fd <= 1 || fdt_size <= fd) { return; } //잘못된 fd값 들어오면 검사를 할랬는데 process_get_file에서 함
	struct file * ptr = process_get_file(fd); //fd가 가리키는 파일 포인터
	struct thread *t = thread_current();

	if(ptr == NULL) { return; } //잘못된 fd값 들어오면
	else { file_close(ptr); }
	t -> fdt[fd] = NULL;
}

void process_close_all_files(void) // 현재 프로세스가 열어둔 모든 파일을 닫음
{
	for(int i = 2; i < fdt_size; i++)
	{
		process_close_file(i);
	}
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	uint64_t sys_type = f->R.rax;
	struct thread *curr = thread_current();

	switch(sys_type){
		/*
			Pintos를 종료하는 syscall
			power_off() 를 호출하면 됨.
		*/
		case SYS_HALT:{
			power_off();
			break;
		}
		/*
			현재 user program을 종료하고, 종료 상태 값을 커널에 남긴다.
			부모가 wait 하면 이 status 값을 받아야 한다.
			관례적으로 0 = 성공, 0 이 아닌 값 = 실패
		*/
		case SYS_EXIT:{
			curr->exit_status = f->R.rdi;
			thread_exit();
			break;
		}
		case SYS_FORK:
			break;

		case SYS_EXEC:
			break;

		case SYS_WAIT:
			break;

		case SYS_CREATE:{
			// file 이름 문자열의 주소를 검사?
			const char *file = (const char*)f->R.rdi;
			unsigned initial_size = (unsigned)f->R.rsi;
			
			check_valid_str(file);
			
			f->R.rax = filesys_create(file, initial_size);
			return;
		}
		case SYS_REMOVE:{
			const char *file = f->R.rdi;

			check_valid_str(file);

			f->R.rax = filesys_remove(file);
			return;
		}

		case SYS_OPEN:
			break;

		case SYS_FILESIZE:{
			// file.c 에 file_length(file *)
			// file 안에 있는 바이트 개수를 반환하는 함수 사용
			int fd = f->R.rdi;

			if(fd < 2){
				f->R.rax = -1;
				return;
			}

			struct file *file = process_get_file(fd);

			if(file == NULL){
				f->R.rax = -1;
    			return;
			}
			
			f->R.rax = file_length(file);
			return;
		}
		/*
			read(fd, buffer, size)
			fd -> buffer
			fd에서 읽고, buffer에 씀
		*/
		case SYS_READ:{
			int fd = f->R.rdi;
			char *buffer = (char *)f->R.rsi;
			unsigned size = (unsigned)f->R.rdx; 

			if(size == 0){
				f->R.rax = 0;
				return;
			}
			check_valid_pointer(buffer, size - 1);

			if(fd == 1){
				f->R.rax = -1;
				return;
			}
			else if(fd == 0){
				// input_getc() => 사용자로부터 한글자를 입력받음
				// 한 글자씩 받아서 size 만큼 반복하면서 buffer에 채워넣음.
				for(int i = 0; i < size; i++){
					buffer[i] = input_getc();
				}
				// 실제 읽은 바이트 수를 반환해야 하는데?
				// 그냥 size를 rax에 넣으면 안될거같은데
				// 사용자 입력값은 중간에 NULL 을 입력해도 끝까지 입력하니까 상관없다?
				f->R.rax = size;
				return;
			}
			// fd가 1보다 크면, 해당 fd에 적혀있는 번호에 맞는 파일을 열어야 함 X => 해당하는 열린 파일을 찾는다?
			// 해당 파일에서 데이터를 읽고 buffer 에 저장한다.
			// OPEN SYSCALL 필요?

			else if(fd > 1){
				// 일단 구현되어 있다고 하고 사용했음. 
				// 주석에 따르면 fd번호로 실제 파일 객체를 찾음 
				// 아직 테스트를 못돌림. process_get_file() 이 구현이 안되어 있음.
				struct file *file = process_get_file(fd);
				// 해당 fd를 찾아서 fd 테이블에 가서 파일을 찾았는데 없는 경우가 있을 수도 있음.
				if(file == NULL){
					f->R.rax = -1;
    				return;
				}
				off_t real_size = file_read(file, buffer, size);
				
				f->R.rax = real_size;
				return;
			}

			// 잘못된 fd 입력값
			f->R.rax = -1;
			return;
		}

		case SYS_WRITE:{
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
		/*
			fd로 열린 파일의 위치를 position 바이트 지점으로 이동한다
		*/
		case SYS_SEEK:{
			int fd = f->R.rdi;
			unsigned position = (unsigned)f->R.rsi;

			if(fd < 2){
				return;
			}

			struct file *file = process_get_file(fd);
			
			if(file == NULL){
				return;
			}
			// 반환값이 없으면 rax에다가 집어넣을 필요가 없을까
			// seek() 가 바꾸는건 fd가 가리키는 열린 파일의 현재 offset 위치를 바꾼다.
			file_seek(file, (off_t)position);
			return;
		}
		/*
			열려 있는 파일에서 읽거나 쓸 다음 바이트의 위치를 fd​​파일 시작 부분부터 바이트 단위로 반환.
		*/
		case SYS_TELL:{
			int fd = f->R.rdi;
			
			if(fd < 2){
				f->R.rax = -1;
				return;
			}

			struct file *file = process_get_file(fd);

			if(file == NULL){
				f->R.rax = -1;
				return;
			}

			f->R.rax = file_tell(file);
			return;
		}
			

		case SYS_CLOSE:
			break;

		default:
			break;
	}
}
