#ifndef _HAPROXY_HASH_H
#define _HAPROXY_HASH_H
#include <sys/types.h>

typedef struct {
	size_t len;
	u_char *data;
} str_t;

typedef struct{
	void 		*value;
	u_short		 len;
	u_char		 name[1];
} hash_elt_t;

typedef struct{
	hash_elt_t     **buckets;
	unsigned int	 size;
} hash_t;

typedef struct {
	str_t			 	 key;
	unsigned int	     key_hash;
	void 				*value;
} engx_hash_key_t;


#endif /*_HAPROXY_HASH_H*/
