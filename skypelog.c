/* memmem */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>

enum
{
	SEC_START_REC = 0,
	SEC_START_CHAT,
	SEC_START_CALLER,
	SEC_MEM_SEP,
	SEC_START_RECIPIENTS,
	SEC_END_MEMBS,
	SEC_START_TIME,
	SEC_START_SENDER,
	SEC_START_MSG,
	SEC_NUM_SECTIONS
};

static const unsigned char sections[][SEC_NUM_SECTIONS] = {
	/* start_rec  */        { 0x6C, 0x33, 0x33, 0x6C, 0x00 },
	/* skip 14    */
	/* start_chat */        { 0xE0, 0x03, 0x00 },
	/* start_caller */      { 0x23, 0x00 },
	/* mem_sep    */        { 0x2F, 0x00 }, /* '/' */
	/* start_recipients */  { 0x34, 0x00 },
	/* end_membs  */        { 0x3B, 0x00 },
	/* start_time */        { 0xE5, 0x03, 0x00 },
	/* msg_sender */        { 0xE8, 0x03, 0x00 },
	/* start_msg  */        { 0xFC, 0x03, 0x00 }
};

static unsigned int MAX_DIST_TO_MSG = 50;

static const char *prog;


void die(const char *fmt, ...)
{
	va_list l;

	fprintf(stderr, "%s: ", prog);

	va_start(l, fmt);
	vfprintf(stderr, fmt, l);
	va_end(l);

	if(fmt[strlen(fmt)-1] == ':'){
		fputc(' ', stderr);
		perror(NULL);
	}else{
		fputc('\n', stderr);
	}

	exit(1);
}

time_t read_time(char *start)
{
	time_t t;
	char *t_ptr = (char *)&t;

	/* drop msb of every byte, join the remaining bits together */
	t_ptr[0] = ((start[1] << 7) & 0x80) | ((start[0] >> 0) & 0x7F);
	t_ptr[1] = ((start[2] << 6) & 0xC0) | ((start[1] >> 1) & 0x3F);
	t_ptr[2] = ((start[3] << 5) & 0xE0) | ((start[2] >> 2) & 0x1F);
	t_ptr[3] = ((start[4] << 4) & 0xF0) | ((start[3] >> 3) & 0x0F);

	return t;
}

char *find_section(char *start, char *data, size_t len, int n)
{
	char *pos = memmem(start, len - (start - data), sections[n], strlen((const char *)sections[n]));

	if(!pos)
		return NULL;

	return pos + strlen((const char *)sections[n]);
}

void output_chat(char *timestr, char *caller, char *recipients, char *sender, char *chatid, char *msg)
{
	/* check for newlines, output each separately, so skypelog.sh can parse and sort more easily */
	char *tok;
	
	if (strcmp(caller, sender))
		recipients = caller;

	for(tok = strtok(msg, "\n"); tok; tok = strtok(NULL, "\n"))
		printf("%s: %s: %s -> %s: %s\n", timestr, chatid, sender, recipients, tok);
}

void parse_data(char *data, size_t len)
{
	char *ptr;
	time_t time;
	char timestr[20];

	ptr = data;

	/* bail if we don't find a section, only warning if it's not the inital section */
#define FIND_SECTION(ptr, data, len, en) \
	do{ \
		ptr = find_section(ptr, data, len, en); \
		if(!ptr){ \
			if(en != 0) \
				fprintf(stderr, "%s: warning: couldn't find section %d\n", prog, en); \
			return; \
		} \
	}while(0)

	do{
		char *caller, *recipients, *msg, *chatid, *sender, *startsection;

		FIND_SECTION(ptr, data, len, SEC_START_REC);
		startsection = ptr;

		ptr += 14;
		FIND_SECTION(ptr, data, len, SEC_START_CHAT);
		FIND_SECTION(ptr, data, len, SEC_START_CALLER);
		caller = ptr;
		FIND_SECTION(ptr, data, len, SEC_MEM_SEP);
		ptr[-1] = '\0';

		ptr++; /* = FIND_SECTION(ptr - 1, data, len, 4); */
		recipients = ptr;
		FIND_SECTION(ptr, data, len, SEC_END_MEMBS);
		ptr[-1] = '\0';

		chatid = ptr;
		FIND_SECTION(ptr, data, len, SEC_START_TIME);
		ptr[-1] = '\0';
		time = read_time(ptr);
		ptr += 6;

		FIND_SECTION(ptr, data, len, SEC_START_SENDER);
		sender = ptr;

		FIND_SECTION(ptr, data, len, SEC_START_MSG);

		if ((ptr-sender) > MAX_DIST_TO_MSG) {
			ptr = startsection + 1;
			continue;
		}

		msg = ptr;
		ptr = memchr(ptr, 0, len - (ptr - data));

		strftime(timestr, sizeof timestr, "%Y-%m-%d.%H:%M:%S", localtime(&time));
		output_chat(timestr, caller, recipients, sender, chatid, msg);
	}while(1);
}

void process(const char *fname)
{
	int fd;
	void *buf;
	off_t sz;

	fd = open(fname, O_RDONLY);

	if(fd == -1)
		die("open \"%s\":", fname);

	sz = lseek(fd, 0, SEEK_END);

	buf = mmap(0, sz, PROT_WRITE, MAP_PRIVATE, fd, 0);
	if(buf == MAP_FAILED)
		die("mmap() on \"%s\":", fname);

	close(fd);

	parse_data(buf, sz);

	munmap(buf, sz);
}


int main(int argc, char **argv)
{
	DIR *d;
	struct dirent *ent;

	prog = argv[0];

	if(argc != 2)
		die("Usage: %s path/to/skype/profile", *argv);

	if(chdir(argv[1]) == -1)
		die("chdir():");

	d = opendir(".");
	if(!d)
		die("opendir():");

	while(errno = 0, ent = readdir(d))
		if(!strncmp(ent->d_name, "chat", 4)){
			int len = strlen(ent->d_name);

			if(len > 3 && !strcmp(ent->d_name + len - 4, ".dbb"))
				process(ent->d_name);
		}

	if(errno)
		die("readdir():");

	closedir(d);

	return 0;
}


