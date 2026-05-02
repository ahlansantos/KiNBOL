#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "mm/pmm.h"
#include "mm/heap.h"
#include "drivers/keyboard.h"
#include "drivers/rtc.h"
#include "graphics/font.h"
#include "kernel/dmesg.h"
#include "fs/vfs.h"

uint64_t hhdm_offset = 0;

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static struct limine_framebuffer *fbi;
static uint32_t pw;
static uint32_t gx = 8, gy = 8;
static uint32_t fg = 0xFFFFFF, bg = 0x00111122;
static uint64_t total_ram = 0;
static uint64_t tsc_hz    = 0;
static uint64_t boot_tsc  = 0;

static struct limine_memmap_response *g_memmap = NULL;

static inline void outb(uint16_t p, uint8_t v) {
    asm volatile("outb %0,%1" :: "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v;
}
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint16_t pit_read(void) {
    outb(0x43, 0x00);
    return (uint16_t)inb(0x40) | ((uint16_t)inb(0x40) << 8);
}
static void tsc_calibrate(void) {
    outb(0x43, 0x34); outb(0x40, 0xFF); outb(0x40, 0xFF);
    while (pit_read() < 60000) asm volatile("pause");
    while (pit_read() < 60000) asm volatile("pause");
    uint16_t start = pit_read(); uint64_t t0 = rdtsc();
    uint16_t target = start - 10000;
    while (pit_read() > target) asm volatile("pause");
    uint64_t cycles = rdtsc() - t0;
    tsc_hz = (cycles * 1193182ULL) / 10000ULL;
    if (tsc_hz < 100000000ULL) tsc_hz = 1000000000ULL;
}
static uint64_t uptime_ms(void) {
    if (!tsc_hz) return 0;
    return ((rdtsc() - boot_tsc) * 1000ULL) / tsc_hz;
}
static uint32_t get_ticks(void) { return (uint32_t)(uptime_ms() / 10); }
static void sleep_ms(uint32_t ms) {
    uint64_t end = rdtsc() + (tsc_hz * (uint64_t)ms) / 1000ULL;
    while (rdtsc() < end) asm volatile("pause");
}

typedef struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idt_ptr_t;
typedef struct {
    uint16_t base_low, selector; uint8_t ist, flags;
    uint16_t base_mid; uint32_t base_high, reserved;
} __attribute__((packed)) idt_entry_t;
static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;
static void idt_set_gate(uint8_t n, uint64_t h, uint16_t s, uint8_t f) {
    idt[n].base_low=h&0xFFFF; idt[n].base_mid=(h>>16)&0xFFFF;
    idt[n].base_high=(h>>32)&0xFFFFFFFF;
    idt[n].selector=s; idt[n].ist=0; idt[n].flags=f; idt[n].reserved=0;
}

static void px(uint32_t x, uint32_t y, uint32_t c) {
    if (x < fbi->width && y < fbi->height)
        ((volatile uint32_t *)fbi->address)[y * pw + x] = c;
}
static void chr(uint32_t x, uint32_t y, char c, uint32_t cf, uint32_t cb) {
    const uint8_t *g = font[(unsigned char)c];
    for (int r = 0; r < 16; r++)
        for (int co = 0; co < 8; co++)
            px(x+co, y+r, (g[r]&(1<<(7-co))) ? cf : cb);
}
static void scroll(void) {
    volatile uint32_t *fbb = fbi->address;
    for (uint32_t r = 0; r < fbi->height-16; r++) {
        uint32_t *d=(uint32_t*)(fbb+r*pw), *s=(uint32_t*)(fbb+(r+16)*pw);
        for (uint32_t c = 0; c < fbi->width; c++) d[c]=s[c];
    }
    for (uint32_t r = fbi->height-16; r < fbi->height; r++) {
        uint32_t *p=(uint32_t*)(fbb+r*pw);
        for (uint32_t c = 0; c < fbi->width; c++) p[c]=bg;
    }
    gy = fbi->height-16;
}
static void put(char c) {
    if (c=='\n'){gx=8;gy+=16;if(gy>=fbi->height-16)scroll();return;}
    if (c=='\b'){if(gx>=16){gx-=8;chr(gx,gy,' ',fg,bg);}return;}
    chr(gx,gy,c,fg,bg); gx+=8;
    if (gx>=fbi->width-8){gx=8;gy+=16;if(gy>=fbi->height-16)scroll();}
}
static void print(const char *s)   { for(int i=0;s[i];i++) put(s[i]); }
static void println(const char *s) { print(s); put('\n'); }
static void print_int(uint32_t n) {
    char b[11]; int i=10; b[i--]=0;
    do{b[i--]='0'+n%10;n/=10;}while(n); print(&b[i+1]);
}
static void print_hex(uint64_t n) {
    char h[]="0123456789ABCDEF"; print("0x");
    for(int i=15;i>=0;i--) put(h[(n>>(i*4))&0xF]);
}
static void clear(void) {
    for(uint32_t y=0;y<fbi->height;y++)
        for(uint32_t x=0;x<fbi->width;x++) px(x,y,bg);
    gx=8; gy=8;
}
void terminal_putchar(char c) { put(c); }

static void cursor_draw(int visible) {
    uint32_t color = visible ? fg : bg;
    for(int co=0;co<8;co++){
        px(gx+co, gy+11, color);
        px(gx+co, gy+12, color);
    }
}

static void serial_init(void) {
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x80);
    outb(0x3F8+0,0x03); outb(0x3F8+1,0x00);
    outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7);
    outb(0x3F8+4,0x0B);
}
static void serial_putc(char c) {
    while(!(inb(0x3F8+5)&0x20)); outb(0x3F8,c);
}
static void serial_print(const char *s) { for(int i=0;s[i];i++) serial_putc(s[i]); }
static void serial_hex(uint64_t n) {
    char h[]="0123456789ABCDEF"; serial_print("0x");
    for(int i=15;i>=0;i--) serial_putc(h[(n>>(i*4))&0xF]);
}

void exception_handler(void) {
    clear(); fg=0xFF0000;
    println("\n=== SYSTEM EXCEPTION ===");
    println("System halted - fatal error");
    asm volatile("cli; 1: hlt; jmp 1b");
}
__attribute__((used,naked)) static void isr_common(void) {
    asm volatile(
        "pushq %%rax; pushq %%rbx; pushq %%rcx; pushq %%rdx;"
        "pushq %%rsi; pushq %%rdi; pushq %%rbp;"
        "pushq %%r8;  pushq %%r9;  pushq %%r10; pushq %%r11;"
        "pushq %%r12; pushq %%r13; pushq %%r14; pushq %%r15;"
        "cld; call exception_handler;"
        "popq %%r15; popq %%r14; popq %%r13; popq %%r12;"
        "popq %%r11; popq %%r10; popq %%r9;  popq %%r8;"
        "popq %%rbp; popq %%rdi; popq %%rsi;"
        "popq %%rdx; popq %%rcx; popq %%rbx; popq %%rax;"
        "addq $16, %%rsp; iretq" ::: "memory"
    );
}
__attribute__((used,naked)) static void isr0(void)  { asm volatile("pushq $0; pushq $0;  jmp isr_common"); }
__attribute__((used,naked)) static void isr6(void)  { asm volatile("pushq $0; pushq $6;  jmp isr_common"); }
__attribute__((used,naked)) static void isr8(void)  { asm volatile("pushq $0; pushq $8;  jmp isr_common"); }
__attribute__((used,naked)) static void isr13(void) { asm volatile("pushq $0; pushq $13; jmp isr_common"); }
__attribute__((used,naked)) static void isr14(void) { asm volatile("pushq $0; pushq $14; jmp isr_common"); }
static void idt_init(void) {
    for(int i=0;i<256;i++) idt_set_gate(i,(uint64_t)isr_common,0x08,0x8E);
    idt_set_gate(0,(uint64_t)isr0,0x08,0x8E);   idt_set_gate(6,(uint64_t)isr6,0x08,0x8E);
    idt_set_gate(8,(uint64_t)isr8,0x08,0x8E);   idt_set_gate(13,(uint64_t)isr13,0x08,0x8E);
    idt_set_gate(14,(uint64_t)isr14,0x08,0x8E);
    idt_ptr.limit=sizeof(idt_entry_t)*256-1; idt_ptr.base=(uint64_t)&idt;
    asm volatile("lidt %0"::"m"(idt_ptr));
}

static int strcmp_local(const char *a, const char *b) {
    while(*a&&*a==*b){a++;b++;} return *a-*b;
}
static int startswith(const char *s, const char *p) {
    while(*p) if(*s++!=*p++) return 0; return 1;
}
static int kstrlen(const char *s) { int i=0; while(s[i]) i++; return i; }
static void *kmemcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d=dst; const uint8_t *s=src; while(n--) *d++=*s++; return dst;
}
static char *kstrcpy(char *dst, const char *src) {
    char *d=dst; while((*d++=*src++)); return dst;
}

#define MAX_RAMFILES 64
typedef struct { char name[64]; uint8_t *data; uint32_t size; } ramfile_t;
static ramfile_t ramfiles[MAX_RAMFILES];
static int ramfile_count = 0;

static void ramdisk_init(void) {
    for(int i=0;i<MAX_RAMFILES;i++){ramfiles[i].name[0]=0;ramfiles[i].data=NULL;ramfiles[i].size=0;}
    ramfile_count=0;
}
static int ramdisk_find(const char *name) {
    for(int i=0;i<MAX_RAMFILES;i++)
        if(ramfiles[i].name[0]&&strcmp_local(ramfiles[i].name,name)==0) return i;
    return -1;
}
static int ramdisk_create(const char *name, const uint8_t *data, uint32_t size) {
    int idx=ramdisk_find(name);
    if(idx>=0){kfree(ramfiles[idx].data);}
    else {
        for(int i=0;i<MAX_RAMFILES;i++) if(!ramfiles[i].name[0]){idx=i;break;}
        if(idx<0) return -1;
        kstrcpy(ramfiles[idx].name,name);
        ramfile_count++;
    }
    ramfiles[idx].data=kmalloc(size+1);
    if(!ramfiles[idx].data){ramfiles[idx].name[0]=0;return -1;}
    kmemcpy(ramfiles[idx].data,data,size);
    ramfiles[idx].data[size]=0;
    ramfiles[idx].size=size;
    return 0;
}
static int ramdisk_delete(const char *name) {
    int idx=ramdisk_find(name);
    if(idx<0) return -1;
    kfree(ramfiles[idx].data);
    ramfiles[idx].name[0]=0; ramfiles[idx].data=NULL; ramfiles[idx].size=0;
    ramfile_count--;
    return 0;
}

static const char *calc_ptr;

static uint64_t calc_num(void) {
    while(*calc_ptr==' ') calc_ptr++;
    uint64_t val=0;
    if(calc_ptr[0]=='0'&&(calc_ptr[1]=='x'||calc_ptr[1]=='X')){
        calc_ptr+=2;
        while((*calc_ptr>='0'&&*calc_ptr<='9')||(*calc_ptr>='a'&&*calc_ptr<='f')||(*calc_ptr>='A'&&*calc_ptr<='F')){
            char c=*calc_ptr++;
            if(c>='0'&&c<='9') val=val*16+(c-'0');
            else if(c>='a'&&c<='f') val=val*16+(c-'a'+10);
            else val=val*16+(c-'A'+10);
        }
    } else if(*calc_ptr>='0'&&*calc_ptr<='9'){
        while(*calc_ptr>='0'&&*calc_ptr<='9') val=val*10+(*calc_ptr++)-'0';
    }
    return val;
}

static uint64_t calc_expr(void);

static uint64_t calc_primary(void) {
    while(*calc_ptr==' ') calc_ptr++;
    if(*calc_ptr=='('){
        calc_ptr++;
        uint64_t v=calc_expr();
        if(*calc_ptr==')') calc_ptr++;
        return v;
    }
    if(*calc_ptr=='~'){ calc_ptr++; return ~calc_primary(); }
    if(*calc_ptr=='-'){ calc_ptr++; return (uint64_t)(-(int64_t)calc_primary()); }
    return calc_num();
}

static uint64_t calc_mul(void) {
    uint64_t v=calc_primary();
    while(1){
        while(*calc_ptr==' ') calc_ptr++;
        if(*calc_ptr=='*'){calc_ptr++;v*=calc_primary();}
        else if(*calc_ptr=='/'){ calc_ptr++; uint64_t d=calc_primary(); v=d?v/d:0; }
        else if(*calc_ptr=='%'){ calc_ptr++; uint64_t d=calc_primary(); v=d?v%d:0; }
        else break;
    }
    return v;
}
static uint64_t calc_add(void) {
    uint64_t v=calc_mul();
    while(1){
        while(*calc_ptr==' ') calc_ptr++;
        if(*calc_ptr=='+'){calc_ptr++;v+=calc_mul();}
        else if(*calc_ptr=='-'){calc_ptr++;v-=calc_mul();}
        else break;
    }
    return v;
}
static uint64_t calc_shift(void) {
    uint64_t v=calc_add();
    while(1){
        while(*calc_ptr==' ') calc_ptr++;
        if(calc_ptr[0]=='<'&&calc_ptr[1]=='<'){calc_ptr+=2;v<<=calc_add();}
        else if(calc_ptr[0]=='>'&&calc_ptr[1]=='>'){calc_ptr+=2;v>>=calc_add();}
        else break;
    }
    return v;
}
static uint64_t calc_band(void) {
    uint64_t v=calc_shift();
    while(*calc_ptr=='&'&&calc_ptr[1]!='&'){calc_ptr++;v&=calc_shift();}
    return v;
}
static uint64_t calc_bxor(void) {
    uint64_t v=calc_band();
    while(*calc_ptr=='^'){calc_ptr++;v^=calc_band();}
    return v;
}
static uint64_t calc_bor(void) {
    uint64_t v=calc_bxor();
    while(*calc_ptr=='|'&&calc_ptr[1]!='|'){calc_ptr++;v|=calc_bxor();}
    return v;
}
static uint64_t calc_expr(void) { return calc_bor(); }

static void cmd_help(void) {
    fg=0x00FFFF; println("\n  === Commands ==="); fg=0xFFFFFF;
    println("  help / clear / uname / echo <txt> / sleep <ms> / date / ticks");
    println("  crash / fastfetch / reboot / memtest / anim");
    println("  ramls / ramcat <f> / ramwrite <f> <txt> / ramdel <f> / raminfo");
    println("  meminfo / hexdump <addr> <len> / peek <addr> / poke <addr> <val>");
    println("  calc <expr> / ascii / dmesg / vfsls / vfsread <dev> / vfswrite <dev> <txt>");
    println("");
}

static void cmd_date(void) {
    rtc_time_t t=rtc_read();
    fg=0x00FFFF; print("\n  ");
    if(t.day<10)put('0');    print_int(t.day);    put('/');
    if(t.month<10)put('0');  print_int(t.month);  put('/');
    print_int(t.year); print("  ");
    if(t.hour<10)put('0');   print_int(t.hour);   put(':');
    if(t.minute<10)put('0'); print_int(t.minute); put(':');
    if(t.second<10)put('0'); print_int(t.second);
    println(""); println("");
}

static void cmd_memtest(void) {
    fg=0xDDDDDD; println("\n  === PMM Test ===");
    fg=0x88CC88; print("  Free pages: "); print_int((uint32_t)pmm_get_free_page_count()); println("");
    fg=0xDDDDDD; print("  Allocating 1 page... ");
    void *phys=pmm_alloc_page();
    if(phys){
        fg=0x00FF00; println("OK");
        fg=0xDDDDDD; print("  Phys: "); print_hex((uint64_t)phys); println("");
        volatile uint32_t *virt=(uint32_t*)pmm_phys_to_virt((uint64_t)phys);
        fg=0xDDDDDD; print("  Writing 0xDEADBEEF... ");
        *virt=0xDEADBEEF;
        if(*virt==0xDEADBEEF){fg=0x00FF00;println("OK");}
        else{fg=0xFF0000;println("FAILED");}
        pmm_free_page(phys);
        fg=0x00FF00; println("  Page freed.");
    } else { fg=0xFF0000; println("FAILED - OOM"); }
    println("");
    fg=0xDDDDDD; println("  === Heap Test ===");
    fg=0x88CC88; print("  kmalloc(64)... ");
    void *p=kmalloc(64);
    if(p){
        fg=0x00FF00; println("OK");
        fg=0xDDDDDD; print("  Virt: "); print_hex((uint64_t)p); println("");
        volatile uint64_t *q=(uint64_t*)p;
        *q=0x123456789ABCDEF0ULL;
        fg=0xDDDDDD; print("  Verifying... ");
        if(*q==0x123456789ABCDEF0ULL){fg=0x00FF00;println("OK");}
        else{fg=0xFF0000;println("FAILED");}
        kfree(p); fg=0x00FF00; println("  Freed.");
    } else { fg=0xFF0000; println("FAILED"); }
    println("");
    fg=0xDDDDDD; println("  === kcalloc Test ===");
    uint32_t *arr=(uint32_t*)kcalloc(16,sizeof(uint32_t));
    if(arr){
        fg=0xDDDDDD; print("  Zero check... ");
        int ok=1; for(int i=0;i<16;i++) if(arr[i]!=0){ok=0;break;}
        fg=ok?0x00FF00:0xFF0000; println(ok?"OK":"FAILED");
        kfree(arr);
    }
    println("");
}

static void cmd_meminfo(void) {
    fg=0x00FFFF; println("\n  === Memory Info ===");

    fg=0xDDDDDD; print("  PMM free pages:  "); fg=0x88CC88;
    print_int((uint32_t)pmm_get_free_page_count()); println("");
    fg=0xDDDDDD; print("  PMM free mem:    "); fg=0x88CC88;
    print_int((uint32_t)(pmm_get_free_page_count()*4)); println(" KB");

    heap_stats_t hs=heap_get_stats();
    fg=0xDDDDDD; println("\n  Heap:");
    fg=0xDDDDDD; print("    Pages:       "); fg=0x88CC88; print_int(hs.pages_allocated); println("");
    fg=0xDDDDDD; print("    Total bytes: "); fg=0x88CC88; print_int(hs.total_bytes); println("");
    fg=0xDDDDDD; print("    Used bytes:  "); fg=0x88CC88; print_int(hs.used_bytes); println("");
    fg=0xDDDDDD; print("    Free bytes:  "); fg=0x88CC88; print_int(hs.free_bytes); println("");
    fg=0xDDDDDD; print("    Blocks used: "); fg=0x88CC88; print_int(hs.used_blocks); println("");
    fg=0xDDDDDD; print("    Blocks free: "); fg=0x88CC88; print_int(hs.free_blocks); println("");

    if(g_memmap){
        fg=0xDDDDDD; println("\n  Memory map:");
        static const char *types[]={"Usable","Reserved","ACPI Recl","ACPI NVS","Bad","Bootldr Recl","Bootldr","Kernel","Framebuf"};
        for(uint64_t i=0;i<g_memmap->entry_count;i++){
            struct limine_memmap_entry *e=g_memmap->entries[i];
            uint8_t t=(uint8_t)(e->type>8?8:e->type);
            fg=(e->type==0)?0x88CC88:0xAAAAAA;
            print("    "); print_hex(e->base);
            print(" + "); print_hex(e->length);
            print("  "); println(types[t]);
        }
    }
    println("");
}

static void cmd_hexdump(const char *addr_str, const char *len_str) {
    calc_ptr=addr_str; uint64_t addr=calc_expr();
    calc_ptr=len_str;  uint64_t len =calc_expr();
    if(len==0||len>4096) len=256;

    if(addr < 0x100000000ULL) addr += hhdm_offset;

    fg=0x00FFFF; print("\n  hexdump "); print_hex(addr);
    print(" ("); print_int((uint32_t)len); println(" bytes)");

    uint8_t *p=(uint8_t*)(uintptr_t)addr;
    for(uint64_t row=0;row<len;row+=16){
        fg=0x88AACC; print("  "); print_hex(addr+row); print("  ");
        fg=0xDDDDDD;
        for(int col=0;col<16;col++){
            if(row+col<len){
                uint8_t b=p[row+col];
                char h[]="0123456789ABCDEF";
                put(h[b>>4]); put(h[b&0xF]); put(' ');
            } else { print("   "); }
            if(col==7) put(' ');
        }
        print(" |"); fg=0x88CC88;
        for(int col=0;col<16&&row+col<len;col++){
            char c=(char)p[row+col];
            put((c>=32&&c<127)?c:'.');
        }
        fg=0xDDDDDD; println("|");
    }
    println("");
}

static void cmd_peek(const char *addr_str) {
    calc_ptr=addr_str;
    uint64_t addr=calc_expr();

    if(addr < 0x100000000ULL) addr += hhdm_offset;

    volatile uint64_t *p=(volatile uint64_t*)(uintptr_t)addr;
    fg=0x00FFFF; print("\n  peek "); print_hex(addr); print("  = ");
    fg=0x88CC88; print_hex(*p); println(""); println("");
}

static void cmd_poke(const char *addr_str, const char *val_str) {
    calc_ptr=addr_str; uint64_t addr=calc_expr();
    calc_ptr=val_str;  uint64_t val =calc_expr();

    if(addr < 0x100000000ULL) addr += hhdm_offset;

    volatile uint32_t *p=(volatile uint32_t*)(uintptr_t)addr;
    *p=(uint32_t)val;
    fg=0xFFAA00; print("\n  poke "); print_hex(addr);
    print(" <- "); print_hex(val); println(""); println("");
}

static void cmd_calc(const char *expr) {
    calc_ptr=expr;
    uint64_t result=calc_expr();
    fg=0x00FFFF; print("\n  = "); fg=0x88CC88;
    print_hex(result); print("  (dec: "); print_int((uint32_t)result); println(")");
    println("");
}

static void cmd_ascii(void) {
    fg=0x00FFFF; println("\n  === ASCII Table ===");
    for(int row=0;row<8;row++){
        print("  ");
        for(int col=0;col<16;col++){
            int c=row*16+col;
            if(c<32||c==127){fg=0x555555;print("  . ");}
            else{
                fg=0xDDDDDD; put(' ');
                fg=0x88CC88; put((char)c);
                fg=0x555555; put(' ');
                fg=0x88AACC;
                char h[]="0123456789ABCDEF";
                put(h[c>>4]); put(h[c&0xF]);
            }
            put(' ');
        }
        println("");
    }
    println("");
}

static void cmd_dmesg(void) {
    fg=0x00FFFF; println("\n  === Kernel Log ===");
    fg=0x88CC88;
    dmesg_foreach(terminal_putchar);
    fg=0xAAAAAA;
    print("  ["); print_int(dmesg_len()); println(" bytes in buffer]");
    println("");
}

static void vfsls_cb(const char *name, uint32_t flags, uint32_t size) {
    fg=(flags&VFS_BLOCKDEV)?0xFFAA00:0x88CC88;
    print("  /dev/"); print(name);
    if(size) { fg=0xAAAAAA; print("  ("); print_int(size); print(" bytes)"); }
    println("");
}

static void cmd_vfsls_real(void) {
    fg=0x00FFFF; println("\n  === /dev ===");
    vfs_list(vfsls_cb);
    println("");
}

static void cmd_vfsread(const char *dev) {
    vfs_node_t *node=vfs_find(dev);
    if(!node){ fg=0xFF4444; print("  Not found: "); println(dev); return; }
    uint8_t buf[256]; uint32_t n=vfs_read(node,0,255,buf);
    buf[n]=0;
    fg=0xDDDDDD; print("\n  read "); print_int(n); print(" bytes: ");
    for(uint32_t i=0;i<n;i++){
        char c=(char)buf[i];
        put((c>=32&&c<127)?c:'.');
    }
    println(""); println("");
}

static void cmd_vfswrite(const char *dev, const char *data) {
    vfs_node_t *node=vfs_find(dev);
    if(!node){ fg=0xFF4444; print("  Not found: "); println(dev); return; }
    uint32_t len=(uint32_t)kstrlen(data);
    uint32_t n=vfs_write(node,0,len,(const uint8_t*)data);
    fg=0x88CC88; print("\n  wrote "); print_int(n); println(" bytes"); println("");
}

static void cmd_anim(void) {
    fg=0xFFFF00; print("\n  ");
    const char spinner[]="|/-\\";
    for(int i=0;i<20;i++){put(spinner[i%4]);sleep_ms(100);put('\b');}
    fg=0x88FF88; println(" Done!");
}

static void cmd_fastfetch(void) {
    clear(); fg=0x88AACC;
    println("   ______                        _____   _____ ");
    println("  |  ____|                 /\\   |  __ \\ / ____|");
    println("  | |__ _ __ ___  ___     /  \\  | |__) | (___  ");
    fg=0xFFFFFF;
    println("  |  __| '__/ _ \\/ _ \\   / /\\ \\ |  _  / \\___ \\ ");
    println("  | |  | | |  __/  __/  / ____ \\| | \\ \\ ____) |");
    fg=0x88AACC;
    println("  |_|  |_|  \\___|\\___| /_/    \\_\\_|  \\_\\_____/ ");
    println("");
    fg=0x88CC88; println("  user@FreeARS"); fg=0xAAAAAA; println("  -----------");
    fg=0xDDDDDD; print("  OS:       "); fg=0x88CC88; println("FreeARS 0.06");
    fg=0xDDDDDD; print("  Kernel:   "); fg=0x88CC88; println("x86_64 Limine UEFI");
    fg=0xDDDDDD; print("  Shell:    "); fg=0x88CC88; println("freesh 1");
    fg=0xDDDDDD; print("  VFS:      "); fg=0x88CC88; print_int(vfs_node_count()); println(" nodes");
    uint64_t ms=uptime_ms();
    uint32_t s=(uint32_t)(ms/1000),h=s/3600,m=(s%3600)/60; s=s%60;
    fg=0xDDDDDD; print("  Uptime:   "); fg=0x88CC88;
    if(h){print_int(h);print("h ");} if(m){print_int(m);print("m ");} print_int(s); println("s");
    fg=0xDDDDDD; print("  RAM:      "); fg=0x88CC88;
    if(total_ram>=1073741824){print_int(total_ram/1073741824);println(" GB");}
    else{print_int(total_ram/1048576);println(" MB");}
    fg=0xDDDDDD; print("  Display:  "); fg=0x88CC88;
    print_int(fbi->width); print("x"); print_int(fbi->height);
    print(" @ "); print_int(fbi->bpp); println("bpp");
    fg=0xDDDDDD; print("  CPU:      "); fg=0x88CC88;
    uint32_t eax,ebx,ecx,edx; char cpu[49]={0};
    asm volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(0x80000002));
    *(uint32_t*)(cpu+0)=eax;*(uint32_t*)(cpu+4)=ebx;*(uint32_t*)(cpu+8)=ecx;*(uint32_t*)(cpu+12)=edx;
    asm volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(0x80000003));
    *(uint32_t*)(cpu+16)=eax;*(uint32_t*)(cpu+20)=ebx;*(uint32_t*)(cpu+24)=ecx;*(uint32_t*)(cpu+28)=edx;
    asm volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(0x80000004));
    *(uint32_t*)(cpu+32)=eax;*(uint32_t*)(cpu+36)=ebx;*(uint32_t*)(cpu+40)=ecx;*(uint32_t*)(cpu+44)=edx;
    cpu[48]=0; int cp=0; while(cpu[cp]==' ')cp++; println(&cpu[cp]);
    fg=0xDDDDDD; print("  TSC:      "); fg=0x88CC88;
    print_int((uint32_t)(tsc_hz/1000000)); println(" MHz");
    fg=0xDDDDDD; print("  PMM:      "); fg=0x88CC88;
    print_int((uint32_t)pmm_get_free_page_count()); println(" pages free");
    println("");
    uint32_t cols[]={0xCC88AA,0xCCCC88,0x88CC88,0x88AACC,0x8888CC};
    for(int r=0;r<5;r++){print("  ");fg=cols[r];for(int c=0;c<30;c++)print("#");println("");}
    println("");
}

static void cmd_ramls(void) {
    fg=0x00FFFF; println("\n  === Ramdisk ==="); fg=0xFFFFFF;
    if(!ramfile_count){ fg=0xFF4444; println("  (empty)"); }
    else for(int i=0;i<MAX_RAMFILES;i++){
        if(ramfiles[i].name[0]){
            fg=0x88CC88; print("  "); print(ramfiles[i].name);
            fg=0xFFFFFF; print("  ("); print_int(ramfiles[i].size); println(" bytes)");
        }
    }
    println("");
}
static void cmd_ramcat(const char *name) {
    int idx=ramdisk_find(name);
    if(idx<0){fg=0xFF4444;print("  Not found: ");println(name);return;}
    fg=0xDDDDDD; print("  ");
    for(uint32_t i=0;i<ramfiles[idx].size;i++) put((char)ramfiles[idx].data[i]);
    println("");
}
static void cmd_ramwrite(const char *name, const char *content) {
    int r=ramdisk_create(name,(const uint8_t*)content,(uint32_t)kstrlen(content));
    if(r==0){fg=0x88CC88;print("  Written: ");println(name);}
    else{fg=0xFF4444;println("  Error.");}
}
static void cmd_ramdel(const char *name) {
    int r=ramdisk_delete(name);
    if(r==0){fg=0x88CC88;print("  Deleted: ");println(name);}
    else{fg=0xFF4444;println("  Not found.");}
}
static void cmd_raminfo(void) {
    fg=0x00FFFF; println("\n  === Ramdisk Info ==="); fg=0xFFFFFF;
    print("  Files: "); print_int(ramfile_count); println("");
    print("  Free slots: "); print_int(MAX_RAMFILES-ramfile_count); println("");
    println("");
}

static void shell(void) {
    char in[256];
    while(1){
        fg=0x00FF00; print("freeARS> "); fg=0xFFFFFF;
        keyboard_readline(in,256);

        if     (!strcmp_local(in,"help"))      cmd_help();
        else if(!strcmp_local(in,"clear"))     clear();
        else if(!strcmp_local(in,"uname"))   { fg=0x00FF00; println("  FreeARS 0.06 x86_64-uefi Limine"); }
        else if(startswith(in,"echo "))      { fg=0x00FF00; print("  "); println(in+5); }
        else if(!strcmp_local(in,"ticks"))   {
            fg=0x00FF00;
            print("  Ticks: ");  print_int(get_ticks());
            print("  Uptime: "); print_int((uint32_t)(uptime_ms()/1000)); println("s");
        }
        else if(startswith(in,"sleep ")){
            unsigned long long ms=0; int valid=0;
            for(int i=6;in[i]>='0'&&in[i]<='9';i++){ms=ms*10+(in[i]-'0');valid=1;}
            if(!valid||ms==0){fg=0xFF0000;println("  Invalid number");}
            else{
                if(ms>3600000) ms=3600000;
                fg=0x00FF00; print("  Sleeping "); print_int((uint32_t)ms); println("ms...");
                sleep_ms((uint32_t)ms);
                fg=0x00FF00; println("  Done!");
            }
        }
        else if(!strcmp_local(in,"crash"))     { fg=0xFF0000; println("  Crashing..."); volatile int a=10,b=0,c=a/b;(void)c; }
        else if(!strcmp_local(in,"fastfetch")) cmd_fastfetch();
        else if(!strcmp_local(in,"memtest"))   cmd_memtest();
        else if(!strcmp_local(in,"reboot"))    outb(0x64,0xFE);
        else if(!strcmp_local(in,"anim"))      cmd_anim();
        else if(!strcmp_local(in,"date"))      cmd_date();
        else if(!strcmp_local(in,"meminfo"))   cmd_meminfo();
        else if(!strcmp_local(in,"ascii"))     cmd_ascii();
        else if(!strcmp_local(in,"dmesg"))     cmd_dmesg();
        else if(!strcmp_local(in,"vfsls"))     cmd_vfsls_real();
        else if(!strcmp_local(in,"ramls"))     cmd_ramls();
        else if(!strcmp_local(in,"raminfo"))   cmd_raminfo();
        else if(startswith(in,"calc ")){
            if(in[5]) cmd_calc(in+5);
            else{fg=0xFF0000;println("  Usage: calc <expr>");}
        }
        else if(startswith(in,"hexdump ")){
            char *p=in+8; while(*p==' ')p++;
            char *q=p; while(*q&&*q!=' ')q++;
            if(*q){*q=0;cmd_hexdump(p,q+1);}
            else{fg=0xFF0000;println("  Usage: hexdump <addr> <len>");}
        }
        else if(startswith(in,"peek ")){
            if(in[5]) cmd_peek(in+5);
            else{fg=0xFF0000;println("  Usage: peek <addr>");}
        }
        else if(startswith(in,"poke ")){
            char *p=in+5; while(*p==' ')p++;
            char *q=p; while(*q&&*q!=' ')q++;
            if(*q){*q=0;cmd_poke(p,q+1);}
            else{fg=0xFF0000;println("  Usage: poke <addr> <val>");}
        }
        else if(startswith(in,"vfsread ")){
            char *dev=in+8; while(*dev==' ')dev++;
            if(*dev) cmd_vfsread(dev);
            else{fg=0xFF0000;println("  Usage: vfsread <dev>");}
        }
        else if(startswith(in,"vfswrite ")){
            char *p=in+9; while(*p==' ')p++;
            char *q=p; while(*q&&*q!=' ')q++;
            if(*q){*q=0;cmd_vfswrite(p,q+1);}
            else{fg=0xFF0000;println("  Usage: vfswrite <dev> <data>");}
        }
        else if(startswith(in,"ramcat ")){
            char *n=in+7; if(*n) cmd_ramcat(n);
            else{fg=0xFF0000;println("  Usage: ramcat <file>");}
        }
        else if(startswith(in,"ramwrite ")){
            char *p=in+9; char *q=p;
            while(*q&&*q!=' ')q++;
            if(*q){*q=0;cmd_ramwrite(p,q+1);}
            else{fg=0xFF0000;println("  Usage: ramwrite <file> <content>");}
        }
        else if(startswith(in,"ramdel ")){
            char *n=in+7; if(*n) cmd_ramdel(n);
            else{fg=0xFF0000;println("  Usage: ramdel <file>");}
        }
        else if(in[0]){ fg=0xFF0000; print("  not found: "); println(in); }
    }
}

static void hcf(void){ for(;;) asm("hlt"); }

void kmain(void) {
    if(LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)==false) hcf();
    if(framebuffer_request.response==NULL||
       framebuffer_request.response->framebuffer_count<1) hcf();

    fbi=framebuffer_request.response->framebuffers[0];
    pw=fbi->pitch/4;

    serial_init();
    dmesg_init();
    dmesg("[boot] FreeARS 0.06 starting\n");

    if(memmap_request.response!=NULL&&hhdm_request.response!=NULL){
        hhdm_offset=hhdm_request.response->offset;
        g_memmap=memmap_request.response;
        for(uint64_t i=0;i<memmap_request.response->entry_count;i++){
            struct limine_memmap_entry *e=memmap_request.response->entries[i];
            if(e->type==LIMINE_MEMMAP_USABLE) total_ram+=e->length;
        }
        serial_print("HHDM: "); serial_hex(hhdm_offset); serial_print("\n");
        dmesg("[pmm] initializing\n");
        pmm_init(memmap_request.response,hhdm_offset);
        dmesg("[pmm] OK, free pages: "); dmesg_int((uint32_t)pmm_get_free_page_count()); dmesg("\n");
    } else { hcf(); }

    tsc_calibrate();
    boot_tsc=rdtsc();
    dmesg("[tsc] "); dmesg_int((uint32_t)(tsc_hz/1000000)); dmesg(" MHz\n");

    idt_init();
    dmesg("[idt] loaded\n");

    ramdisk_init();
    vfs_init();
    dmesg("[vfs] ready\n");

    asm volatile("sti");
    keyboard_set_cursor_cb(cursor_draw);

    clear();
    fg=0x88AACC;
    println("   ______                        _____   _____ ");
    println("  |  ____|                 /\\   |  __ \\ / ____|");
    println("  | |__ _ __ ___  ___     /  \\  | |__) | (___  ");
    fg=0xFFFFFF;
    println("  |  __| '__/ _ \\/ _ \\   / /\\ \\ |  _  / \\___ \\ ");
    println("  | |  | | |  __/  __/  / ____ \\| | \\ \\ ____) |");
    fg=0x88AACC;
    println("  |_|  |_|  \\___|\\___| /_/    \\_\\_|  \\_\\_____/ ");
    println("");
    fg=0x88CC88; println("  FreeARS 0.06"); println("");
    fg=0xDDDDDD; print("  Framebuffer: "); fg=0x88CC88;
    print_int(fbi->width); print("x"); print_int(fbi->height); println("");
    fg=0xDDDDDD; print("  RAM:         "); fg=0x88CC88;
    if(total_ram>=1073741824){print_int(total_ram/1073741824);println(" GB");}
    else{print_int(total_ram/1048576);println(" MB");}
    fg=0xDDDDDD; print("  TSC:         "); fg=0x88CC88;
    print_int((uint32_t)(tsc_hz/1000000)); println(" MHz");
    fg=0xDDDDDD; print("  PMM:         "); fg=0x88CC88;
    print_int((uint32_t)pmm_get_free_page_count()); println(" pages free");
    fg=0xDDDDDD; print("  VFS:         "); fg=0x88CC88;
    print_int(vfs_node_count()); println(" nodes");
    println("");
    fg=0xAAAAAA; println("  Type 'help' for available commands."); println("");

    shell();
    hcf();
}