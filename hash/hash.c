#include <hash.h>
#include <stdio.h>

void hash_find(hash_t *hash, unsigned int key, u_char *name, size_t len)
{
	unsigned int 	i;
	hash_elt_t     *elt;

	elt = hash->buckets[key % hash->size];	
	if (elt == NULL) {
		return;
	}

	while (elt->value) {
		if (len != (size_t) elt->len) {
			goto next;		
		}
		
		for (i = 0; i < len; i++) {
			if (name[i] != elt->name[i]) {
				goto next;
			}		
		}

		return elt->value;
	next:
	//	elt = (hash_elt_t *) align_ptr();
		continue;
	}
	return ;
}
/*
int hash_init(hash_init_t *hinit, hash_key_t *names, int nelts)
{
	char 			*elts;
	size_t			 len;
	short           *test;
	int 			 i, n, key, size, start, bucket_size;
	hash_elt_t		*elt, **buckets;


}*/


































