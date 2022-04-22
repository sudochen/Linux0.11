# ��Ȼ��16λָ���80386ʵģʽ��������1MB�ڴ�
	.code16
# rewrite with AT&T syntax by falcon <wuzhangjin@gmail.com> at 081012
#
#	setup.s		(C) 1991 Linus Torvalds
#
# setup.s is responsible for getting the system data from the BIOS,
# and putting them into the appropriate places in system memory.
# both setup.s and system has been loaded by the bootblock.
#
# This code asks the bios for memory/disk/other parameters, and
# puts them in a "safe" place: 0x90000-0x901FF, ie where the
# boot-block used to be. It is then up to the protected mode
# system to read them from there before the area is overwritten
# for buffer-blocks.
#

# NOTE! These had better be the same as in bootsect.s!

	.equ INITSEG, 0x9000	# we move boot here - out of the way
	.equ SYSSEG, 0x1000		# system loaded at 0x10000 (65536).
	.equ SETUPSEG, 0x9020	# this is the current segment

	.global _start, begtext, begdata, begbss, endtext, enddata, endbss
	.text
	begtext:
	.data
	begdata:
	.bss
	begbss:
	.text
#
# 
#	ljmp $SETUPSEG, $_start
# ���ϵĴ���ɾ����ϵͳ��Ȼ�������У����ϵĴ�����޸�CS�Ĵ���������
# ��Ϊֱ����ת��_start��ַ������Ҳ����, �����ע����
#
# 
_start:

# ok, the read went well so we get current cursor position and save it for
# posterity.
# address	bytes	name		description
# 0x90000	2		���λ��	�кţ�0x00����ˣ����кţ�0x00��ˣ�
# 0x90002	2		��չ�ڴ���	ϵͳ��1M��ʼ����չ�ڴ���ֵ��KB����ʵģʽ��������1M�ռ�	

# ��ȡ��ǰ����λ�ã�����ڵ�ַ0x90000������Ϊbootsect�����ʱ�Ѿ�û���ˣ���512���ֽڣ�
# 
	mov	$INITSEG, %ax	# this is done in bootsect already, but...
	mov	%ax, %ds		# DS = 0x9000
	mov	$0x03, %ah		# read cursor pos
	xor	%bh, %bh
	int	$0x10			# save it in known place, con_init fetches
	mov	%dx, %ds:0		# it from 0x90000. save current in 0x90000
#
# ��ȡmem�Ĵ�С���������0x90002��
# Get memory size (extended mem, kB)
	mov	$0x88, %ah 
	int	$0x15
	mov	%ax, %ds:2		# mem size save int 0x90002

#
# ��ȡ�������ݴ����0x90004,0x90006�������ĸ��ֽ�
# Get video-card data:
	mov	$0x0f, %ah
	int	$0x10
	mov	%bx, %ds:4	# bh = display page, address is 0x90004
	mov	%ax, %ds:6	# al = video mode, ah = window width 0x90006

# ��ȡEGA/VGA���ݣ����δ����0x90008, 0x9000a,  0x9000c��
# check for EGA/VGA and some config parameters

	mov	$0x12, %ah
	mov	$0x10, %bl
	int	$0x10
	mov	%ax, %ds:8
	mov	%bx, %ds:10
	mov	%cx, %ds:12

# ��ȡhd0�����ݣ������0x90080,128���ֽ�ƫ�ƴ�����16���ֽ�
# Get hd0 data

	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4*0x41, %si
	mov	$INITSEG, %ax			# INITSEG = 0x9000
	mov	%ax, %es				# ES = 0x9000
	mov	$0x0080, %di			# destination address is ES:DI = 0x90080
	mov	$0x10, %cx
	rep
	movsb

# ��ȡhd1�����ݣ������0x90090,128+16=144���ֽ�ƫ�ƴ�����16���ֽ�
# Get hd1 data

	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4*0x46, %si
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di			# destination ES:DI = 0x90090
	mov	$0x10, %cx
	rep
	movsb

# Check that there IS a hd1 :-)

	mov	$0x01500, %ax
	mov	$0x81, %dl
	int	$0x13
	jc	no_disk1
	cmp	$3, %ah
	je	is_disk1
no_disk1:
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di
	mov	$0x10, %cx
	mov	$0x00, %ax
	rep
	stosb
is_disk1:

# now we want to move to protected mode ...
# ���ڣ�����Ҫ���뱣��ģʽ���ȹص��ն�

	cli							# no interrupts allowed ! 

# first we move the system to it's rightful place
# ����Ĵ��뽫0x10000���Ĵ��뿽����0x0000����������0x80000���ֽ�512KB��
# �ں˳�������ܳ���512K�ļٶ�ǰ��
#
#
	mov	$0x0000, %ax
	cld							# 'direction'=0, movs moves forward
do_move:
	mov	%ax, %es				# destination segment
	add	$0x1000, %ax
	cmp	$0x9000, %ax
	jz	end_move
	mov	%ax, %ds				# source segment
	sub	%di, %di
	sub	%si, %si
	mov $0x8000, %cx
	rep
	movsw
	jmp	do_move

# then we load the segment descriptors
# SETUPSEG 0x9020
# ��ǰ���ݶε�ַΪ0x9020
# ����GDT, IDT��������Ϊ����ģʽ��׼��
end_move:
	mov	$SETUPSEG, %ax			# right, forgot this at first. didn't work :-)
	mov	%ax, %ds				# DS = 0x9020 ��Ϊidt_48�����DS��ƫ�Ƶ�ַ��������Ĵ����иñ���DS��������ط��ָ�
	lidt idt_48					# load idt with 0,0
	lgdt gdt_48					# load gdt with whatever appropriate

# that was painless, now we enable A20

	#call	empty_8042			# 8042 is the keyboard controller
	#mov	$0xD1, %al			# command write
	#out	%al, $0x64
	#call	empty_8042
	#mov	$0xDF, %al			# A20 on
	#out	%al, $0x60
	#call	empty_8042
	inb     $0x92, %al			# open A20 line(Fast Gate A20).
	orb     $0b00000010, %al
	outb    %al, $0x92

# well, that went ok, I hope. Now we have to reprogram the interrupts :-(
# we put them right after the intel-reserved hardware interrupts, at
# int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
# messed this up with the original PC, and they haven't been able to
# rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
# which is used for the internal hardware interrupts as well. We just
# have to reprogram the 8259's, and it isn't fun.

	mov	$0x11, %al				# initialization sequence(ICW1)
								# ICW4 needed(1),CASCADE mode,Level-triggered
	out	%al, $0x20				# send it to 8259A-1
	.word	0x00eb,0x00eb		# jmp $+2, jmp $+2
	out	%al, $0xA0				# and to 8259A-2
	.word	0x00eb,0x00eb
	mov	$0x20, %al				# start of hardware int's (0x20)(ICW2)
	out	%al, $0x21				# from 0x20-0x27
	.word	0x00eb,0x00eb
	mov	$0x28, %al				# start of hardware int's 2 (0x28)
	out	%al, $0xA1				# from 0x28-0x2F
	.word	0x00eb,0x00eb		# IR 7654 3210
	mov	$0x04, %al				# 8259-1 is master(0000 0100) --\
	out	%al, $0x21				# |
	.word	0x00eb,0x00eb		# INT	/
	mov	$0x02, %al				# 8259-2 is slave(010 --> 2)
	out	%al, $0xA1
	.word	0x00eb,0x00eb
	mov	$0x01, %al				# 8086 mode for both
	out	%al, $0x21
	.word	0x00eb,0x00eb
	out	%al, $0xA1
	.word	0x00eb,0x00eb
	mov	$0xFF, %al				# mask off all interrupts for now
	out	%al, $0x21
	.word	0x00eb,0x00eb
	out	%al, $0xA1

# well, that certainly wasn't fun :-(. Hopefully it works, and we don't
# need no steenking BIOS anyway (except for the initial loading :-).
# The BIOS-routine wants lots of unnecessary data, and it's less
# "interesting" anyway. This is how REAL programmers do it.
#
# Well, now's the time to actually move into protected mode. To make
# things as simple as possible, we do no register set-up or anything,
# we let the gnu-compiled 32-bit programs do that. We just jump to
# absolute address 0x00000, in 32-bit protected mode.
# ����32λ����ģʽ��Ѱַ��ʽ�����仯

	mov	$0x0001, %ax			# protected mode (PE) bit
	lmsw %ax					# This is it!
#
# ��ʵģʽ�жμĴ�����ֵΪ�ε�ַ
# �ڱ���ģʽ�жμĴ�����Ϊ��ѡ�����������ŵ�ֵΪ��ѡ����
# �ڱ���ģʽ��$8��ʾ��ѡ���ӵ�Ϊ8���䶨������
# +--------------------+---------------+------------------+
# |    ������13bits��  | Ȩ�ޣ�2bits�� | ȫ��/�ֲ�(1bits) |
# +--------------------+---------------+------------------+
# $8�Ķ�����Ϊ00001000,�������Ͽ��Կ�����ȫ����������1�������Ȩ�ޣ�00��������Ϊ1
#
# ���ݺ����ȫ����������gdt����֪�����˴��Ǵ���Σ�����ַΪ0��Ҳ��Imageģ���Head�ĵ�ַ
#
	ljmp $8, $0					# jmp offset 0 of code segment 0 in gdt

# This routine checks that the keyboard command queue is empty
# No timeout is used - if this hangs there is something wrong with
# the machine, and we probably couldn't proceed anyway.
empty_8042:
	.word	0x00eb,0x00eb
	in	$0x64, %al				# 8042 status port
	test $2, %al				# is input buffer full?
	jnz	empty_8042				# yes - loop
	ret
#
# ��0��ַ��ʼ�������Σ����ݶκʹ���Σ�����ַΪ0������Ϊ8MB
#
gdt:
	.word	0,0,0,0				# dummy

	.word	0x07FF				# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000				# base address=0
	.word	0x9A00				# code read/exec
	.word	0x00C0				# granularity=4096, 386

	.word	0x07FF				# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000				# base address=0
	.word	0x9200				# data read/write
	.word	0x00C0				# granularity=4096, 386

idt_48:
	.word	0					# idt limit=0
	.word	0,0					# idt base=0L

#
# 0x800��ʾ���ƴ�С��0x800
# 512+gdt�ɱ�ʾ0x200+gdt����ʾsetupģ�����ڵĵĵ�ַ+gdtƫ�ƣ�Ҳ����gdt���������Ϣ
# LGDT, LIDTָ�������ݵ�ַ�����Ե�ַ��������ָ���ǽ��е��ܹ��������Ե�ַ��ָ��
# Ҳ����˵���ܶμĴ�����ʲôֵ��������0��������ָ��ͨ����ʵģʽ��ʹ��
# �Ա��ڴ��������л�������ģʽ֮ǰ���г�ʼ��
#
#
gdt_48:
	.word	0x800				# gdt limit=2048, 256 GDT entries
# 512+gdt is the real gdt after setup is moved to 0x9020 * 0x10
	.word   512+gdt, 0x9		# gdt base = 0X9xxxx, 
	
.text
endtext:
.data
enddata:
.bss
endbss:

