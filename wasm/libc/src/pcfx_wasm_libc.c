#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <wchar.h>

int errno;

extern unsigned char __heap_base;
static uintptr_t heap_top;

__attribute__((import_module("pcfx"), import_name("read_host_file")))
extern uint32_t pcfx_js_read_host_file(uint32_t handle, uint32_t offset, uint32_t size, uint32_t dest);

static uintptr_t align16(uintptr_t v) { return (v + 15u) & ~(uintptr_t)15u; }
static void heap_init(void) { if(!heap_top) heap_top = align16((uintptr_t)&__heap_base); }
void __pcfx_wasm_heap_reset(void) { heap_top = align16((uintptr_t)&__heap_base); }
uint32_t __pcfx_wasm_heap_used(void) { heap_init(); return (uint32_t)(heap_top - align16((uintptr_t)&__heap_base)); }

#define PCFX_WASM_ALLOC_MAGIC 0x50434658u /* "PCFX" */
typedef struct PCFXWasmAllocHeader
{
    uint32_t magic;
    uint32_t size;
    uint32_t reserved0;
    uint32_t reserved1;
} PCFXWasmAllocHeader;

void *malloc(size_t size)
{
    heap_init();
    const size_t user_size = size ? size : 1u;
    if(user_size > ((size_t)-1) - sizeof(PCFXWasmAllocHeader) - 15u)
    { errno = ENOMEM; return NULL; }

    const size_t total = align16(sizeof(PCFXWasmAllocHeader) + user_size);
    uintptr_t old = heap_top;
    uintptr_t need = old + total;
    if(need < old) { errno = ENOMEM; return NULL; }

    size_t pages = __builtin_wasm_memory_size(0);
    uintptr_t have = (uintptr_t)pages << 16;
    if(need > have)
    {
        uintptr_t delta = need - have;
        uintptr_t grow = (delta + 0xffffu) >> 16;
        if(__builtin_wasm_memory_grow(0, grow) == (size_t)-1)
        { errno = ENOMEM; return NULL; }
    }

    PCFXWasmAllocHeader *hdr = (PCFXWasmAllocHeader*)old;
    hdr->magic = PCFX_WASM_ALLOC_MAGIC;
    hdr->size = (uint32_t)user_size;
    hdr->reserved0 = 0;
    hdr->reserved1 = 0;
    heap_top = need;
    return (void*)(old + sizeof(PCFXWasmAllocHeader));
}

void free(void *ptr) { (void)ptr; }
void *calloc(size_t nmemb, size_t size)
{
    if(size && nmemb > ((size_t)-1) / size) { errno = ENOMEM; return NULL; }
    size_t n = nmemb * size;
    void *p = malloc(n);
    if(p) memset(p, 0, n);
    return p;
}
void *realloc(void *ptr, size_t size)
{
    if(!ptr) return malloc(size);
    void *n = malloc(size);
    if(!n) return NULL;

    size_t copy = size ? size : 1u;
    PCFXWasmAllocHeader *old = ((PCFXWasmAllocHeader*)ptr) - 1;
    if(old->magic == PCFX_WASM_ALLOC_MAGIC && old->size < copy)
        copy = old->size;
    if(copy) memcpy(n, ptr, copy);
    return n;
}
void *alloca(size_t size) { return __builtin_alloca(size); }
void abort(void) { for(;;){} }
void exit(int status) { (void)status; for(;;){} }

void *memcpy(void *dest, const void *src, size_t n)
{ unsigned char*d=dest; const unsigned char*s=src; for(size_t i=0;i<n;i++) d[i]=s[i]; return dest; }
void *memmove(void *dest, const void *src, size_t n)
{ unsigned char*d=dest; const unsigned char*s=src; if(d<s){ for(size_t i=0;i<n;i++) d[i]=s[i]; } else if(d>s){ for(size_t i=n;i;i--) d[i-1]=s[i-1]; } return dest; }
void *memset(void *s, int c, size_t n)
{ unsigned char*p=s; for(size_t i=0;i<n;i++) p[i]=(unsigned char)c; return s; }
int memcmp(const void *s1, const void *s2, size_t n)
{ const unsigned char*a=s1,*b=s2; for(size_t i=0;i<n;i++) if(a[i]!=b[i]) return (int)a[i]-(int)b[i]; return 0; }
void *memchr(const void *s, int c, size_t n)
{ const unsigned char*p=s; for(size_t i=0;i<n;i++) if(p[i]==(unsigned char)c) return (void*)(p+i); return NULL; }
size_t strlen(const char *s) { size_t n=0; while(s&&s[n]) n++; return n; }
size_t strnlen(const char *s, size_t maxlen) { size_t n=0; while(s&&n<maxlen&&s[n]) n++; return n; }
char *strcpy(char *d, const char *s)
{
    char *r = d;
    for(;;)
    {
        char c = *s++;
        *d++ = c;
        if(!c)
            break;
    }
    return r;
}
char *strncpy(char *d, const char *s, size_t n) { size_t i=0; for(;i<n&&s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]=0; return d; }
char *strcat(char *d, const char *s) { strcpy(d+strlen(d),s); return d; }
char *strncat(char *d, const char *s, size_t n) { char*p=d+strlen(d); size_t i=0; for(;i<n&&s[i];i++) p[i]=s[i]; p[i]=0; return d; }
int strcmp(const char *a,const char*b){ while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char *a,const char*b,size_t n){ for(size_t i=0;i<n;i++){ if(a[i]!=b[i]||!a[i]||!b[i]) return (unsigned char)a[i]-(unsigned char)b[i]; } return 0; }
int strcoll(const char *a,const char*b){ return strcmp(a,b); }
char *strchr(const char*s,int c){ while(*s){ if(*s==(char)c) return (char*)s; s++; } return c?NULL:(char*)s; }
char *strrchr(const char*s,int c){ const char*r=NULL; do{ if(*s==(char)c) r=s; }while(*s++); return (char*)r; }
char *strstr(const char*h,const char*n){ if(!*n) return (char*)h; for(;*h;h++) if(*h==*n&&!strncmp(h,n,strlen(n))) return (char*)h; return NULL; }
size_t strspn(const char*s,const char*accept){ size_t n=0; while(s[n] && strchr(accept,s[n])) n++; return n; }
size_t strcspn(const char*s,const char*reject){ size_t n=0; while(s[n] && !strchr(reject,s[n])) n++; return n; }
char *strpbrk(const char*s,const char*accept){ while(*s){ if(strchr(accept,*s)) return (char*)s; s++; } return NULL; }
static char *strtok_save;
char *strtok(char *str,const char*delim){ if(str) strtok_save=str; if(!strtok_save) return NULL; char*s=strtok_save+strspn(strtok_save,delim); if(!*s){strtok_save=NULL;return NULL;} char*e=s+strcspn(s,delim); if(*e){*e=0;strtok_save=e+1;} else strtok_save=NULL; return s; }
char *strdup(const char*s){ size_t n=strlen(s)+1; char*d=malloc(n); if(d) memcpy(d,s,n); return d; }
char *strerror(int e){ (void)e; return "wasm errno"; }
static int lower(int c){ return (c>='A'&&c<='Z')?c+32:c; }
int strcasecmp(const char*a,const char*b){ while(*a&&lower(*a)==lower(*b)){a++;b++;} return lower((unsigned char)*a)-lower((unsigned char)*b); }
int strncasecmp(const char*a,const char*b,size_t n){ for(size_t i=0;i<n;i++){ int ca=lower((unsigned char)a[i]),cb=lower((unsigned char)b[i]); if(ca!=cb||!ca||!cb) return ca-cb; } return 0; }
void bzero(void*s,size_t n){ memset(s,0,n); }

int isdigit(int c){return c>='0'&&c<='9';} int isalpha(int c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z');} int isalnum(int c){return isalpha(c)||isdigit(c);} int isblank(int c){return c==' '||c=='\t';} int iscntrl(int c){return (c>=0&&c<32)||c==127;} int isgraph(int c){return c>32&&c<127;} int islower(int c){return c>='a'&&c<='z';} int isprint(int c){return c>=32&&c<127;} int ispunct(int c){return isgraph(c)&&!isalnum(c);} int isspace(int c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';} int isupper(int c){return c>='A'&&c<='Z';} int isxdigit(int c){return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F');} int tolower(int c){return lower(c);} int toupper(int c){return (c>='a'&&c<='z')?c-32:c;}

static unsigned long long parse_unsigned(const char*s,char**end,int base,int neg)
{ while(isspace((unsigned char)*s)) s++; if(*s=='+'||*s=='-'){ if(*s=='-') neg=!neg; s++; } if(base==0){ if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;} else if(*s=='0') base=8; else base=10; } unsigned long long v=0; while(*s){ int d; if(isdigit(*s)) d=*s-'0'; else if(*s>='a'&&*s<='z') d=*s-'a'+10; else if(*s>='A'&&*s<='Z') d=*s-'A'+10; else break; if(d>=base) break; v=v*(unsigned)base+(unsigned)d; s++; } if(end) *end=(char*)s; return neg?(unsigned long long)(-(long long)v):v; }
int atoi(const char*s){return (int)strtol(s,NULL,10);} long atol(const char*s){return strtol(s,NULL,10);} long strtol(const char*s,char**e,int b){return (long)parse_unsigned(s,e,b,0);} unsigned long strtoul(const char*s,char**e,int b){return (unsigned long)parse_unsigned(s,e,b,0);} long long strtoll(const char*s,char**e,int b){return (long long)parse_unsigned(s,e,b,0);} unsigned long long strtoull(const char*s,char**e,int b){return parse_unsigned(s,e,b,0);} double strtod(const char*s,char**e){ long v=strtol(s,e,10); return (double)v; }
int abs(int j){return j<0?-j:j;} long labs(long j){return j<0?-j:j;} static unsigned rnd=1; void srand(unsigned s){rnd=s? s:1;} int rand(void){rnd=rnd*1103515245u+12345u; return (int)((rnd>>16)&0x7fff);} void qsort(void*b,size_t n,size_t sz,int(*cmp)(const void*,const void*)){ unsigned char*a=b; for(size_t i=0;i<n;i++) for(size_t j=i+1;j<n;j++) if(cmp(a+i*sz,a+j*sz)>0){ for(size_t k=0;k<sz;k++){unsigned char t=a[i*sz+k];a[i*sz+k]=a[j*sz+k];a[j*sz+k]=t;}} }

static void append_ch(char **out, size_t *left, int *count, char c){ if(*left>1){ **out=c; (*out)++; (*left)--; } (*count)++; }
static void append_str(char **out,size_t*left,int*count,const char*s,int width,char pad){ int len=(int)strlen(s); for(int i=len;i<width;i++) append_ch(out,left,count,pad); for(int i=0;i<len;i++) append_ch(out,left,count,s[i]); }
static void append_num(char **out,size_t*left,int*count,unsigned long long v,int neg,int base,int upper,int width,char pad){ char tmp[64]; int n=0; const char*dig=upper?"0123456789ABCDEF":"0123456789abcdef"; do{tmp[n++]=dig[v%base]; v/=base;}while(v); if(neg) tmp[n++]='-'; for(int i=n;i<width;i++) append_ch(out,left,count,pad); while(n--) append_ch(out,left,count,tmp[n]); }
int vsnprintf(char *str,size_t size,const char*fmt,va_list ap)
{ char dummy; char*out=str?str:&dummy; size_t left=str?size:0; int count=0; if(left) *out=0; for(;*fmt;fmt++){ if(*fmt!='%'){append_ch(&out,&left,&count,*fmt);continue;} fmt++; char pad=' '; if(*fmt=='0'){pad='0';fmt++;} int width=0; while(isdigit((unsigned char)*fmt)){width=width*10+(*fmt++-'0');} int longness=0; while(*fmt=='l'||*fmt=='z'){longness++;fmt++;} switch(*fmt){ case '%': append_ch(&out,&left,&count,'%'); break; case 'c': append_ch(&out,&left,&count,(char)va_arg(ap,int)); break; case 's': { const char*s=va_arg(ap,const char*); append_str(&out,&left,&count,s?s:"(null)",width,pad); break;} case 'd': case 'i': { long long v = longness?va_arg(ap,long long):va_arg(ap,int); append_num(&out,&left,&count,(v<0)?-v:v,v<0,10,0,width,pad); break;} case 'u': { unsigned long long v = longness?va_arg(ap,unsigned long long):va_arg(ap,unsigned int); append_num(&out,&left,&count,v,0,10,0,width,pad); break;} case 'x': case 'X': { unsigned long long v = longness?va_arg(ap,unsigned long long):va_arg(ap,unsigned int); append_num(&out,&left,&count,v,0,16,*fmt=='X',width,pad); break;} case 'p': { uintptr_t v=(uintptr_t)va_arg(ap,void*); append_str(&out,&left,&count,"0x",0,' '); append_num(&out,&left,&count,v,0,16,0,width?width:1,'0'); break;} case 'f': { double d=va_arg(ap,double); long long iv=(long long)d; append_num(&out,&left,&count,(iv<0)?-iv:iv,iv<0,10,0,width,pad); break;} default: append_ch(&out,&left,&count,*fmt); break; }} if(str&&size){ *out=0; } return count; }
int snprintf(char*s,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=vsnprintf(s,n,f,ap);va_end(ap);return r;} int sprintf(char*s,const char*f,...){va_list ap;va_start(ap,f);int r=vsnprintf(s,(size_t)-1,f,ap);va_end(ap);return r;} int vsprintf(char*s,const char*f,va_list ap){return vsnprintf(s,(size_t)-1,f,ap);} int vprintf(const char*f,va_list ap){return vsnprintf(NULL,0,f,ap);} int printf(const char*f,...){va_list ap;va_start(ap,f);int r=vprintf(f,ap);va_end(ap);return r;} int vfprintf(FILE*fp,const char*f,va_list ap){(void)fp;return vprintf(f,ap);} int fprintf(FILE*fp,const char*f,...){va_list ap;va_start(ap,f);int r=vfprintf(fp,f,ap);va_end(ap);return r;}
int vsscanf(const char *str,const char *fmt,va_list ap)
{
    int assigned = 0;
    const char *s = str ? str : "";

    for(; fmt && *fmt; fmt++)
    {
        if(isspace((unsigned char)*fmt))
        {
            while(isspace((unsigned char)*s)) s++;
            continue;
        }
        if(*fmt != '%')
        {
            if(*s == *fmt) { s++; continue; }
            break;
        }

        fmt++;
        if(*fmt == '%')
        {
            if(*s != '%') break;
            s++;
            continue;
        }

        int suppress = 0;
        if(*fmt == '*') { suppress = 1; fmt++; }

        int width = 0;
        while(isdigit((unsigned char)*fmt))
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        while(*fmt == 'h' || *fmt == 'l' || *fmt == 'j' || *fmt == 'z' || *fmt == 't' || *fmt == 'L')
            fmt++;

        if(!*fmt) break;

        if(*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X')
        {
            while(isspace((unsigned char)*s)) s++;
            if(!*s) break;

            char tmp[64];
            int n = 0;
            const int lim = width > 0 && width < (int)sizeof(tmp) ? width : (int)sizeof(tmp) - 1;
            const char *p = s;
            if(n < lim && (*p == '+' || *p == '-')) tmp[n++] = *p++;
            if((*fmt == 'i' || *fmt == 'x' || *fmt == 'X') && n + 1 < lim && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            {
                tmp[n++] = *p++;
                tmp[n++] = *p++;
            }
            while(n < lim)
            {
                int c = (unsigned char)*p;
                if(!c) break;
                if(*fmt == 'd' || *fmt == 'u') { if(!isdigit(c)) break; }
                else { if(!isxdigit(c)) break; }
                tmp[n++] = (char)c;
                p++;
            }
            if(n == 0 || (n == 1 && (tmp[0] == '+' || tmp[0] == '-'))) break;
            tmp[n] = 0;
            char *end = NULL;
            if(*fmt == 'd' || *fmt == 'i')
            {
                long v = strtol(tmp, &end, (*fmt == 'i') ? 0 : 10);
                if(end == tmp) break;
                if(!suppress) { int *out = va_arg(ap, int*); *out = (int)v; assigned++; }
            }
            else
            {
                unsigned long v = strtoul(tmp, &end, (*fmt == 'u') ? 10 : 16);
                if(end == tmp) break;
                if(!suppress) { unsigned *out = va_arg(ap, unsigned*); *out = (unsigned)v; assigned++; }
            }
            s = p;
        }
        else if(*fmt == 's')
        {
            while(isspace((unsigned char)*s)) s++;
            if(!*s) break;
            const int lim = width > 0 ? width : 0x7fffffff;
            int n = 0;
            if(!suppress)
            {
                char *out = va_arg(ap, char*);
                while(*s && !isspace((unsigned char)*s) && n < lim)
                {
                    *out++ = *s++;
                    n++;
                }
                *out = 0;
                assigned++;
            }
            else
            {
                while(*s && !isspace((unsigned char)*s) && n < lim) { s++; n++; }
            }
            if(n == 0) break;
        }
        else if(*fmt == 'c')
        {
            const int n = width > 0 ? width : 1;
            if(!*s) break;
            if(!suppress)
            {
                char *out = va_arg(ap, char*);
                for(int i = 0; i < n; i++)
                {
                    if(!*s && i) break;
                    *out++ = *s++;
                }
                assigned++;
            }
            else
            {
                for(int i = 0; i < n && *s; i++) s++;
            }
        }
        else
            break;
    }
    return assigned;
}
int sscanf(const char*s,const char*f,...){va_list ap;va_start(ap,f);int r=vsscanf(s,f,ap);va_end(ap);return r;}

#define MAX_VFS 128
#define MAX_FILES 32
struct VfsEntry { char path[260]; uint8_t *data; size_t size; uint32_t host_handle; };
struct PCFX_WASM_FILE { int used; int writable; int error; int eof; uint8_t *data; size_t size; size_t pos; uint32_t host_handle; char path[260]; };
struct DIR { unsigned idx; char prefix[260]; struct dirent ent; };
static struct VfsEntry vfs[MAX_VFS]; static unsigned vfs_count; static struct PCFX_WASM_FILE files[MAX_FILES]; static FILE std_file[3]; FILE *stdin=&std_file[0]; FILE *stdout=&std_file[1]; FILE *stderr=&std_file[2];
static const char* base_name(const char*p){ const char*s1=strrchr(p,'/'); const char*s2=strrchr(p,'\\'); const char*s=s1>s2?s1:s2; return s?s+1:p; }
static bool path_equal(const char*a,const char*b){ return !strcasecmp(a,b); }
static int vfs_find(const char*path){ if(!path) return -1; for(unsigned i=vfs_count;i>0;i--) if(path_equal(vfs[i-1].path,path)) return (int)(i-1); const char*b=base_name(path); for(unsigned i=vfs_count;i>0;i--) if(path_equal(base_name(vfs[i-1].path),b)) return (int)(i-1); return -1; }
__attribute__((export_name("pcfx_wasm_vfs_clear"))) void pcfx_wasm_vfs_clear(void){ vfs_count=0; memset(files, 0, sizeof(files)); }
__attribute__((export_name("pcfx_wasm_vfs_add_file"))) uint32_t pcfx_wasm_vfs_add_file(uint32_t path_ptr,uint32_t path_len,uint32_t data_ptr,uint32_t size){ if(vfs_count>=MAX_VFS||!path_ptr||!data_ptr) return 0; if(path_len>=sizeof(vfs[0].path)) path_len=sizeof(vfs[0].path)-1; struct VfsEntry*e=&vfs[vfs_count++]; memcpy(e->path,(void*)(uintptr_t)path_ptr,path_len); e->path[path_len]=0; e->data=(uint8_t*)(uintptr_t)data_ptr; e->size=size; e->host_handle=0; return 1; }
__attribute__((export_name("pcfx_wasm_vfs_add_host_file"))) uint32_t pcfx_wasm_vfs_add_host_file(uint32_t path_ptr,uint32_t path_len,uint32_t host_handle,uint32_t size){ if(vfs_count>=MAX_VFS||!path_ptr||!host_handle) return 0; if(path_len>=sizeof(vfs[0].path)) path_len=sizeof(vfs[0].path)-1; struct VfsEntry*e=&vfs[vfs_count++]; memcpy(e->path,(void*)(uintptr_t)path_ptr,path_len); e->path[path_len]=0; e->data=NULL; e->size=size; e->host_handle=host_handle; return 1; }
__attribute__((export_name("pcfx_wasm_vfs_count"))) uint32_t pcfx_wasm_vfs_count(void){ return vfs_count; }
FILE *fopen(const char*path,const char*mode){ int wr=mode&&(strchr(mode,'w')||strchr(mode,'a')); int idx=wr?-1:vfs_find(path); if(!wr&&idx<0){errno=ENOENT;return NULL;} for(unsigned i=0;i<MAX_FILES;i++) if(!files[i].used){ struct PCFX_WASM_FILE*f=&files[i]; memset(f,0,sizeof(*f)); f->used=1; f->writable=wr; snprintf(f->path,sizeof(f->path),"%s",path?path:""); if(wr){ f->data=malloc(1); f->size=0; } else { f->data=vfs[idx].data; f->size=vfs[idx].size; f->host_handle=vfs[idx].host_handle; } return (FILE*)f; } errno=EMFILE; return NULL; }
int fclose(FILE*fp){ if(!fp) return EOF; ((struct PCFX_WASM_FILE*)fp)->used=0; return 0; }
size_t fread(void*ptr,size_t size,size_t nmemb,FILE*fp){ if(!fp||!ptr||!size) return 0; struct PCFX_WASM_FILE*f=(void*)fp; size_t want=size*nmemb, rem=f->pos<f->size?f->size-f->pos:0; if(want>rem){want=rem;f->eof=1;} if(!want) return 0; if(f->host_handle){ uint32_t got=pcfx_js_read_host_file(f->host_handle,(uint32_t)f->pos,(uint32_t)want,(uint32_t)(uintptr_t)ptr); if(got>want) got=(uint32_t)want; f->pos += got; if(got < want) f->eof=1; return got/size; } memcpy(ptr,f->data+f->pos,want); f->pos+=want; return want/size; }
size_t fwrite(const void*ptr,size_t size,size_t nmemb,FILE*fp){ (void)ptr; (void)fp; return nmemb; }
int fseek(FILE*fp,long off,int whence){ if(!fp) return -1; struct PCFX_WASM_FILE*f=(void*)fp; long np = whence==SEEK_SET?off:whence==SEEK_CUR?(long)f->pos+off:(long)f->size+off; if(np<0){f->error=1;return -1;} f->pos=(size_t)np; f->eof=0; return 0; }
long ftell(FILE*fp){ return fp?(long)((struct PCFX_WASM_FILE*)fp)->pos:-1; } int fseeko(FILE*fp,long off,int whence){return fseek(fp,off,whence);} long ftello(FILE*fp){return ftell(fp);} int fflush(FILE*fp){(void)fp;return 0;} int ferror(FILE*fp){return fp?((struct PCFX_WASM_FILE*)fp)->error:1;} void clearerr(FILE*fp){if(fp){((struct PCFX_WASM_FILE*)fp)->error=0;((struct PCFX_WASM_FILE*)fp)->eof=0;}} int feof(FILE*fp){return fp?((struct PCFX_WASM_FILE*)fp)->eof:1;} int fgetc(FILE*fp){ unsigned char c; return fread(&c,1,1,fp)==1?c:EOF;} int getc(FILE*fp){return fgetc(fp);} int ungetc(int c,FILE*fp){ if(!fp||c==EOF) return EOF; struct PCFX_WASM_FILE*f=(void*)fp; if(f->pos) f->pos--; return c;} char*fgets(char*s,int n,FILE*fp){ if(!s||n<=0) return NULL; int i=0,c=0; while(i<n-1 && (c=fgetc(fp))!=EOF){ s[i++]=(char)c; if(c=='\n') break;} s[i]=0; return i?s:NULL;} int fputc(int c,FILE*fp){(void)fp;return c;} int putc(int c,FILE*fp){return fputc(c,fp);} int fputs(const char*s,FILE*fp){(void)fp;return (int)strlen(s);} int puts(const char*s){return (int)strlen(s);} int remove(const char*p){(void)p;return 0;} int rename(const char*a,const char*b){(void)a;(void)b;return 0;}
int stat(const char*path,struct stat*st){ if(!st) return -1; memset(st,0,sizeof(*st)); int idx=vfs_find(path); if(idx>=0){st->st_mode=S_IFREG;st->st_size=(long)vfs[idx].size;return 0;} if(path){ size_t plen=strlen(path); for(unsigned i=0;i<vfs_count;i++) if(!strncmp(vfs[i].path,path,plen)){st->st_mode=S_IFDIR;return 0;} } errno=ENOENT; return -1; } int access(const char*path,int mode){(void)mode; struct stat st; return stat(path,&st);} int mkdir(const char*p,mode_t m){(void)p;(void)m;return 0;}
DIR*opendir(const char*name){ DIR*d=calloc(1,sizeof(DIR)); if(!d)return NULL; snprintf(d->prefix,sizeof(d->prefix),"%s",name?name:""); return d;} struct dirent*readdir(DIR*d){ if(!d)return NULL; size_t plen=strlen(d->prefix); while(d->idx<vfs_count){ const char*p=vfs[d->idx++].path; if(plen&&strncmp(p,d->prefix,plen)) continue; snprintf(d->ent.d_name,sizeof(d->ent.d_name),"%s",base_name(p)); return &d->ent;} return NULL;} int closedir(DIR*d){free(d);return 0;}
char *dirname(char*path){ char*s=strrchr(path,'/'); if(s&&s!=path)*s=0; else strcpy(path,"."); return path;} char*basename(char*path){return (char*)base_name(path);} time_t time(time_t*t){ if(t)*t=0; return 0;} int gettimeofday(struct timeval*tv,struct timezone*tz){ if(tv){tv->tv_sec=0;tv->tv_usec=0;} (void)tz; return 0;} size_t wcsrtombs(char*dest,const wchar_t**src,size_t len,mbstate_t*ps){(void)ps; if(!src||!*src)return 0; size_t n=0; const wchar_t*w=*src; while(w[n]){ if(dest&&n<len) dest[n]=(char)(w[n]&0x7f); n++; } if(dest&&n<len) dest[n]=0; return n;}

double floor(double x){ long long i=(long long)x; return (x<0 && x!=(double)i)?(double)(i-1):(double)i;} double ceil(double x){ long long i=(long long)x; return (x>0 && x!=(double)i)?(double)(i+1):(double)i;} double fabs(double x){return x<0?-x:x;} float fabsf(float x){return x<0?-x:x;} double round(double x){return x>=0?floor(x+0.5):ceil(x-0.5);} float roundf(float x){return x>=0?(float)floor((double)x+0.5):(float)ceil((double)x-0.5);} long lround(double x){return (long)round(x);} long lroundf(float x){return (long)roundf(x);} long lrint(double x){return lround(x);} long lrintf(float x){return lroundf(x);} double sqrt(double x){return __builtin_sqrt(x);} float sqrtf(float x){return __builtin_sqrtf(x);} double fmod(double x,double y){ long long q=(long long)(x/y); return x-(double)q*y;} float fmodf(float x,float y){return (float)fmod(x,y);} double exp2(double x); double pow(double x,double y){ if(y==0)return 1; if(y==1)return x; if(y==2)return x*x; if(x==2.0)return exp2(y); if(y<0)return 1.0/pow(x,-y); double r=1; int n=(int)y; for(int i=0;i<n;i++)r*=x; return r;} float powf(float x,float y){return (float)pow(x,y);} double sin(double x){return x;} double cos(double x){return 1.0;} double tan(double x){return sin(x)/cos(x);} double asin(double x){return x;} double acos(double x){return 1.57079632679-x;} double atan(double x){return x;} double atan2(double y,double x){(void)x;return y;} double exp(double x){return 1.0+x;} double exp2(double x){ int i=(int)floor(x); double f=x-(double)i; double r=1.0; if(i>=0){ while(i--) r*=2.0; } else { while(i++) r*=0.5; } double a=0.6931471805599453; return r*(1.0 + f*a + f*f*0.2402265069591007 + f*f*f*0.0555041086648216); } double log(double x){(void)x;return 0;} double log10(double x){(void)x;return 0;} float sinf(float x){return (float)sin(x);} float cosf(float x){return (float)cos(x);} float tanf(float x){return (float)tan(x);} 
