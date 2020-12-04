#include <defs.h>
#include <x86.h>
#include <elf.h>

/* *********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an ELF kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(bootasm.S and bootmain.c) is the bootloader.
 *    It should be stored in the first sector of the disk.
 *
 *  * The 2nd sector onward holds the kernel image.
 *
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive)
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in bootasm.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls bootmain()
 *
 *  * bootmain() in this file takes over, reads in the kernel and jumps to it.
 * */

#define SECTSIZE        512
#define ELFHDR          ((struct elfhdr *)0x10000)      // scratch space

/* waitdisk - wait for disk ready */
static void
waitdisk(void) {
    while ((inb(0x1F7) & 0xC0) != 0x40)
        /* do nothing */;
}

/* readsect - read a single sector at @secno into @dst */
static void
readsect(void *dst, uint32_t secno) {
    // wait for disk to be ready
    waitdisk();

    outb(0x1F2, 1);                         // count = 1，0x1F2位置代表要读取扇区的数量，这里1代表只读取一个扇区
    outb(0x1F3, secno & 0xFF);              //LBA 0~7 位 低八位         LBA28位就是要读的扇区号
    outb(0x1F4, (secno >> 8) & 0xFF);       //LBA 8～15 位 中八位
    outb(0x1F5, (secno >> 16) & 0xFF);      //LBA 16～23 位 高八位
    outb(0x1F6, ((secno >> 24) & 0xF) | 0xE0);//如果是LBA模式 前4位就是LBA 24～27位，第4位为0主盘，为1从盘
    outb(0x1F7, 0x20);                      //既是状态也是命令寄存器，第七位为1表示忙，第三位为1表示准备好了    写入0x20读取数据 写入0x30写入数据 

    // wait for disk to be ready
    waitdisk();

    // read a sector
    insl(0x1F0, dst, SECTSIZE / 4);         //从0x1F0读数据
}

/* *
 * readseg - read @count bytes at @offset from kernel into virtual address @va,
 * might copy more than asked.
 * */
static void
readseg(uintptr_t va, uint32_t count, uint32_t offset) {
    uintptr_t end_va = va + count;//elf程序段的起始虚拟地址加上程序段的长度，计算得出该程序段的虚拟地址的结束地址

    // round down to sector boundary
    va -= offset % SECTSIZE;

    // translate from bytes to sectors; kernel starts at sector 1
    uint32_t secno = (offset / SECTSIZE) + 1;

    // If this is too slow, we could read lots of sectors at a time.
    // We'd write more to memory than asked, but it doesn't matter --
    // we load in increasing order.
    for (; va < end_va; va += SECTSIZE, secno ++) {
        readsect((void *)va, secno);
    }
}

/* bootmain - the entry of bootloader */
void
bootmain(void) {
    // read the 1st page off disk
    readseg((uintptr_t)ELFHDR, SECTSIZE * 8, 0);

    // is this a valid ELF?
    if (ELFHDR->e_magic != ELF_MAGIC) {
        goto bad;
    }

    struct proghdr *ph, *eph;

    // load each program segment (ignores ph flags)
    ph = (struct proghdr *)((uintptr_t)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;
    for (; ph < eph; ph ++) {                           //循环读取每个program header（注：ph是proghdr结构指针，+1即代表讲指针指向下一个proghdr结构）
        readseg(ph->p_va & 0xFFFFFF, ph->p_memsz, ph->p_offset);//由于用的是简易的GDT，故真实地址即虚拟地址，所以第一个参数直接使用真实地址，第二个参数是段的大小，第三个参数是相对于头的偏移地址，因为内核是直接从1扇区开始写入的，readseg也是从1扇区写入的，所以不需要再添加头的基址（0）
    }

    // call the entry point from the ELF header
    // note: does not return
    ((void (*)(void))(ELFHDR->e_entry & 0xFFFFFF))();   //跳转到ELFHDR的程序入口虚拟地址

bad:
    outw(0x8A00, 0x8A00);
    outw(0x8A00, 0x8E00);

    /* do nothing */
    while (1);
}

