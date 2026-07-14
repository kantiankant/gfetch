/* gfetch.c: the classic Glenda Fetch re-implemented in C for FreeBSD.*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/types.h>

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
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(buf, sz, "%s %s", u.sysname, u.release);
	else
		snprintf(buf, sz, "FreeBSD");
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

	if   ((p = strstr(s, "-Core")) != NULL) *p = '\0';

	while ((p = strstr(s, "  ")) != NULL) memmove(p, p+1, strlen(p+1)+1);

	size_t n = strlen(s);
	while (n > 0 && s[n-1] == ' ') s[--n] = '\0';
}

static void
get_cpu(char *buf, size_t sz)
{
	char model[256] = {0};
	size_t len = sizeof(model);

	if (sysctlbyname("hw.model", model, &len, NULL, 0) != 0) {
		snprintf(buf, sz, "unknown");
		return;
	}
	chomp(model);
	snprintf(buf, sz, "%s", model);
	shorten_cpu(buf);
}

/* RAM */

static void
get_ram(char *used_buf, size_t used_sz, char *total_buf, size_t total_sz)
{
	u_long physmem = 0;
	u_int  pagesize = 0;
	u_int  free_count = 0, inactive_count = 0, cache_count = 0;
	size_t len;

	len = sizeof(physmem);
	if (sysctlbyname("hw.physmem", &physmem, &len, NULL, 0) != 0) {
		snprintf(used_buf,  used_sz,  "?");
		snprintf(total_buf, total_sz, "?");
		return;
	}

	len = sizeof(pagesize);
	sysctlbyname("vm.stats.vm.v_page_size", &pagesize, &len, NULL, 0);
	if (pagesize == 0)
		pagesize = getpagesize();

	len = sizeof(free_count);
	sysctlbyname("vm.stats.vm.v_free_count", &free_count, &len, NULL, 0);

	len = sizeof(inactive_count);
	sysctlbyname("vm.stats.vm.v_inactive_count", &inactive_count, &len, NULL, 0);

	len = sizeof(cache_count);
	sysctlbyname("vm.stats.vm.v_cache_count", &cache_count, &len, NULL, 0);

	unsigned long long total_bytes = (unsigned long long)physmem;
	unsigned long long reclaimable_pages =
	    (unsigned long long)free_count +
	    (unsigned long long)inactive_count +
	    (unsigned long long)cache_count;
	unsigned long long reclaimable_bytes = reclaimable_pages * pagesize;

	unsigned long long used_bytes =
	    (total_bytes >= reclaimable_bytes) ? total_bytes - reclaimable_bytes : 0;

	double total_gib = total_bytes / (1024.0 * 1024.0 * 1024.0);
	double used_gib  = used_bytes  / (1024.0 * 1024.0 * 1024.0);
	snprintf(used_buf,  used_sz,  "%.2f", used_gib);
	snprintf(total_buf, total_sz, "%.2f", total_gib);
}

/* Uptime */

static void
get_uptime(char *buf, size_t sz)
{
	struct timeval boottime;
	size_t len = sizeof(boottime);

	if (sysctlbyname("kern.boottime", &boottime, &len, NULL, 0) != 0) {
		snprintf(buf, sz, "unknown");
		return;
	}

	time_t now = time(NULL);
	long up = (long)(now - boottime.tv_sec);
	if (up < 0) up = 0;

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

/* Disk */

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

/* GPU */

static void
get_gpu(char *buf, size_t sz)
{
	FILE *p;
	char line[512];
	char class_line[512] = {0};
	char device_line[512] = {0};
	int found = 0;

	p = popen("pciconf -lv 2>/dev/null", "r");
	if (!p) {
		snprintf(buf, sz, "unknown");
		return;
	}

	while (fgets(line, sizeof(line), p)) {
		if (line[0] != '\t' && line[0] != ' ') {
			/* new device tag: reset any partial block */
			class_line[0] = '\0';
			device_line[0] = '\0';
			continue;
		}
		char *trimmed = line;
		while (*trimmed == '\t' || *trimmed == ' ') trimmed++;

		if (strncmp(trimmed, "class=", 6) == 0) {
			snprintf(class_line, sizeof(class_line), "%s", trimmed);
		} else if (strncmp(trimmed, "device=", 7) == 0) {
			snprintf(device_line, sizeof(device_line), "%s", trimmed);
		}

		if (class_line[0] && device_line[0]) {
			/* pciconf reports display controllers as class=0x03xxxx */
			if (strstr(class_line, "0x03") != NULL) {
				char *q1 = strchr(device_line, '\'');
				char *q2 = q1 ? strchr(q1 + 1, '\'') : NULL;
				if (q1 && q2 && q2 > q1) {
					*q2 = '\0';
					snprintf(buf, sz, "%s", q1 + 1);
					found = 1;
					break;
				}
			}
			class_line[0] = '\0';
			device_line[0] = '\0';
		}
	}
	pclose(p);

	if (!found)
		snprintf(buf, sz, "unknown");
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
	"             gpu: %s",
	NULL
};

/* main */

int
main(void)
{
	char os[128], kernel[128], cpu[256];
	char shell[64], uptime[64];
	char ram_used[32], ram_total[32];
	char disk[64], gpu[128];
	char hostname[64] = {0};
	const char *user;

	get_os(os, sizeof(os));
	get_kernel(kernel, sizeof(kernel));
	get_cpu(cpu, sizeof(cpu));
	get_shell(shell, sizeof(shell));
	get_uptime(uptime, sizeof(uptime));
	get_ram(ram_used, sizeof(ram_used), ram_total, sizeof(ram_total));
	get_disk(disk, sizeof(disk));
	get_gpu(gpu, sizeof(gpu));

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
	printf(rabbit[9], gpu); putchar('\n');

	return 0;
}
