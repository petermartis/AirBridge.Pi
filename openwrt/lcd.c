#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#endif

static int W = 320, H = 240, rotation = 90;
enum { BLACK=0, WHITE=0xffff, BLUE=0x1a3f, CYAN=0x07ff,
       GREEN=0x07e0, RED=0xf800, GRAY=0x7bef, YELLOW=0xffe0, LINE=0x4208 };
static uint16_t frame[320][320];
static volatile sig_atomic_t running = 1;
static int spi_fd = -1, gpio_fd = -1;
static uint32_t speed = 1000000;

/* Five columns, low bit at the top. Small, fixed-size status-screen alphabet. */
static const struct glyph { char c; uint8_t col[5]; } font[] = {
 {' ',{0,0,0,0,0}}, {'!',{0,0,95,0,0}}, {'%',{99,19,8,100,99}},
 {'(',{0,28,34,65,0}}, {')',{0,65,34,28,0}}, {'+',{8,8,62,8,8}},
 {'-',{8,8,8,8,8}}, {'.',{0,96,96,0,0}}, {'/',{32,16,8,4,2}},
 {':',{0,54,54,0,0}}, {'?',{2,1,81,9,6}}, {'_',{64,64,64,64,64}},
 {'0',{62,81,73,69,62}}, {'1',{0,66,127,64,0}},
 {'2',{66,97,81,73,70}}, {'3',{33,65,69,75,49}},
 {'4',{24,20,18,127,16}}, {'5',{39,69,69,69,57}},
 {'6',{60,74,73,73,48}}, {'7',{1,113,9,5,3}},
 {'8',{54,73,73,73,54}}, {'9',{6,73,73,41,30}},
 {'A',{126,17,17,17,126}}, {'B',{127,73,73,73,54}},
 {'C',{62,65,65,65,34}}, {'D',{127,65,65,34,28}},
 {'E',{127,73,73,73,65}}, {'F',{127,9,9,9,1}},
 {'G',{62,65,73,73,122}}, {'H',{127,8,8,8,127}},
 {'I',{0,65,127,65,0}}, {'J',{32,64,65,63,1}},
 {'K',{127,8,20,34,65}}, {'L',{127,64,64,64,64}},
 {'M',{127,2,12,2,127}}, {'N',{127,4,8,16,127}},
 {'O',{62,65,65,65,62}}, {'P',{127,9,9,9,6}},
 {'Q',{62,65,81,33,94}}, {'R',{127,9,25,41,70}},
 {'S',{70,73,73,73,49}}, {'T',{1,1,127,1,1}},
 {'U',{63,64,64,64,63}}, {'V',{31,32,64,32,31}},
 {'W',{63,64,56,64,63}}, {'X',{99,20,8,20,99}},
 {'Y',{7,8,112,8,7}}, {'Z',{97,81,73,69,67}}
};

static void rect(int x, int y, int w, int h, uint16_t color) {
    for (int j=y; j<y+h && j<H; j++)
        for (int i=x; i<x+w && i<W; i++)
            if (i>=0 && j>=0) frame[j][i]=color;
}
static void text(int x, int y, const char *s, int scale, uint16_t color, int max) {
    for (int n=0; *s && n<max; s++, n++, x+=6*scale) {
        const struct glyph *g=NULL;
        int c=toupper((unsigned char)*s);
        for (size_t k=0; k<sizeof(font)/sizeof(font[0]); k++)
            if (font[k].c==c) { g=&font[k]; break; }
        if (!g) g=&font[10];
        for (int i=0; i<5; i++)
            for (int j=0; j<7; j++)
                if (g->col[i] & (1u<<j)) rect(x+i*scale,y+j*scale,scale,scale,color);
    }
}
struct status {
    char ssid[128], ip[64], eth[32], ap[32], radio[128], mode[32];
    char clients[6][128];
    int nclients, total, fresh;
};
static void copy(char *out, size_t n, const char *in) { snprintf(out,n,"%s",in); }
static void load_status(const char *path, struct status *s) {
    memset(s,0,sizeof(*s));
    copy(s->ssid,sizeof(s->ssid),"UNAVAILABLE");
    copy(s->ip,sizeof(s->ip),"NO ADDRESS");
    copy(s->eth,sizeof(s->eth),"?"); copy(s->ap,sizeof(s->ap),"?");
    copy(s->mode,sizeof(s->mode),"UNKNOWN");
    FILE *f=fopen(path,"r");
    if (!f) return;
    struct stat st;
    s->fresh=fstat(fileno(f),&st)==0 && time(NULL)-st.st_mtime<=15;
    char line[512];
    while (fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;
        char *v=strchr(line,'\t');
        if (!v) continue;
        *v++=0;
        if (!strcmp(line,"SSID")) copy(s->ssid,sizeof(s->ssid),v);
        else if (!strcmp(line,"IP")) copy(s->ip,sizeof(s->ip),v);
        else if (!strcmp(line,"ETH")) copy(s->eth,sizeof(s->eth),v);
        else if (!strcmp(line,"AP")) copy(s->ap,sizeof(s->ap),v);
        else if (!strcmp(line,"RADIO")) copy(s->radio,sizeof(s->radio),v);
        else if (!strcmp(line,"MODE")) copy(s->mode,sizeof(s->mode),v);
        else if (!strcmp(line,"COUNT")) {
            long count=strtol(v,NULL,10);
            s->total=count<0 ? 0 : count>999 ? 999 : (int)count;
        }
        else if (!strcmp(line,"CLIENT") && s->nclients<6)
            copy(s->clients[s->nclients++],sizeof(s->clients[0]),v);
    }
    fclose(f);
}
#ifdef __linux__
static int cpu_percent(void) {
    static unsigned long long prev_total,prev_idle;
    unsigned long long a,b,c,d,e,f,g,h;
    FILE *p=fopen("/proc/stat","r");
    if (!p) return -1;
    int n=fscanf(p,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&a,&b,&c,&d,&e,&f,&g,&h);
    fclose(p);
    if (n!=8) return -1;
    unsigned long long total=a+b+c+d+e+f+g+h,idle=d+e,dt=total-prev_total,di=idle-prev_idle;
    int result=prev_total && dt ? (int)(100*(dt-di)/dt) : 0;
    prev_total=total; prev_idle=idle;
    return result<0 ? 0 : result>100 ? 100 : result;
}
static int mem_percent(void) {
    FILE *p=fopen("/proc/meminfo","r");
    if (!p) return -1;
    unsigned long total=0,available=0,value; char line[256];
    while (fgets(line,sizeof(line),p)) {
        if (sscanf(line,"MemTotal: %lu",&value)==1) total=value;
        if (sscanf(line,"MemAvailable: %lu",&value)==1) available=value;
    }
    fclose(p);
    return total ? (int)(100*(total-available)/total) : -1;
}
#endif
static void dashboard(const struct status *s, int cpu, int mem) {
    memset(frame,0,sizeof(frame));
    rect(0,0,W,28,BLUE); text(8,6,"AirBridge.Pi",W==320?2:1,WHITE,12);
    char buf[128];
    snprintf(buf,sizeof(buf),"CPU:%d%% RAM:%d%%",cpu,mem);
    text(W==320?152:124,10,buf,1,WHITE,27);
    text(8,37,"SSID",1,CYAN,8); text(8,51,s->ssid,2,WHITE,(W-16)/12);
    text(8,78,"PASSWORD",1,CYAN,12); text(8,93,"HIDDEN",1,GREEN,15);
    text(W/2,78,"IP ADDRESS",1,CYAN,20);
    text(W/2,91,s->ip,W==320?2:1,WHITE,15);
    rect(0,112,W,1,LINE);
    snprintf(buf,sizeof(buf),"ETH: %s",s->eth);
    text(8,121,buf,1,!strcmp(s->eth,"UP")?GREEN:GRAY,18);
    snprintf(buf,sizeof(buf),"AP: %s",s->ap);
    text(W/3+8,121,buf,1,!strcmp(s->ap,"UP")?GREEN:RED,14);
    text(2*W/3+8,121,s->mode,1,GRAY,15);
    text(8,139,s->radio,1,CYAN,(W-16)/6);
    rect(0,154,W,1,LINE);
    snprintf(buf,sizeof(buf),"CLIENTS (%d)",s->total);
    text(8,164,buf,1,CYAN,25);
    if (!s->fresh) text(W-114,164,"STATUS STALE",1,RED,18);
    if (!s->nclients) text(8,184,"NO CONNECTED DEVICES",1,GRAY,40);
    for (int i=0; i<s->nclients; i++)
        text(8,180+i*9,s->clients[i],1,YELLOW,(W-16)/6);
}
static void pattern(void) {
    const uint16_t colors[]={RED,GREEN,0x001f,WHITE,CYAN,YELLOW};
    for (int i=0; i<6; i++) rect(i*W/6,0,W/6+1,H,colors[i]);
    rect(8,8,W-16,32,BLACK); text(14,16,"AIRBRIDGE.PI LCD",2,WHITE,20);
    rect(8,H-44,W-16,36,BLACK);
    char label[64];
    snprintf(label,sizeof(label),"%d X %d - ROTATE %d",W,H,rotation);
    text(14,H-38,label,1,WHITE,40);
    text(14,H-22,"RED GREEN BLUE WHITE CYAN YELLOW",1,WHITE,(W-28)/6);
}
static int ppm(const char *path) {
    FILE *p=fopen(path,"wb"); if (!p) return -1;
    fprintf(p,"P6\n%d %d\n255\n",W,H);
    for (int y=0; y<H; y++) for (int x=0; x<W; x++) {
        unsigned v=frame[y][x];
        unsigned char rgb[]={(unsigned char)(((v>>11)&31)*255/31),
                             (unsigned char)(((v>>5)&63)*255/63),
                             (unsigned char)((v&31)*255/31)};
        if (fwrite(rgb,1,3,p)!=3) { fclose(p); return -1; }
    }
    return fclose(p);
}
#ifdef __linux__
static int gpio_chip(int verbose) {
    glob_t paths={0}; int result=-1, matches=0;
    if (glob("/dev/gpiochip*",0,NULL,&paths)) return -1;
    for (size_t i=0; i<paths.gl_pathc; i++) {
        int fd=open(paths.gl_pathv[i],O_RDWR|O_CLOEXEC);
        struct gpiochip_info info={0};
        if (fd<0) continue;
        if (ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&info)<0) { close(fd); continue; }
        if (verbose) printf("%s label=%s lines=%u\n",paths.gl_pathv[i],info.label,info.lines);
        struct gpio_v2_line_info dc={.offset=24},rst={.offset=25};
        if (info.lines>25 && !ioctl(fd,GPIO_V2_GET_LINEINFO_IOCTL,&dc) &&
            !ioctl(fd,GPIO_V2_GET_LINEINFO_IOCTL,&rst)) {
            if (verbose) printf("  line24=%s line25=%s\n",dc.name,rst.name);
            if (strstr(info.label,"rp1") && !strcmp(dc.name,"GPIO24") && !strcmp(rst.name,"GPIO25")) {
                if (result>=0) close(result);
                result=fd; matches++; continue;
            }
        }
        close(fd);
    }
    globfree(&paths);
    if (matches!=1) { if (result>=0) close(result); errno=ENODEV; return -1; }
    return result;
}
static int pins(void) {
    int chip=gpio_chip(0); if (chip<0) return -1;
    struct gpio_v2_line_request req={0};
    req.offsets[0]=24; req.offsets[1]=25; req.num_lines=2;
    copy(req.consumer,sizeof(req.consumer),"airbridge-lcd");
    req.config.flags=GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs=1;
    req.config.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values=3; req.config.attrs[0].mask=3;
    int rc=ioctl(chip,GPIO_V2_GET_LINE_IOCTL,&req); close(chip);
    if (rc<0) return -1;
    gpio_fd=req.fd; return 0;
}
static int pin(unsigned index, int high) {
    struct gpio_v2_line_values v={.bits=high ? 1ULL<<index : 0,.mask=1ULL<<index};
    return ioctl(gpio_fd,GPIO_V2_LINE_SET_VALUES_IOCTL,&v);
}
static int bytes(const void *buf,size_t n) {
    const uint8_t *p=buf;
    while (n) {
        size_t count=n>4096?4096:n;
        ssize_t rc=write(spi_fd,p,count);
        if (rc<0 && errno==EINTR) { if (running) continue; return -1; }
        if (rc!=(ssize_t)count) { if (rc>=0) errno=EIO; return -1; }
        p+=count; n-=count;
    }
    return 0;
}
static int command(uint8_t c,const uint8_t *data,size_t n) {
    if (pin(0,0)<0 || bytes(&c,1)<0 || pin(0,1)<0) return -1;
    return n ? bytes(data,n) : 0;
}
#define CMD(c, ...) do { const uint8_t d[]={__VA_ARGS__}; \
    if (command(c,d,sizeof(d))<0) return -1; } while (0)
static int panel_init(void) {
    if (pin(1,1)<0) return -1;
    usleep(10000); if (pin(1,0)<0) return -1;
    usleep(20000); if (pin(1,1)<0) return -1; usleep(150000);
    if (command(0x01,NULL,0)<0) return -1;
    usleep(150000);
    /* Read identification before initialization; a zero/FF response is not proof of identity. */
    if (command(0xd3,NULL,0)<0) return -1;
    uint8_t id[4]={0};
    struct spi_ioc_transfer tr={.rx_buf=(uintptr_t)id,.len=4,.speed_hz=1000000,.bits_per_word=8};
    if (ioctl(spi_fd,SPI_IOC_MESSAGE(1),&tr)<0) return -1;
    printf("LCD ID (dummy + response): %02x %02x %02x %02x\n",id[0],id[1],id[2],id[3]);
    if ((id[2] || id[3]) && !(id[2]==0xff && id[3]==0xff) &&
        !(id[2]==0x93 && id[3]==0x41)) {
        fprintf(stderr,"Unexpected LCD ID; refusing ILI9341 initialization\n"); errno=ENODEV; return -1;
    }
    CMD(0xef,0x03,0x80,0x02); CMD(0xcf,0x00,0xc1,0x30);
    CMD(0xed,0x64,0x03,0x12,0x81); CMD(0xe8,0x85,0x00,0x78);
    CMD(0xcb,0x39,0x2c,0x00,0x34,0x02); CMD(0xf7,0x20);
    CMD(0xea,0x00,0x00); CMD(0xc0,0x23); CMD(0xc1,0x10);
    CMD(0xc5,0x3e,0x28); CMD(0xc7,0x86);
    const uint8_t orientation[]={0x28,0x88,0xe8,0x48};
    CMD(0x36,orientation[rotation/90]);
    CMD(0x3a,0x55); CMD(0xb1,0x00,0x18); CMD(0xb6,0x08,0x82,0x27);
    CMD(0xf2,0x00); CMD(0x26,0x01);
    CMD(0xe0,0x0f,0x31,0x2b,0x0c,0x0e,0x08,0x4e,0xf1,0x37,0x07,0x10,0x03,0x0e,0x09,0x00);
    CMD(0xe1,0x00,0x0e,0x14,0x03,0x11,0x07,0x31,0xc1,0x48,0x08,0x0f,0x0c,0x31,0x36,0x0f);
    if (command(0x11,NULL,0)<0) return -1;
    usleep(150000);
    if (command(0x29,NULL,0)<0) return -1;
    usleep(20000);
    return 0;
}
static int present(void) {
    uint8_t row[W*2];
    for (int y=0; y<H; y++) {
        CMD(0x2a,0,0,(uint8_t)((W-1)>>8),(uint8_t)(W-1));
        CMD(0x2b,(uint8_t)(y>>8),(uint8_t)y,(uint8_t)(y>>8),(uint8_t)y);
        if (command(0x2c,NULL,0)<0) return -1;
        for (int x=0; x<W; x++) { row[2*x]=frame[y][x]>>8; row[2*x+1]=frame[y][x]&255; }
        if (bytes(row,sizeof(row))<0) return -1;
    }
    return 0;
}
#endif
static void stop(int sig) { (void)sig; running=0; }
int main(int argc,char **argv) {
    const char *device="/dev/spidev0.0",*status="/var/run/airbridge-lcd/status",*out=NULL;
    int test=0,probe=0,geometry=0;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"--test")) test=1;
        else if (!strcmp(argv[i],"--probe")) probe=1;
        else if (!strcmp(argv[i],"--device") && i+1<argc) device=argv[++i];
        else if (!strcmp(argv[i],"--status") && i+1<argc) status=argv[++i];
        else if (!strcmp(argv[i],"--render") && i+1<argc) out=argv[++i];
        else if (!strcmp(argv[i],"--geometry") && i+1<argc) {
            const char *size=argv[++i];
            if (!strcmp(size,"320x240")) geometry=1;
            else if (!strcmp(size,"240x320")) geometry=2;
            else { fprintf(stderr,"Geometry must be 320x240 or 240x320\n"); return 2; }
        }
        else if (!strcmp(argv[i],"--rotation") && i+1<argc) {
            const char *angle=argv[++i];
            if (!strcmp(angle,"0")) rotation=0;
            else if (!strcmp(angle,"90")) rotation=90;
            else if (!strcmp(angle,"180")) rotation=180;
            else if (!strcmp(angle,"270")) rotation=270;
            else { fprintf(stderr,"Rotation must be 0, 90, 180 or 270\n"); return 2; }
        }
        else if (!strcmp(argv[i],"--speed") && i+1<argc) {
            char *end=NULL;
            long hz=strtol(argv[++i],&end,10);
            if (*end || hz<100000 || hz>32000000) {
                fprintf(stderr,"SPI speed must be 100000..32000000 Hz\n"); return 2;
            }
            speed=(uint32_t)hz;
        }
        else { fprintf(stderr,"Usage: %s [--probe|--test] [--render output.ppm] [--status file] [--device /dev/spidev0.0] [--speed Hz] [--rotation 0|90|180|270] [--geometry 320x240|240x320]\n",argv[0]); return 2; }
    }
    if (rotation%180) { W=240; H=320; }
    if (geometry==1) { W=320; H=240; }
    if (geometry==2) { W=240; H=320; }
    if (out) {
        struct status s; load_status(status,&s);
        if (test) pattern(); else dashboard(&s,12,18);
        if (ppm(out)<0) { perror("render"); return 1; } return 0;
    }
#ifdef __linux__
    if (probe) { int fd=gpio_chip(1); if (fd<0) { perror("RP1 GPIO discovery"); return 1; } close(fd); return 0; }
    int lock=open("/var/run/airbridge-lcd.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if (lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) { perror("LCD already in use / lock"); return 1; }
    spi_fd=open(device,O_RDWR|O_CLOEXEC);
    uint8_t mode=SPI_MODE_0,bits=8;
    if (spi_fd<0 || ioctl(spi_fd,SPI_IOC_WR_MODE,&mode)<0 ||
        ioctl(spi_fd,SPI_IOC_WR_BITS_PER_WORD,&bits)<0 ||
        ioctl(spi_fd,SPI_IOC_WR_MAX_SPEED_HZ,&speed)<0 || pins()<0) { perror("SPI/GPIO setup"); return 1; }
    signal(SIGINT,stop); signal(SIGTERM,stop);
    if (panel_init()<0) { perror("LCD init"); return 1; }
    while (running) {
        struct status s; load_status(status,&s);
        if (test) pattern(); else dashboard(&s,cpu_percent(),mem_percent());
        if (present()<0) { if (running) perror("LCD write"); return running?1:0; }
        sleep(2);
    }
    close(gpio_fd); close(spi_fd); close(lock); return 0;
#else
    (void)device; (void)probe; (void)spi_fd; (void)gpio_fd; (void)speed; (void)stop;
    fprintf(stderr,"Hardware access requires Linux; use --render on this host\n"); return 1;
#endif
}
