/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

void do_exit(long code);

static inline void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}

#define invalidate() \
__asm__ __volatile__("movl %%cr3,%%eax\n\tmovl %%eax,%%cr3":::"ax")


/* these are not to be changed without changing head.s etc */

#define PAGING_MEMORY 		(16*1024*1024)
#define PAGING_PAGES 		(PAGING_MEMORY>>12)
#define MAP_NR(addr) 		(((unsigned long)(addr))>>12)
#define USED 				(1<<7)
#define PAGE_DIRTY			0x40
#define PAGE_ACCESSED		0x20
#define PAGE_USER			0x04
#define PAGE_RW				0x02
#define PAGE_PRESENT		0x01
#define PAGE_SHIFT 			12


static long HIGH_MEMORY = 0;
static long total_pages = 0;

/* chenwg
 * ����һҳ4KB���ڴ�
 *
 */
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024))

static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 * 
 * ��ȡһ������ҳ�������ַ����ȡ��ҳ�����ַ��LOW_MEM��ʼ
 * ����ɹ�����һ�������ַ�����û�з���0
 * 
 *
 */
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t"
	"jne 1f\n\t"
	"movb $1,1(%%edi)\n\t"
	"sall $12,%%ecx\n\t"
	"addl %2,%%ecx\n\t"
	"movl %%ecx,%%edx\n\t"
	"movl $1024,%%ecx\n\t"
	"leal 4092(%%edx),%%edi\n\t"
	"rep ; stosl\n\t"
	" movl %%edx,%%eax\n"
	"1: cld"
	:"=a" (__res)
	:"0" (0),"i" (0),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	);
return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 *
 * �ͷ�һ��ҳ�������ַС��LOW_MEM 1MB����ֱ���˳�
 * addrΪ�����ַ
 */
void free_page(unsigned long addr)
{
	int i = 0;
	
	if (addr >= HIGH_MEMORY) {
		panic("trying to free nonexistent page");
	}

	i = MAP_NR(addr);

	if (mem_map[i] & USED) {
		printk("system reserve mem, ignore free\n");
		return;
	}

	if (!mem_map[i]) {
		panic("trying to free free page");
	}

	mem_map[i]--;
	return;
}

static void free_one_table(unsigned long * page_dir)
{

	int j;
	unsigned long pg_table = *page_dir;
	unsigned long * page_table;

	if (!pg_table)
		return;
		
	if (pg_table >= HIGH_MEMORY|| !(pg_table & 1)) {
		printk("Bad page table: [%08x]=%08x\n",page_dir,pg_table);
		*page_dir = 0;
		return;
	}
	
	*page_dir = 0;
	if (mem_map[MAP_NR(pg_table)] & USED) {
		return;
	}
		
	page_table = (unsigned long *) (pg_table & 0xfffff000);
	for (j = 0 ; j < 1024 ; j++,page_table++) {
		unsigned long pg = *page_table;
		
		if (!pg)
			continue;
			
		if (mem_map[MAP_NR(pg)] & USED)
			continue;
			
		*page_table = 0;
		if (1 & pg)
			free_page(0xfffff000 & pg);
	}
	free_page(0xfffff000 & pg_table);
}

void clear_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long * page_dir;
	unsigned long tmp;

	if (!tsk)
		return;
	if (tsk == task[0])
		panic("task[0] (swapper) doesn't support exec() yet\n");


	page_dir = (unsigned long *) tsk->tss.cr3;
	if (!page_dir) {
		printk("Trying to clear kernel page-directory: not good\n");
		return;
	}
	for (i = 0 ; i < 768 ; i++,page_dir++)
		free_one_table(page_dir);
	invalidate();
	return;
}


/*
 * This function frees up all page tables of a process when it exits.
 */
int free_page_tables(struct task_struct * tsk)
{
	int i;
	unsigned long pg_dir;
	unsigned long * page_dir;

	if (!tsk)
		return 1;
		
	if (tsk == task[0]) {
		panic("Trying to free up task[0] (swapper) memory space");
	}
	
	pg_dir = tsk->tss.cr3;
	if (!pg_dir) {
		printk("Trying to free kernel page-directory: not good\n");
		return 1;
	}
	
	tsk->tss.cr3 = (unsigned long) swapper_pg_dir;
	if (tsk == current)
		__asm__ __volatile__("movl %0,%%cr3"::"a" (tsk->tss.cr3));
		
	page_dir = (unsigned long *) pg_dir;
	for (i = 0 ; i < 1024 ; i++,page_dir++)
		free_one_table(page_dir);
		
	*page_dir = 0;
	free_page(pg_dir);
	invalidate();
	return 0;
}


/*
 * copy_page_tables() just copies the whole process memory range:
 * note the special handling of RESERVED (ie kernel) pages, which
 * means that they are always shared by all processes.
 */
int copy_page_tables(struct task_struct * tsk)
{
	int i;
	int page_count = 1024;
	unsigned long old_pg_dir, *old_page_dir;
	unsigned long new_pg_dir, *new_page_dir;

	old_pg_dir = current->tss.cr3;
	new_pg_dir = get_free_page();
	if (!new_pg_dir)
		return -1;
	
	tsk->tss.cr3 = new_pg_dir;
	old_page_dir = (unsigned long *) old_pg_dir;
	new_page_dir = (unsigned long *) new_pg_dir;

	/* 
	 * old_pg_dir�����0����ʾ��һ�����̣���һ������ֻ��160��Ҳ��Ч������
	 * �����Խ�ʡ�ܶ��ڴ�
	 *
	 */
	if (current == task[0] || current == task[1]) {
		page_count = 160;
	} else {
		page_count = 1024;
	}
	
	for (i = 0 ; i < 1024 ; i++,old_page_dir++,new_page_dir++) {
		int j;
		unsigned long old_pg_table, *old_page_table;
		unsigned long new_pg_table, *new_page_table;

		old_pg_table = *old_page_dir;
		if (!old_pg_table)
			continue;
		if (old_pg_table >= HIGH_MEMORY || !(1 & old_pg_table)) {
			printk("copy_page_tables: bad page table: "
				"probable memory corruption  %d %p\n", i, old_pg_table);
			*old_page_dir = 0;
			continue;
		}

		/* 
		 * i >= 768��ʾ3GB���ϵ��ںˣ�3GB���ϵ��ڴ��ʾ�ں˿ռ�
		 * ���н��̹����ں˿ռ䣬�ں˿ռ��ҳ����һ��
		 *
		 */
		if (mem_map[MAP_NR(old_pg_table)] & USED && i >= 768) {
			*new_page_dir = old_pg_table;
			continue;
		}

		new_pg_table = get_free_page();
		if (!new_pg_table) {
			free_page_tables(tsk);
			return -1;
		}
		*new_page_dir = new_pg_table | PAGE_ACCESSED | 7;
		old_page_table = (unsigned long *) (0xfffff000 & old_pg_table);
		new_page_table = (unsigned long *) (0xfffff000 & new_pg_table);
		for (j = 0 ; j < page_count ; j++,old_page_table++,new_page_table++) {
			unsigned long pg;
			pg = *old_page_table;
			if (!pg)
				continue;
			if (!(pg & PAGE_PRESENT)) {
				continue;
			}
			pg &= ~2;
			*new_page_table = pg;
			if (mem_map[MAP_NR(pg)] & USED)
				continue;
			*old_page_table = pg;
			mem_map[MAP_NR(pg)]++;
		}
	}
	invalidate();
	return 0;
}

unsigned long put_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;
	struct task_struct *tsk = current;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page >= HIGH_MEMORY)
		printk("put_dirty_page: trying to put page %p at %p\n",page,address);
		
	if (mem_map[MAP_NR(page)] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
		
	page_table = (unsigned long *) (tsk->tss.cr3 + ((address>>20) & 0xffc));
	
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp | PAGE_ACCESSED |7;
		page_table = (unsigned long *) tmp;
	}
	page_table += (address >> PAGE_SHIFT) & 0x3ff;
	if (*page_table) {
		printk("put_dirty_page: page already exists\n");
		*page_table = 0;
		invalidate();
	}
	*page_table = page | (PAGE_DIRTY | PAGE_ACCESSED | 7);
/* no need for invalidate */
	return page;
}



/*
 * table_entryҳ����ָ��
 */
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	
	/* ��ȡ��ҳ��Ӧ�������ַ�����ԭҳ�治�Ǳ���������ֵΪ1��ʾû�й�������д���
	 *
	 */
	old_page = 0xfffff000 & *table_entry;

	if (!(mem_map[MAP_NR(old_page)] & USED) && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page())) {
		printk("%s-%d\n", __func__, __LINE__);
		oom();
	}

	/*
	 *  ���ԭҳ�治�Ǳ����Ҳ�Ϊ1����ʾ�Ѿ�����������ֵ��һ�������µ�ҳ��
	 *
	 */
	if (!(mem_map[MAP_NR(old_page)] & USED))
		mem_map[MAP_NR(old_page)]--;
	
	*table_entry = new_page | 7;
	invalidate();
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
	unsigned long* dir_base = (unsigned long *)current->tss.cr3;
	unsigned long* dir_item = dir_base + (address >> 22);
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 & *dir_item)));
}

void write_verify(unsigned long address)
{
	unsigned long page;

	page = *(unsigned long *) (current->tss.cr3 + ((address>>20) & 0xffc));
	if (!(page & PAGE_PRESENT))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		printk("%s-%d tmp: %p\n", __func__, __LINE__, tmp);
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	struct task_struct *tsk = current;
	
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = p->tss.cr3 + ((address>>20) & 0xffc);
	to_page = tsk->tss.cr3 + ((address>>20) & 0xffc);
	/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
	/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY)
		return 0;
	if (mem_map[MAP_NR(phys_addr)] & USED)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		to = get_free_page();
		if (!to)
			return 0;
		*(unsigned long *) to_page = to | PAGE_ACCESSED | 7;
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr >>= PAGE_SHIFT;
	mem_map[phys_addr]++;
	return 1;

}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
		
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	
	address &= 0xfffff000;
	tmp = address - current->start_code;

#ifdef KDEBUG
	printk("%s-%d do no page address %p start_code %p end_data %p\n", 
		__func__, __LINE__, address, current->start_code, current->end_data);
#endif

	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	if (share_page(tmp))
		return;
	if (!(page = get_free_page())) {
		printk("%s-%d\n", __func__, __LINE__);
		oom();
	}
		
	/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;	
	free_page(page);
	oom();
}


long get_total_pages(void)
{
	return total_pages;
}

/*******************************************************************************
	start_mem��������ҳ�������ڴ����ʼ��ַ
	end_mem ʵ�������ڴ������ַ
*******************************************************************************/
void mem_init(long start_mem, long end_mem)
{
	int i;

/*******************************************************************************
	HIGH_MEMORY��һ�����������ڼ�¼��ǰ�ڴ���������

	PAGING_PAGES����Ϊ(PAGING_MEMORY>>12)
	PAGING_MEMORY��ֵΪ15*1024*1024Ϊ15MB����Linux�ں��������ʹ�õ��ڴ�Ϊ16MB
	��͵�1MB�����ں�ϵͳ�����ڴ�����ڣ���LOW��ֵΪ0x100000

	�����ϵͳ�ʼ���Ƚ����е�ҳ�������Ϊ���ú����ڸ���ʵ���ڴ����������

	MAP_NR(addr)����Ϊ(((addr) - LOW_MEM) >> 12)��ʾҳ��ţ����ǿ��Կ���ҳ���
	ȥ������͵�1MB�ռ䣬����ҳ�����start_mem��ʼ��Ҳ����˵buffer��ramdisk����
	Ҳ��������Ϊ����

	end_mem -= start_mem����������ڴ�Ĵ�С
	end_mem >>= 12 ����12λ�൱�ڳ���4096����ʾ�����ڴ��Сռ�õ�ҳ��
	Ȼ������mem_map��Ӧ�ı�־

	��ʱϵͳ�����ڴ��Ѿ���mem_map���й����ˣ���Щҳʹ�ù�����Щҳ��δʹ��
	һĿ��Ȼ
*******************************************************************************/

	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	total_pages= end_mem;
	while (end_mem-->0)
		mem_map[i++]=0;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	
	i = HIGH_MEMORY >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (mem_map[i] & USED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
	}
	printk("Buffer blocks:   %6d\n", nr_buffers);
	printk("Tatal pages:     %6d\n", total);
	printk("Free pages:      %6d\n", free);
	printk("Reserved pages:  %6d\n", reserved);
	printk("Shared pages:    %6d\n", shared);
}
