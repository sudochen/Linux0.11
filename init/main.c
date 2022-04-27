/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>


/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 *
 * ���ҿ���ֻ��Ҫfork����pause����һ��Ϊinline�Ϳ��ԣ�ͨ���޸ĳ���Ҳ֤ʵ����
 * ��������һ��������һ������
 * forkʱ�����ص�ַ��ջ������ϵͳ���ã�������ɺ�������������л�
 * �ᵼ��task0���У�task0���лὫpause�ĵ�ַ��ջ��
 *
 *
 */
#ifndef K_INLINE
#define K_INLINE __attribute__((always_inline))
#endif

static inline K_INLINE _syscall0(int,fork)
static inline K_INLINE _syscall0(int,pause)
static _syscall1(int,setup,void *,BIOS)
static _syscall0(int,sync)
static _syscall0(pid_t,setsid)
static _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static _syscall1(int,dup,int,fd)
static _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static _syscall3(int,open,const char *,file,int,flag,int,mode)
static _syscall1(int,close,int,fd)
static _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)
static _syscall0(int,getpid)

static pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}

static inline K_INLINE void init(void);


#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long get_total_pages(void);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 * ��Щ��ַ���Է��ʵ�ԭ��������0��ַ��0xc0000000��ʼ��4MB��ӳ�䵽ͬһ����
 *
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

extern int printk(const char * fmt, ...);

struct drive_info { char dummy[32]; } drive_info;

void start_kernel(int __a, int __b, int __c)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */

/*******************************************************************************
	ORIG_ROOT_DEVΪ0x901FC��������ǰ��ĳ����лؼ�bootsect���򿽱���0x90000����
	bootsect��508��ַ��ŵ�ʱ���豸�ı��301��0x1fc=508�����д˴���ŵ��Ǹ��豸
	���豸��;
	DRIVE_INFO��ŵ��ǵ�һ��Ӳ����Ϣ
	EXT_MEM_Kϵͳ��1MB��ʼ����չ�ڴ���ֵ(KB)����ϰһ��ʵģʽ��������1MB�ռ�
	memory_end & 0xffff000�����ڴ���룬���ǿ���������3��0��һ��12λ���������
	֪���ں�Ҫ��ҳ���뼴4KB����

	���Ǹ��ݴ��뿴�����Ӳ�̴���16MB�����ڴ�Ϊ16MB,
	����ڴ����12MB, buffer_memory_endΪ4MB
	����ڴ����6MB��buffer_memory_endΪ2MB
	����buffer_memory_endΪ1M
	memory_end���16MB
*******************************************************************************/
 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 6*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK_SIZE
	main_memory_start += rd_init(main_memory_start, RAMDISK_SIZE*1024);
#endif
/*******************************************************************************
	����mem_map���飬����main_memory_start��memory_end֮����ڴ�
	��4KBΪһ�飬���д���
*******************************************************************************/
	mem_init(main_memory_start,memory_end);
	trap_init();
	blk_dev_init();
	chr_dev_init();
	tty_init();
	printk("params a %d b %d c %d\n", __a, __b, __c);
	printk("mem_start is %dMB\n", main_memory_start/(1024*1024));
	printk("men_end is %dMB\n", memory_end/(1024*1024));
	printk("system has %d pages omg\n", get_total_pages());
#ifdef RAMDISK_SIZE
	printk("ramdisk size is %dMB", RAMDISK_SIZE/1024);
#endif
	time_init();
	sched_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	show_mem();

	/*
	 * sti�����ж�
	 *
	 */	
	sti();
	/*
	 ִ����move_to_user_mode�����󣬳�����ֹ��е�task0ִ�У������κ����ݶ�
	 ���ں�һ�£���ջҲʹ�����ں˶�ջ������Ȩ�ޱ��3, ������ǿ��Կ���Linux��
	 ʹ��0��3Ȩ��
	 ������Բ鿴move_to_user_mode����
 	*/
	move_to_user_mode();

	/*	
     fork������һ��ϵͳ���ã�ʹ��_syscall0����չ�����ɣ�0��ʾû�в���
     
     #define _syscall0(type,name) \
     	type name(void) \
     	{ \
     	long __res; \
     	__asm__ volatile ("int $0x80" \
     	 : "=a" (__res) \
     	 : "0" (__NR_##name)); \
     	if (__res >= 0) \
     	 return (type) __res; \
     	errno = -__res; \
     	return -1; \
     
     ����ǰ��Ķ���static inline _syscall0(int,fork) չ��
     
     int fork() {
     	register eax __ret;
     	eax= __NR_fork;
     	int 0x80
     	if (eax >= 0)
     		return int __res;
     	error = - __res
     	return -1
     }
     INT 0x80�����жϺ��������������Ϊ:
     CPUͨ���ж�����0x80�ҵ���Ӧ�����������������������˶�ѡ���Ӻ�ƫ�Ƶ�ַ�Ѿ�DPL
     CPU��鵱ǰ��DPL�Ƿ�С����������DPL
     CPU��ӵ�ǰTSS�����ҵ��жϴ�������ջѡ���Ӻ�ջָ����Ϊ�µ�ջ��ַ(tss.ss0, tss.esp0)
     ���DPL�����仯�򽫵�ǰ��SS, ESP, EFLAGS, CS, EIPѹ���µ�ջ��
     ���DPLû�з����仯��EFLAGS, CS, EIPѹ���µ�ջ��
     CPU���ж���������ȡCS:EIP��Ϊ�µ����е�ַ
     
     forkִ����Ϻ��ǽ���1����ʱ����0�ͽ���1ʹ����ͬ���û��ռ�ջ��
     Ϊ�˽���֮�以��Ӱ�����
     ��ʱ��ʹ��ջ����������������ʽ���е��ã�����һ��
     ����Ժ������õ��γɵ�forkʱ�����л���ϵͳ����ǰ��SS, SPѹջ��
     ��ʱpause����Ҳ��SS, SPѹջ
     pause�Ķ�ջ���ݻḲ��fork�Ķ�ջ���ݣ�ʹfork�������ص�pause�������
     �Ӷ�init����ִ��
     ͬ��Ҳ���ܵ���pause����ִ�е�init������
	*/
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

#ifdef CONFIG_VGA
const char *ttydev = "/dev/tty0";
#else
const char *ttydev = "/dev/tty1";
#endif

static inline K_INLINE void init(void)
{
	int pid, i;

	setup((void *) &drive_info);
	(void) open(ttydev,O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

	if (!(pid=fork())) {
		printf("init fork current pid is %d\n", getpid());
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
		while (pid != wait(&i))

	/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open(ttydev,O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1) {
			if (pid == wait(&i)) {
				break;
			}
			
		}
		printf("\n\rchild %d died with code %04x\n\r",pid, i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
