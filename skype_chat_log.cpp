/* http://www.ndmteam.com/skype-chat-logs-dissection/ 

6C 33 33 6C __ __ __ __ __ __ __ __ __ __ __ __ __ __
E0 03 23 -- -- -- ...   2F 34 -- -- -- ...   3B
         ^          ^
         +----------+- name1  +----------+- name2

1. Every record starts with byte sequence 0x6C 0x33 0x33 0x6C (l33l in ASCII)
2. Next 14 bytes are there with unknown (at least for me) purpose
3. 0xE0 0x03 – marker for the beginning of chat members field
   first chat member username is prefixed with 0x23 (# in ASCII)
   two chat members are separated with 0x2F (/ in ASCII)
   the second chat member username is prefixed with 0x34 ($ in ASCII)
   the list of chat members ends with 0x3B (; in ASCII)

   Remark: I still have some problems with correct interpretation of this field for records with more then two chat members

4. The bytes after 0x3B to the next described number are with unknown content
5. 0xE5 0x03 – marker for the beginning of 6 bytes sequence, representing the message timestamp. The numbers in all chat logs are stored in little-endian format. The fifth and the sixth byte seems to be constant in all the records - 0x04 0x03. The sixth byte is not used in the actual timestamp calculations (for now ... may be it'll be used in further moment). Bytes 1st to 5th represent message timestamp in standard Unix format.Normally only 4 bytes of information are needed to store Unix timestamp. That's why first I thought that bytes 5th and 6th are not used at all. But after some calculations it came clear that first 4 bytes did not represent the actual time since 1/1/1970. It came clear also that the most significant bit in every of the first 4 bytes is always 1. That's why it seems logically to me to conclude that those bits are sign bits and that they shouldn't be used in actual timestamp calculations. Striping those most significant bits from every of the first 4 bytes and combining the rest of the bits it was received 28bit combination. For the standard Unix time representation 32 bits of information are needed, so we just 'lend' last 4 bits from 5th byte. This 32 bit combination gave the Unix timestamp of the chat message
6. 0xE8 0x03 – marker for the beginning of the sender username field. The field ends with zero byte 0x00
7. 1.0xEC 0x03 – marker for the beginning of the sender screen name field. The field ends with zero byte 0x00
8. 1.0xFC 0x03 – marker for the beginning of the message field. The field ends with zero byte 0x00

*/


#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>

jmp_buf nul;

const unsigned char sections[][6] = {
 /* start_rec  */  { 0x6C, 0x33, 0x33, 0x6C, 0x0 },
 /* // skip 14 */
 /* start_chat */  { 0xE0, 0x03, 0x0 },
 /* start_mem1 */  { 0x23, 0x00 },
 /* mem_sep    */  { 0x2F, 0x00 }, /* '/' */
 /* start_mem2 */  { 0x34, 0x00 },
 /* end_membs  */  { 0x3B, 0x00 },
 /* start_msg  */  { 0xFC, 0x03, 0x0 } /* 0x0 term */
};


void die(const char *s)
{
	perror(s);
	exit(1);
}

void fget(FILE *f, char **buf, size_t *pos)
{
#define SINGLE 512
	size_t len = SINGLE;
	char *base = (char*) malloc(len);
	char *tmp;

	if(!base)
		die("malloc()");

	tmp = base;

	while(fread(tmp, sizeof(char), SINGLE, f) > 0){
		base = (char*) realloc(base, len += SINGLE);
		if(!base)
			die("malloc()");
		tmp = base + len - SINGLE;
	}

	if(ferror(f))
		die("read()");

	*buf = base;
	*pos = len - SINGLE;
}

char *memmem2(char *start, char *data, size_t len, int n)
{
	char *pos = (char*) memmem(start, len - (start - data),
				sections[n],
				strlen((const char *)sections[n]));

	if(!pos)
		longjmp(nul, 1);

	return pos + strlen((const char *)sections[n]);
}

int fprocess(FILE *f, const char *fname)
{
	int ret = 0;
	char *data;
	char *ptr;
	char *save;
	size_t len;

	fget(f, &data, &len);

	ptr = data;

	if(!setjmp(nul))
		do{
			char *mem1, *mem2, *msg;

			ptr = memmem2(ptr, data, len, 0);
			ptr += 14;
			ptr = memmem2(ptr, data, len, 1);
			ptr = memmem2(ptr, data, len, 2);
			save = ptr;
			ptr = memmem2(ptr, data, len, 3);
			ptr[-1] = '\0';
			mem1 = save;

			ptr++; /* = memmem2(ptr - 1, data, len, 4); */
			save = ptr;
			ptr = memmem2(ptr, data, len, 5);
			ptr[-1] = '\0';
			mem2 = save;

			ptr = memmem2(ptr, data, len, 6);
			save = ptr;
			ptr = (char*) memchr(ptr, 0, len - (ptr - data));
			msg = save;

			printf("%s: %s <-> %s: %s\n", fname, mem1, mem2, msg);
		}while(1);
	else
		ret = 1;

	free(data);

	return ret;
}

int process(const char *fname)
{
	FILE *f = fopen(fname, "r");
	int ret;

	if(!f){
		fprintf(stderr, "open: \"%s\": %s\n", fname, strerror(errno));
		return 1;
	}

	ret = fprocess(f, fname);
	fclose(f);

	return ret;
}

int main(int argc, char **argv)
{
	int ret = 0;

	if(argc > 1){
		int i;
		for(i = 1; i < argc; i++)
			ret |= process(argv[i]);
	}else
		ret = fprocess(stdin, "-");

	return ret;
}


