#ifndef _HAPROXY_HASH_H
#define _HAPROXY_HASH_H
typedef struct{
	void 		*value;
	u_short		 len;
	u_char		 name[1];
} hash_elt_t;
typedef struct{
	hash_elt_t     **buckets;
	unsigned int	 size;
} hash_t;

#endif /*_HAPROXY_HASH_H*/
