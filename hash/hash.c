#include <hash.h>

void hash_find(hash_t *hash, unsigned int key, u_char *name, size_t len)
{
	unsigned int 	i;
	hash_elt_t     *elt;

	elt = hash->buckets[key % hash->size];	
	if (elt == NULL) {
		return NULL;
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
		elt = (hash_elt_t *) align_ptr();
		continue;
	}
	return NULL:
}


































