/*
 * fetch.c: the classic Glenda Fetch re-implemented 
 * in C for Linux systems. More or less this is kinda
 * incomplete and all so enjoy I guess.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <dirent.h>
#include <errno.h>

/* Helpers */

static void
chomp(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
		s[--n] = '\0';
}

/* OS/kernel */

static void
get_os(char *buf, size_t sz)
{
	FILE *f;
	char line[256];
	char name[128] = {0};

	f = fopen("/etc/os-release", "r");
	if (!f) f = fopen("/usr/lib/os-release", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
				char *val = line + 12;
				if (*val == '"') val++;
				chomp(val);
				size_t l = strlen(val);
				if (l && val[l-1] == '"') val[l-1] = '\0';
				snprintf(name, sizeof(name), "%s", val);
				break;
			}
		}
		fclose(f);
	}

	if (!name[0])
		snprintf(name, sizeof(name), "Linux");

	snprintf(buf, sz, "%s", name);
}

static void
get_kernel(char *buf, size_t sz)
{
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(buf, sz, "%s", u.release);
	else
		snprintf(buf, sz, "unknown");
}

/* CPU */

static void
shorten_cpu(char *s)
{
	char *p;
	while ((p = strstr(s, "(R)"))  != NULL) memmove(p, p+3, strlen(p+3)+1);
	while ((p = strstr(s, "(TM)")) != NULL) memmove(p, p+4, strlen(p+4)+1);
	if   ((p = strstr(s, " CPU"))  != NULL) memmove(p, p+4, strlen(p+4)+1);
	if   ((p = strstr(s, " @ "))   != NULL) *p = '\0';

	/* AMD: zap " X-Core Processor" suffix */
	if   ((p = strstr(s, "-Core")) != NULL) *p = '\0';

	/* tidy any double-spaces left behind */
	while ((p = strstr(s, "  ")) != NULL) memmove(p, p+1, strlen(p+1)+1);

	/* trim trailing space */
	size_t n = strlen(s);
	while (n > 0 && s[n-1] == ' ') s[--n] = '\0';
}

static void
get_cpu(char *buf, size_t sz)
{
	FILE *f;
	char line[256];

	f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		snprintf(buf, sz, "unknown");
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "model name", 10) == 0) {
			char *colon = strchr(line, ':');
			if (colon) {
				colon++;
				while (*colon == ' ') colon++;
				chomp(colon);
				snprintf(buf, sz, "%s", colon);
				shorten_cpu(buf);
				fclose(f);
				return;
			}
		}
	}
	fclose(f);
	snprintf(buf, sz, "unknown");
}

/* RAM */

static void
get_ram(char *used_buf, size_t used_sz, char *total_buf, size_t total_sz)
{
	FILE *f;
	char line[256];
	unsigned long long total = 0, memfree = 0, buffers = 0;
	unsigned long long cached = 0, sreclaimable = 0, shmem = 0;

	f = fopen("/proc/meminfo", "r");
	if (!f) {
		snprintf(used_buf,  used_sz,  "?");
		snprintf(total_buf, total_sz, "?");
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		if      (strncmp(line, "MemTotal:",     9)  == 0) sscanf(line + 9,  "%llu", &total);
		else if (strncmp(line, "MemFree:",      8)  == 0) sscanf(line + 8,  "%llu", &memfree);
		else if (strncmp(line, "Buffers:",      8)  == 0) sscanf(line + 8,  "%llu", &buffers);
		else if (strncmp(line, "Cached:",       7)  == 0 &&
		         strncmp(line, "SwapCached:",  11)  != 0) sscanf(line + 7,  "%llu", &cached);
		else if (strncmp(line, "SReclaimable:", 13) == 0) sscanf(line + 13, "%llu", &sreclaimable);
		else if (strncmp(line, "Shmem:",        6)  == 0) sscanf(line + 6,  "%llu", &shmem);
	}
	fclose(f);

	unsigned long long usedDiff = memfree + cached + sreclaimable + buffers;
	unsigned long long used_kb  = (total >= usedDiff) ? total - usedDiff : total - memfree;
	used_kb += shmem;
	double total_gib = total   / (1024.0 * 1024.0);
	double used_gib  = used_kb / (1024.0 * 1024.0);
	snprintf(used_buf,  used_sz,  "%.2f", used_gib);
	snprintf(total_buf, total_sz, "%.2f", total_gib);
}

/* Uptime */

static void
get_uptime(char *buf, size_t sz)
{
	struct sysinfo si;
	if (sysinfo(&si) != 0) {
		snprintf(buf, sz, "unknown");
		return;
	}
	long up = si.uptime;
	int days  = up / 86400;
	int hours = (up % 86400) / 3600;
	int mins  = (up % 3600)  / 60;

	if (days > 0)
		snprintf(buf, sz, "%dd %dh %dm", days, hours, mins);
	else if (hours > 0)
		snprintf(buf, sz, "%dh %dm", hours, mins);
	else
		snprintf(buf, sz, "%dm", mins);
}

/* Shell */

static void
get_shell(char *buf, size_t sz)
{
	const char *s = getenv("SHELL");
	if (s) {
		const char *b = strrchr(s, '/');
		snprintf(buf, sz, "%s", b ? b + 1 : s);
	} else {
		snprintf(buf, sz, "unknown");
	}
}


/* disk */

static void
get_disk(char *buf, size_t sz)
{
	struct statvfs st;
	if (statvfs("/", &st) != 0) {
		snprintf(buf, sz, "unknown");
		return;
	}
	unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
	unsigned long long free  = (unsigned long long)st.f_bfree  * st.f_frsize;
	unsigned long long used  = total - free;

	double used_gib  = used  / (1024.0 * 1024.0 * 1024.0);
	double total_gib = total / (1024.0 * 1024.0 * 1024.0);
	snprintf(buf, sz, "%.1f / %.1f GiB", used_gib, total_gib);
}

/*
 * Resolution (reads from /dev/fb0 via ioctl)
 * TODO: Swap with GPU because no one gives a
 * damn about one's screen resolution
 * */

static void
get_resolution(char *buf, size_t sz)
{
	int fd;
	struct fb_var_screeninfo vinfo;

	fd = open("/dev/fb0", O_RDONLY);
	if (fd >= 0) {
		if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
			snprintf(buf, sz, "%ux%u", vinfo.xres, vinfo.yres);
			close(fd);
			return;
		}
		close(fd);
	}

	snprintf(buf, sz, "unavailable");
}

/* Glenda the rabbit */

static const char *rabbit[] = {
	"             %s@%s",
	"    (\\(\\     -----------",
	"   j\". ..    os: %s",
	"   (  . .)   kernel: %s",
	"   |   \xc2\xb0 \xc2\xa1   shell: %s",
	"   \xc2\xbf     ;   uptime: %s",
	"   c?\".UJ    cpu: %s",
	"             ram: %s / %s GiB",
	"             disk: %s",
	"             res: %s",
	NULL
};

/* main */

int
main(void)
{
	char os[128], kernel[128], cpu[256];
	char shell[64], uptime[64];
	char ram_used[32], ram_total[32];
	char disk[64], res[64];
	char hostname[64] = {0};
	const char *user;

	get_os(os, sizeof(os));
	get_kernel(kernel, sizeof(kernel));
	get_cpu(cpu, sizeof(cpu));
	get_shell(shell, sizeof(shell));
	get_uptime(uptime, sizeof(uptime));
	get_ram(ram_used, sizeof(ram_used), ram_total, sizeof(ram_total));
	get_disk(disk, sizeof(disk));
	get_resolution(res, sizeof(res));

	gethostname(hostname, sizeof(hostname) - 1);
	user = getenv("USER");
	if (!user) user = getenv("LOGNAME");
	if (!user) user = "unknown";

	printf(rabbit[0], user, hostname);      putchar('\n');
	printf("%s\n", rabbit[1]);
	printf(rabbit[2], os); putchar('\n');
	printf(rabbit[3], kernel); putchar('\n');
	printf(rabbit[4], shell); putchar('\n');
	printf(rabbit[5], uptime); putchar('\n');
	printf(rabbit[6], cpu); putchar('\n');
	printf(rabbit[7], ram_used, ram_total); putchar('\n');
	printf(rabbit[8], disk); putchar('\n');
	printf(rabbit[9], res); putchar('\n');

	return 0;
}
