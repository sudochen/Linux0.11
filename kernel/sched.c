/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

#define _S(nr) (1<<((nr)-1))
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

void show_task(int nr,struct task_struct * p)
{
	int i,j = 4096-sizeof(struct task_struct);

	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);
	i=0;
	while (i<j && !((char *)(p+1))[i])
		i++;
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}

void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)
		if (task[i])
			show_task(i,task[i]);
}

#define LATCH (1193180/HZ)

extern void mem_use(void);

extern int timer_interrupt(void);
extern int system_call(void);

union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE];
};

static union task_union init_task = {INIT_TASK,};
struct tss_struct *tss = &(init_task.task.tss);
long volatile jiffies=0;
long startup_time=0;
struct task_struct * current = &(init_task.task);
struct task_struct * last_task_used_math = NULL;
struct task_struct * task[NR_TASKS] = {&(init_task.task), };

long user_stack [ PAGE_SIZE>>2 ] ;

struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
void math_state_restore()
{
	if (last_task_used_math == current)
		return;
	__asm__("fwait");
	if (last_task_used_math) {
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	last_task_used_math=current;
	if (current->used_math) {
		__asm__("frstor %0"::"m" (current->tss.i387));
	} else {
		__asm__("fninit"::);
		current->used_math=1;
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */

extern void switch_to_by_stack(long, long, long);
void schedule(void)
{
	int i,next,c;
	struct task_struct *pnext = &(init_task.task);
	struct task_struct ** p;
	int flag;

/* check alarm, wake up any interruptible tasks that have got a signal */


	local_irq_disable(flag);
	
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		if (*p) {
			if ((*p)->alarm && (*p)->alarm < jiffies) {
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
				}
			if (((*p)->signal & (_BLOCKABLE & ~(*p)->blocked)) &&
			(*p)->state==TASK_INTERRUPTIBLE)
				(*p)->state=TASK_RUNNING;
		}

	/* this is the scheduler proper: */

	while (1) {
		c = -1;
		next = 0;
		pnext = task[next];
		
		i = NR_TASKS;
		p = &task[NR_TASKS];
		while (--i) {
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, pnext = *p, next = i; 
		}
		if (c) break;
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
			if (*p)
				(*p)->counter = ((*p)->counter >> 1) +
						(*p)->priority;
	}

	local_irq_restore(flag);

#ifdef CONFIG_SWITCH_TSS
	switch_to(next);
#else
	switch_to_by_stack((long)pnext, (long)(_LDT(next)), pnext->tss.cr3);
#endif
}

int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp = *p;
	*p = current;
	current->state = TASK_UNINTERRUPTIBLE;
	schedule();
	*p = tmp;
	if (tmp)
		tmp->state=TASK_RUNNING;
}

void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
		return;
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
	schedule();
	if (*p && *p != current) {
		(*p)->state = TASK_RUNNING;
		goto repeat;
	}
	*p = tmp;
	if (tmp)
		tmp->state = TASK_RUNNING;
}

void wake_up(struct task_struct **p)
{
	if (p && *p) {
		(*p)->state = TASK_RUNNING;
		*p = NULL;
	}
}


/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;

int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	if (nr>3)
		panic("floppy_on: nr>3");
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	if (!selected) {
		mask &= 0xFC;
		mask |= nr;
	}
	if (mask != current_DOR) {
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ/2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}
	sti();
	return mon_timer[nr];
}

void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
		sleep_on(nr+wait_motor);
	sti();
}

void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}

void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) {
		if (!(mask & current_DOR))
			continue;
		if (mon_timer[i]) {
			if (!--mon_timer[i])
				wake_up(i+wait_motor);
		} else if (!moff_timer[i]) {
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} else
			moff_timer[i]--;
	}
}

#define TIME_REQUESTS 64

static struct timer_list {
	long jiffies;
	void (*fn)();
	struct timer_list * next;
} timer_list[TIME_REQUESTS], * next_timer = NULL;

void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	if (!fn)
		return;
	cli();
	/* 
	 * 如果timer时间小于等于0，立即执行其回掉函数
	 *
	 */
	if (jiffies <= 0)
		(fn)();
	else {
		/*
		 * 在数组中找到一个可用的timer_list, 如果没有找到则panic
		 *
		 */
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
			if (!p->fn)
				break;
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");
			
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
		while (p->next && p->next->jiffies < p->jiffies) {
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();
}

void do_timer(long cpl)
{
	extern int beepcount;
	extern void sysbeepstop(void);

	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	/*
	 * 增加内核时间或用户时间计数
	 * 如果是内核态增加stime，否则增加utime
	 *
	 */
	if (cpl)
		current->utime++;
	else
		current->stime++;

	/*
	 * 如果有定时器存在则处理定制器相关
	 */
	if (next_timer) {
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) {
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();
		}
	}
	
	if (current_DOR & 0xf0)
		do_floppy_timer();

	if ((--current->counter)>0) 
		return;

	current->counter=0;
	/*
	 * 这句话很重要，也就是如果是在内核态不进行调度，内核否则就涉及一个
	 * 概念叫内核抢占，因此我们知道，在linux内核程序被中断后，中断推出
	 * 是一定退出到被中断的地方，但是用户态的程序则可能发生进程切换
	 *
	 */
	if (!cpl) 
		return;
		
	schedule();
}

int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}

int sys_getpid(void)
{
	return current->pid;
}

int sys_getppid(void)
{
	return current->father;
}

int sys_getuid(void)
{
	return current->uid;
}

int sys_geteuid(void)
{
	return current->euid;
}

int sys_getgid(void)
{
	return current->gid;
}

int sys_getegid(void)
{
	return current->egid;
}

int sys_nice(long increment)
{
	if (current->priority-increment>0)
		current->priority -= increment;
	return 0;
}

void sched_init(void)
{
	int i;
	struct desc_struct * p;
	
	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");
	
	/*
	 * FIRST_TSS_ENTRY定义为4，表示第一个进程的TSS描述符的地址索引
	 * FIRST_LDT_ENTRY定义为5，表示第一个进程的LDT描述符的地址索引
	 */
	set_tss_desc(gdt+FIRST_TSS_ENTRY, &(init_task.task.tss));
	set_ldt_desc(gdt+FIRST_LDT_ENTRY, &(init_task.task.ldt));

#ifdef CONFIG_SWITCH_TSS
	printk("task switch use TSS\n");
#else
	printk("task switch use KERNEL STACK\n");
#endif

	printk("init_task use GTD[%d] for TSS\n", FIRST_TSS_ENTRY);
	printk("init_task use GTD[%d] for LDT\n", FIRST_LDT_ENTRY);
	
	/* 
	 * 此时p为gdt的第6项，也就是说从第六项开始清理gdt为0，
	 * 每次清理两项，也就是TSS和LDT
	 * 
	 */
	p = gdt+2+FIRST_TSS_ENTRY;
	for(i=1; i<NR_TASKS; i++) {
		task[i] = NULL;
		p->a = p->b=0;
		p++;
		p->a = p->b=0;
		p++;
		
	}
	/*
	 * 如下代码
	 * Clear NT, so that we won't have troubles with that later on 
	 * 防止任务嵌套和iret时发生任务切换，清NT(bit16)和RF(bit14)位
	 * pushfl指令是push flags long的缩写，意思是将标志寄存器压栈
	 * popfl是将标志寄存器出栈
	 * NT用于控制iret的执行具体如下
	 * NT = 0 时，用堆栈中保存的值恢复EFlag、CS(代码段寄存器)和EIP(32位指令指针寄存器)，执行常规的中断返回操作；
	 * NT = 1 时, 通过任务转换实现中断返回。
	 */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");

	/*
	 * 加载TSS和LDT段描述符
	 *
	 */
	ltr(0);
	lldt(0);

	/*
	 * outb_p(0x36,0x43);						
	 * outb_p(LATCH & 0xff , 0x40);			
	 * outb(LATCH >> 8 , 0x40);				
	 * 定时器操作，知道就行
	 * 操作系统就是根据定时器中断驱动进行任务切换
	 */
	outb_p(0x36,0x43);						/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);			/* LSB */
	outb(LATCH >> 8 , 0x40);				/* MSB */
	printk("Enable timer_interrupt\n");
	/*
	 * 安装时钟中断门
	 */
	set_intr_gate(0x20,&timer_interrupt);
	outb(inb_p(0x21)&~0x01,0x21);
	/*
	 * 安装系统调用的系统门
	 */
	printk("Enable system_call\n");
	set_system_gate(0x80,&system_call);
}

