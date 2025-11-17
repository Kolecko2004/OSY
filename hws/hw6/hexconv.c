#define SYS_exit  1
#define SYS_read  3
#define SYS_write 4

#define STDIN_FILENO  0
#define STDOUT_FILENO 1

static int my_read(int fd, void *buf, unsigned count)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (SYS_read), "b" (fd), "c" (buf), "d" (count)
        : "memory"
    );
    return ret;
}

static int my_write(int fd, const void *buf, unsigned count)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (SYS_write), "b" (fd), "c" (buf), "d" (count)
        : "memory"
    );
    return ret;
}

static void my_exit(int status)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a" (SYS_exit), "b" (status)
    );
    __builtin_unreachable();
}

static unsigned my_strlen(const char *s)
{
    unsigned len = 0;
    while (s[len])
        len++;
    return len;
}

static void num_to_hex(unsigned num, char *buf)
{
    const char hex_digits[] = "0123456789abcdef";
    int i;
    
    buf[0] = '0';
    buf[1] = 'x';
    
    for (i = 9; i >= 2; i--) {
        buf[i] = hex_digits[num & 0xF];
        num >>= 4;
    }
    
    i = 2;
    while (i < 9 && buf[i] == '0')
        i++;
    
    if (i > 2) {
        int j = 2;
        while (i <= 9) {
            buf[j++] = buf[i++];
        }
        buf[j] = '\n';
        buf[j + 1] = '\0';
    } else {
        buf[10] = '\n';
        buf[11] = '\0';
    }
}

static int isnum(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int isspc(char ch)
{
    return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r';
}

static void print(unsigned num)
{
    char buf[20];
    num_to_hex(num, buf);
    int ret = my_write(STDOUT_FILENO, buf, my_strlen(buf));
    if (ret == -1)
        my_exit(1);
}

void _start()
{
    char buf[20];
    unsigned num = 0;
    int i = 0;
    int num_digits = 0;
    unsigned chars_to_process = 0;

    for (;; i++, chars_to_process--) {
        if (chars_to_process == 0) {
            int ret = my_read(STDIN_FILENO, buf, sizeof(buf));
            if (ret < 0)
                my_exit(1);
            i = 0;
            chars_to_process = ret;
        }
        if (
            num_digits > 0
            && (chars_to_process == 0 || !isnum(buf[i]))
        ) {
            print(num);
            num_digits = 0;
            num = 0;
        }
        if (chars_to_process == 0)
            my_exit(0);
        
        if (!isspc(buf[i]) && !isnum(buf[i]))
            my_exit(0);

        if (isnum(buf[i])) {
            num = num * 10 + buf[i] - '0';
            num_digits++;
        }
    }
}