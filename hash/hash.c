#include <hash.h>
#include <stdio.h>

#include <common/logging.h>

unsigned int hap_cacheline_size = 32;

void hap_strlow(u_char *dst, u_char *src, size_t n)
{
	while (n) {
		*dst = hap_tolower(*src);
		dst++;
		src++;
		n--;
	}
}

void *hap_hash_find(hash_t *hash, unsigned int key, u_char *name, size_t len)
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
		elt++;
		continue;
	}
	return NULL;
}

hash_t *hap_hash_init(hash_key_t *names, int nelts)
{
	logging(TRACE, "hap_hash_init");
	char 			*elts;
	size_t			 len;
	short           *test;
	int 			 i, n, key, size, bucket_size = 4, start = nelts/bucket_size;
	hash_elt_t		*elt, **buckets;
	hash_t 			*hash;

	test = (u_short *) malloc (HASH_MAX_SIZE * sizeof(u_short));

	for (size = start; size < HASH_MAX_SIZE; size++) {

		memset(test, 0, size * sizeof(u_short));
		/*start from new size*/
		for (n = 0; n < nelts; n++) {
			if (names[n].key.data == NULL) {
				continue;
			}
			key = names[n].key_hash % size;

			/*get every bucket real size*/
			test[key] = (u_short) (test[key] + 1);
			/*if the bucket real size is too large, then size++*/
			if (test[key] > (u_short) bucket_size) {
				goto next;
			}
		}
		goto found;
	next:
		continue;
	}

	return NULL;
found:
	logging(TRACE, "[hap_hash_init][size:%d]", size);

	/*every bucket has a yayuansu*/
	for (i = 0; i < size; i++) {
		test[i] = sizeof(void *);
	}

	/*visit every name to cal the bucket size*/
	for (n = 0; n < nelts; n++) {
		if (names[n].key.data == NULL) {
			continue;
		}
		key = names[n].key_hash % size;
		test[key] = (u_short) (test[key] + sizeof(hash_elt_t));
	}

	len = 0;

	/*sum up all bucket size (first to align size) to len*/
	for (i = 0; i < size; i++) {
		if (test[i] == sizeof(void *)) {
			continue;
		}
		test[i] = (u_short) (hap_align(test[i], hap_cacheline_size));
		len += test[i];
		logging(TRACE, "test[i]:%d, %d", test[i], sizeof(hash_elt_t));
	}

	hash = malloc(sizeof(hash));
	if (hash == NULL) {
		goto error_hash;
	}

	buckets = malloc(size * sizeof(hash_elt_t *));
	
	if (buckets == NULL) {
		goto error_buckets;
	}

	/*malloc bucket space according to len(still need to align len)*/
	elts = malloc(len + hap_cacheline_size);
	memset(elts, 0, len + hap_cacheline_size);
	if (elts == NULL) {
		goto error_elts;
	}

	/*start from align address*/
	//elts = hap_align_ptr(elts, hap_cacheline_size);

	/*assign space to every bucket*/
	for (i = 0; i < size; i++) {
		if (test[i] == sizeof(void *)) {
			continue;
		}
		buckets[i] = (hash_elt_t *) elts;
		elts += test[i];
		logging(TRACE, "test[i]:%d", test[i]);
	}

	/*stuff data*/
	for (i = 0; i < size; i++) {
		test[i] = 0;
	}

	for (n = 0; n < nelts; n++) {
		if (names[n].key.data == NULL) {
			continue;
		}
		key = names[n].key_hash % size;
		elt = (hash_elt_t *) ((u_char *) buckets[key] + test[key]);

		elt->value = names[n].value;
		elt->len = (u_short) names[n].key.len;
		//memcpy(elt->name, names[n].key.data, strlen(names[n].key.data));
		//elt->name[names[n].key.len] = '\0';
		elt->name = names[n].key.data;
		logging(TRACE, "set:%s %d\n   %s %d", names[n].key.data, names[n].key.len, 
			buckets[key]->name, buckets[key]->name - (int)(buckets[key]->name + 1));
		
		/*update the location of next elt in bucket*/
		test[key] = (u_short) (test[key] + sizeof(hash_key_t));
	}

	/*stuff the end node*/
	for (i = 0; i < size; i++) {
		if (buckets[i] == NULL) {
			continue;
		}
		elt = (hash_elt_t *)((u_char *)buckets[i] + test[i]);
		elt->value = NULL;
	}
	free(test);
	hash->buckets = buckets;
	hash->size = size;

	return hash;

error_hash:
	free(test);
error_buckets:
	free(hash);
error_elts:
	free(buckets);
	return NULL;
}

unsigned int hap_hash_key(u_char *data, size_t len)
{
	unsigned int i, key;

	key = 0;

	for (i = 0; i < len; i++) {
		key = hap_hash(key, data[i]);
	}
	return key;
}

unsigned int hap_hash_key_lc(u_char *data, size_t len)
{
	unsigned int i, key;

	key = 0;

	for (i = 0; i < len; i++) {
		key = hap_hash(key, hap_tolower(data[i]));
	}
	return key;
}

unsigned int hap_hash_strlow(u_char *dst, u_char *src, size_t n)
{
	unsigned int key;

	key = 0;

	while (n--) {
		*dst = hap_tolower(*src);
		key = hap_hash(key, *dst);
		dst++;
		src++;
	}
	return key;
}
