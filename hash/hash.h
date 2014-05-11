#ifndef _HAPROXY_HASH_H
#define _HAPROXY_HASH_H
#include <sys/types.h>


#define HASH_MAX_SIZE 1000

#define hap_align(d, a) (((d) + (a - 1)) & ~(a - 1))
#define hap_align_ptr(p, a) \
	(u_char *) (((int *) (p) + ((int *) a - 1)) & ~((int *) a - 1))

typedef struct {
	size_t len;
	u_char *data;
} str_t;

typedef struct{
	void 				*value;  // NULL when elt is a yayuansu in buckets
	unsigned int 	 	 vlen;	 // len of value
	u_short		 	 	 len;
	u_char				*name;
} hash_elt_t;

typedef struct{
	hash_elt_t     	   **buckets;
	unsigned int	 	 size;
} hash_t;

typedef struct {
	str_t			 	 key;
	unsigned int	     key_hash;
	unsigned int 		 vlen;
	void 				*value;
} hash_key_t;


#define hap_hash(key, c)    ((unsigned int) key * 31 + c)
#define hap_tolower(c)		(u_char) ((c >= 'A' && c <= 'Z') ? (c | 0x20) : c)
#define hap_toupper(c)		(u_char) ((c >= 'a' && c <= 'z') ? (c & ~0x20) : c)


//void hap_strlow(u_char *dst, u_char *src, size_t n);

#endif /*_HAPROXY_HASH_H*/
