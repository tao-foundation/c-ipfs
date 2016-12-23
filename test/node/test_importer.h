#include <stdio.h>

#include "ipfs/importer/importer.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/merkledag/merkledag.h"
#include "mh/hashes.h"
#include "mh/multihash.h"
#include "libp2p/crypto/encoding/base58.h"

/***
 * Helper to create a test file in the OS
 */
int create_file(const char* fileName, unsigned char* bytes, size_t num_bytes) {
	FILE* file = fopen(fileName, "wb");
	fwrite(bytes, num_bytes, 1, file);
	fclose(file);
	return 1;
}

int create_bytes(unsigned char* buffer, size_t num_bytes) {
	int counter = 0;

	for(int i = 0; i < num_bytes; i++) {
		buffer[i] = counter++;
		if (counter > 15)
			counter = 0;
	}
	return 1;
}

int test_import_large_file() {
	size_t bytes_size = 1000000; //1mb
	unsigned char file_bytes[bytes_size];
	const char* fileName = "/tmp/test_import_large.tmp";

	// create the necessary file
	create_bytes(file_bytes, bytes_size);
	create_file(fileName, file_bytes, bytes_size);

	// get the repo
	drop_and_build_repository("/tmp/.ipfs");
	struct FSRepo* fs_repo;
	ipfs_repo_fsrepo_new("/tmp/.ipfs", NULL, &fs_repo);
	ipfs_repo_fsrepo_open(fs_repo);

	// write to ipfs
	struct Node* write_node;
	if (ipfs_import_file(fileName, &write_node, fs_repo) == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// cid should be the same each time
	unsigned char cid_test[10] = { 0x2c ,0x8e ,0x20 ,0x1b, 0xc7, 0xcc, 0x4d, 0x8f, 0x7e, 0x77 };

	/*
	for (int i = 0; i < 10; i++) {
		printf(" %02x ", write_node->hash[i]);
	}
	printf("\n");
	*/

	for(int i = 0; i < 10; i++) {
		if (write_node->hash[i] != cid_test[i]) {
			printf("Hashes should be the same each time, and do not match at position %d, should be %02x but is %02x\n", i, cid_test[i], write_node->hash[i]);
			ipfs_repo_fsrepo_free(fs_repo);
			ipfs_node_free(write_node);
			return 0;
		}
	}

	// make sure all went okay
	struct Node* read_node;
	if (ipfs_merkledag_get(write_node->hash, write_node->hash_size, &read_node, fs_repo) == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		return 0;
	}

	// the second block should be there
	struct Node* read_node2;
	if (ipfs_merkledag_get(read_node->head_link->hash, read_node->head_link->hash_size, &read_node2, fs_repo) == 0) {
		printf("Unable to find the linked node.\n");
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		return 0;
	}

	ipfs_node_free(read_node2);

	// compare data
	if (write_node->data_size != read_node->data_size) {
		printf("Data size of nodes are not equal. Should be %lu but are %lu\n", write_node->data_size, read_node->data_size);
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		ipfs_node_free(read_node);
		return 0;
	}

	for(int i = 0; i < write_node->data_size; i++) {
		if (write_node->data[i] != read_node->data[i]) {
			printf("Data within node is different at position %d. The value should be %02x, but was %02x.\n", i, write_node->data[i], read_node->data[i]);
			ipfs_repo_fsrepo_free(fs_repo);
			ipfs_node_free(write_node);
			ipfs_node_free(read_node);
			return 0;
		}
	}

	// convert cid to multihash
	size_t base58_size = 55;
	unsigned char base58[base58_size];
	if ( ipfs_cid_hash_to_base58(read_node->hash, read_node->hash_size, base58, base58_size) == 0) {
		printf("Unable to convert cid to multihash\n");
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		ipfs_node_free(read_node);
		return 0;
	}

	// attempt to write file
	if (ipfs_exporter_to_file(base58, "/tmp/test_import_large_file.rsl", fs_repo) == 0) {
		printf("Unable to write file.\n");
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		ipfs_node_free(read_node);
		return 0;
	}

	// compare original with new
	size_t new_file_size = os_utils_file_size("/tmp/test_import_large_file.rsl");
	if (new_file_size != bytes_size) {
		printf("File sizes are different. Should be %lu but the new one is %lu\n", bytes_size, new_file_size);
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		ipfs_node_free(read_node);
		return 0;
	}

	FILE* f1 = fopen("/tmp/test_import_large.tmp", "rb");
	FILE* f2 = fopen("/tmp/test_import_large_file.rsl", "rb");

	size_t bytes_read1 = 1;
	size_t bytes_read2 = 1;
	unsigned char buf1[100];
	unsigned char buf2[100];
	// compare bytes of files
	while (bytes_read1 != 0 && bytes_read2 != 0) {
		bytes_read1 = fread(buf1, 1, 100, f1);
		bytes_read2 = fread(buf2, 1, 100, f2);
		if (bytes_read1 != bytes_read2) {
			printf("Error reading files for comparison. Read %lu bytes of file 1, but %lu bytes of file 2\n", bytes_read1, bytes_read2);
			ipfs_repo_fsrepo_free(fs_repo);
			ipfs_node_free(write_node);
			ipfs_node_free(read_node);
			fclose(f1);
			fclose(f2);
			return 0;
		}
		if (memcmp(buf1, buf2, bytes_read1) != 0) {
			printf("The bytes between the files are different\n");
			ipfs_repo_fsrepo_free(fs_repo);
			ipfs_node_free(write_node);
			ipfs_node_free(read_node);
			fclose(f1);
			fclose(f2);
			return 0;
		}
	}

	ipfs_repo_fsrepo_free(fs_repo);
	ipfs_node_free(write_node);
	ipfs_node_free(read_node);

	return 1;

}

int test_import_small_file() {
	size_t bytes_size = 1000;
	unsigned char file_bytes[bytes_size];
	const char* fileName = "/tmp/test_import_small.tmp";

	// create the necessary file
	create_bytes(file_bytes, bytes_size);
	create_file(fileName, file_bytes, bytes_size);

	// get the repo
	drop_and_build_repository("/tmp/.ipfs");
	struct FSRepo* fs_repo;
	ipfs_repo_fsrepo_new("/tmp/.ipfs", NULL, &fs_repo);
	ipfs_repo_fsrepo_open(fs_repo);

	// write to ipfs
	struct Node* write_node;
	if (ipfs_import_file(fileName, &write_node, fs_repo) == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		return 0;
	}

	// cid should be the same each time
	unsigned char cid_test[10] = { 0x94, 0x4f, 0x39, 0xa0, 0x33, 0x5d, 0x7f, 0xf2, 0xcd, 0x66 };

	/*
	for (int i = 0; i < 10; i++) {
		printf("%02x\n", write_node->hash[i]);
	}
	*/

	for(int i = 0; i < 10; i++) {
		if (write_node->hash[i] != cid_test[i]) {
			printf("Hashes do not match at position %d, should be %02x but is %02x\n", i, cid_test[i], write_node->hash[i]);
			ipfs_repo_fsrepo_free(fs_repo);
			ipfs_node_free(write_node);
			return 0;
		}
	}

	// make sure all went okay
	struct Node* read_node;
	if (ipfs_merkledag_get(write_node->hash, write_node->hash_size, &read_node, fs_repo) == 0) {
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		return 0;
	}

	// compare data
	if (write_node->data_size != bytes_size || write_node->data_size != read_node->data_size) {
		printf("Data size of nodes are not equal or are incorrect. Should be %lu but are %lu\n", write_node->data_size, read_node->data_size);
		ipfs_repo_fsrepo_free(fs_repo);
		ipfs_node_free(write_node);
		ipfs_node_free(read_node);
		return 0;
	}

	for(int i = 0; i < bytes_size; i++) {
		if (write_node->data[i] != read_node->data[i]) {
			printf("Data within node is different at position %d\n", i);
			ipfs_repo_fsrepo_free(fs_repo);
			ipfs_node_free(write_node);
			ipfs_node_free(read_node);
			return 0;
		}
	}

	ipfs_repo_fsrepo_free(fs_repo);
	ipfs_node_free(write_node);
	ipfs_node_free(read_node);

	return 1;
}
