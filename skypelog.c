/*
 * http://www.ndmteam.com/skype-chat-logs-dissection/
 *
 * 6C 33 33 6C __ __ __ __ __ __ __ __ __ __ __ __ __ __
 * E0 03 23 -- -- -- ...   2F 34 -- -- -- ...   3B
 *         ^          ^
 *         +----------+- name1  +----------+- name2
 *
 * 1. Every record starts with byte sequence 0x6C 0x33 0x33 0x6C (l33l in ASCII)
 * 2. Next 14 bytes are there with unknown (at least for me) purpose
 * 3. 0xE0 0x03 – marker for the beginning of chat members field
 *   first chat member username is prefixed with 0x23 (# in ASCII)
 *   two chat members are separated with 0x2F (/ in ASCII)
 *   the second chat member username is prefixed with 0x34 ($ in ASCII)
 *   the list of chat members ends with 0x3B (; in ASCII)
 *
 *   Remark: I still have some problems with correct interpretation of this field for records with more then two chat members
 *
 * 4. The bytes after 0x3B to the next described number are with unknown content
 * 5. 0xE5 0x03 – marker for the beginning of 6 bytes sequence, representing the message timestamp. The numbers in all chat logs are stored in little-endian format. The fifth and the sixth byte seems to be constant in all the records - 0x04 0x03. The sixth byte is not used in the actual timestamp calculations (for now ... may be it'll be used in further moment). Bytes 1st to 5th represent message timestamp in standard Unix format.Normally only 4 bytes of information are needed to store Unix timestamp. That's why first I thought that bytes 5th and 6th are not used at all. But after some calculations it came clear that first 4 bytes did not represent the actual time since 1/1/1970. It came clear also that the most significant bit in every of the first 4 bytes is always 1. That's why it seems logically to me to conclude that those bits are sign bits and that they shouldn't be used in actual timestamp calculations. Striping those most significant bits from every of the first 4 bytes and combining the rest of the bits it was received 28bit combination. For the standard Unix time representation 32 bits of information are needed, so we just 'lend' last 4 bits from 5th byte. This 32 bit combination gave the Unix timestamp of the chat message
 * 6. 0xE8 0x03 – marker for the beginning of the sender username field. The field ends with zero byte 0x00
 * 7. 1.0xEC 0x03 – marker for the beginning of the sender screen name field. The field ends with zero byte 0x00
 * 8. 1.0xFC 0x03 – marker for the beginning of the message field. The field ends with zero byte 0x00
 *
 */

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
	SEC_START_SENDER,
	SEC_MEM_SEP,
	SEC_START_RECIPIENTS,
	SEC_END_MEMBS,
	SEC_START_TIME,
	SEC_START_MSG,
	SEC_NUM_SECTIONS
};

static const unsigned char sections[][SEC_NUM_SECTIONS] = {
	/* start_rec  */        { 0x6C, 0x33, 0x33, 0x6C, 0x0 },
	/* skip 14    */
	/* start_chat */        { 0xE0, 0x03, 0x0 },
	/* start_sender */      { 0x23, 0x00 },
	/* mem_sep    */        { 0x2F, 0x00 }, /* '/' */
	/* start_recipients */  { 0x34, 0x00 },
	/* end_membs  */        { 0x3B, 0x00 },
	/* start_time */        { 0xE5, 0x03, 0x0},
	/* start_msg  */        { 0xFC, 0x03, 0x0 }
};


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

void output_chat(char *timestr, char *sender, char *recipients, char *msg)
{
	/* check for newlines, output each separately, so skypelog.sh can parse and sort more easily */
	char *tok;

	for(tok = strtok(msg, "\n"); tok; tok = strtok(NULL, "\n"))
		printf("%s: %s <-> %s: %s\n", timestr, sender, recipients, tok);
}

void parse_data(char *data, size_t len)
{
	char *ptr;
	char *save;
	time_t time;
	char timestr[18];

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
		char *sender, *recipients, *msg;

		FIND_SECTION(ptr, data, len, SEC_START_REC);
		ptr += 14;
		FIND_SECTION(ptr, data, len, SEC_START_CHAT);
		FIND_SECTION(ptr, data, len, SEC_START_SENDER);
		save = ptr;
		FIND_SECTION(ptr, data, len, SEC_MEM_SEP);
		ptr[-1] = '\0';
		sender = save;

		ptr++; /* = FIND_SECTION(ptr - 1, data, len, 4); */
		save = ptr;
		FIND_SECTION(ptr, data, len, SEC_END_MEMBS);
		ptr[-1] = '\0';
		recipients = save;

		FIND_SECTION(ptr, data, len, SEC_START_TIME);
		time = read_time(ptr);
		ptr += 6;

		FIND_SECTION(ptr, data, len, SEC_START_MSG);
		save = ptr;
		ptr = memchr(ptr, 0, len - (ptr - data));
		msg = save;

		strftime(timestr, sizeof timestr, "%Y-%m-%d.%H%M%S", localtime(&time));
		output_chat(timestr, sender, recipients, msg);
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


