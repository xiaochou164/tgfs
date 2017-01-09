#define FUSE_USE_VERSION 26

#define _XOPEN_SOURCE 600

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "socket_tasks.h"
#include "jsmn/jsmn.h"
#include "tg_data.h"

extern tg_data_t tg;

int tgfs_fd = -1;
char tgfs_buff[1000];

static int tgfs_getattr(const char *path, struct stat *stbuf)
{
	printf("getattr(): %s\n", path);
	memset(stbuf, 0, sizeof(struct stat));
	
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	
	printf("buff: %s\n", tgfs_buff);
	
	if(strcmp(path, tgfs_buff) == 0) {
		printf("Ok\n");
		stbuf->st_mode = S_IFREG | 0200;
		stbuf->st_nlink = 1;
		return 0;
	}
	
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0500;
		stbuf->st_nlink = 2;
		return 0;
	}
	
	size_t c[10];
	size_t n = 0;
	for(size_t i = 0; i < strlen(path); i++) {
		if(path[i] == '/') {
			c[n] = i + 1;
			n++;
		}
	}
	c[n] = strlen(path) + 1;
	
	tg_peer_t* peer = tg_find_peer_by_name(path + 1, c[1] - c[0] - 1);
	
	if(peer == NULL) {
		return -ENOENT;
	}
	
	if(n == 1) {
		/* Root dir item */
		if(peer->peer_type != TG_CHANNEL) {
			stbuf->st_mode = S_IFDIR | 0700;
		} else {
			stbuf->st_mode = S_IFDIR | 0500;
		}
		stbuf->st_nlink = 0;
		stbuf->st_atime = peer->last_seen;
		stbuf->st_ctime = peer->last_seen;
		stbuf->st_mtime = peer->last_seen;
		stbuf->st_size = peer->photo_size + 
							peer->audio_size + 
							peer->voice_size + 
							peer->documents_size + 
							peer->gif_size + 
							peer->video_size;
		stbuf->st_ino = peer->peer_id;
		return 0;
	}
	
	if(n == 2) {
		if(path[c[1]] == 'P' || path[c[1]] == 'A') {
			stbuf->st_mode = S_IFDIR | 0500;
			stbuf->st_nlink = 2;
			return 0;
		}
		if(path[c[1]] == 'i' && tgfs_fd	>= 0) {
			stbuf->st_mode = S_IFREG | 0200;
			stbuf->st_nlink = 1;
			return 0;
		}
	}
	
	if(n == 3) {
		printf("getattr 3\n");
		size_t message_count;
		tg_msg_t* messages;
		int media_type = TG_MEDIA_AUDIO;
		uint32_t hash = tg_string_hash(path + c[2]);
		tg_get_msg_array_by_media_type(&messages, &message_count, peer, media_type);
		for(size_t i = 0; i < message_count; i++) {
			if(messages[i].caption_hash == hash) {
				stbuf->st_mode = S_IFREG | 0400;
				stbuf->st_nlink = 1;
				stbuf->st_mtime = messages[i].timestamp;
				stbuf->st_size = messages[i].size;
				return 0;
			}
		}
	}
	return -ENOENT;
}

static int tgfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") == 0) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		for(size_t i = 0; i < tg.peers_count; i++) {
			if(strlen(tg.peers[i].print_name)) {
				filler(buf, tg.peers[i].print_name, NULL, 0); 
			}
		}
		return 0;
	}
	
	size_t c[10];
	size_t n = 0;
	for(size_t i = 0; i < strlen(path); i++) {
		if(path[i] == '/') {
			c[n] = i + 1;
			n++;
		}
	}
	c[n] = strlen(path) + 1;
	
	if(n == 1) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		filler(buf, "Photo", NULL, 0);
		filler(buf, "Audio", NULL, 0);
		return 0;
	}	
	tg_peer_t* peer = tg_find_peer_by_name(path + 1, c[1] - c[0] - 1);
	if(n == 2) {
		/*if(path[c[1]] == 'P') {
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
			tg_search_msg(peer, TG_MEDIA_PHOTO, "");
			printf("GOT\n");
			for(size_t i = 0; i < peer->message_count; i++) {	
				char a[255];
				if(peer->messages[i].caption) {
					sprintf(a, "%s.jpg", peer->messages[i].caption);
				} else {
					sprintf(a, "untitled-%4.4li.jpg", i);
					peer->messages[i].caption = (char*)malloc(sizeof(a));
					strcpy(peer->messages[i].caption, a);
				}
				filler(buf, a, NULL, 0);
			}
			return 0;
		}*/
		if(path[c[1]] == 'A') {
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
			tg_search_msg(peer, TG_MEDIA_AUDIO, "");
			for(size_t i = 0; i < peer->audio_size; i++) {	
				filler(buf, peer->audio[i].caption, NULL, 0);
			}
			return 0;
		}
	}
	
	return -ENOENT;
}

static int tgfs_open(const char *path, struct fuse_file_info *fi)
{
	printf("open(): %s\n", path);
	
	if(strcmp(path, tgfs_buff) == 0) {
		return 0;
	}
	
	size_t c[10];
	size_t n = 0;
	for(size_t i = 0; i < strlen(path); i++) {
		if(path[i] == '/') {
			c[n] = i + 1;
			n++;
		}
	}
	c[n] = strlen(path) + 1;
	
	tg_peer_t* peer = tg_find_peer_by_name(path + 1, c[1] - c[0] - 1);
	if(peer == NULL) {
		return -ENOENT;
	}
	if(n == 3) {
		
		char name[255];
		tg_download_file(name, peer, path + c[2]);
		printf("Filename: %s\n", name);
		
		/*strcpy(name, path + c[2]);
		printf("Peer: %s\n", peer->print_name);
		
		for(size_t i = 0; i < peer->message_count; i++) {
			if(strcmp(peer->messages[i].caption, name) == 0) {
				sprintf(name, "load_document %s\n", peer->messages[i].id);
				printf("string: %s", name);
				socket_send_string(name, strlen(name));
				char* json;
				size_t len;
				socket_read_data(&json, &len);
				printf("Answer: %s\n", json);
				
				
				jsmn_parse(&parser, json, len, tokens, 10);
				printf("tokens: %i\n", tokens[0].size); 
				size_t size = tokens[2].end - tokens[2].start;
				strncpy(name, json + tokens[2].start, size);
				name[size] = 0;
				printf("file: %s\n", name);
				tgfs_fd = open(name, O_RDONLY);
				return tgfs_fd > 0 ? 0 : -ENOENT;
			}
		}*/
	}
	
	return -ENOENT;
	
	tgfs_fd = open("/tmp/tgfs_tmp.jpg",  O_WRONLY | O_CREAT);
	printf(" = %i\n", tgfs_fd);
	return 0;
	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int tgfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	printf("read(): %s\n", path);
	(void) fi;
	return pread(tgfs_fd, buf, size, offset);
}

static int tgfs_create(const char *path, mode_t mode,
					struct fuse_file_info *fi) {
						
	(void) fi;
	printf("create(): %s\n", path);
	strcpy(tgfs_buff, path);
	remove("/tmp/tgfs_tmp.jpg");
	tgfs_fd = open("/tmp/tgfs_tmp.jpg",  O_WRONLY | O_CREAT);
	printf(" = %i\n", tgfs_fd);
	return 0;
}

static int tgfs_write(const char *path, const char *buf, size_t size, off_t offset, 
			struct fuse_file_info *fi) {
	(void) fi;
	int res = write(tgfs_fd, buf, size);
	printf("write: %s #%i(%li -> %li) = %i\n", path, tgfs_fd, size, offset, res);
	
	return res;
}

int tgfs_release(const char *path, struct fuse_file_info *fi) {
	printf("release:  %s\n", path);
	close(tgfs_fd);
	tgfs_fd = -1;
	
	//size_t c[10];
	size_t n = 0;
	for(size_t i = 0; i < strlen(path); i++) {
		if(path[i] == '/') {
			//c[n] = i + 1;
			n++;
		}
	}
	//c[n] = strlen(path) + 1;
	
	if(n == 2) {
		
		char buff[1000];
		char req[1000];
		size_t s = 1;
		while(path[s] != '/')
			s++;
		strncpy(buff, path + 1, s - 1);
		buff[s] = 0;
		sprintf(req, "post_photo %s %s %s\n", buff, "/tmp/tgfs_tmp.jpg", path + s + 1);
		printf("req: %s\n", req);
		//socket_send_string(req, strlen(req));
	}
	return 0;
}

int tgfs_truncate(const char *path, off_t newsize) {
	printf("truncate:  %s\n", path);
	truncate("/tmp/tgfs_tmp.jpg", newsize);
	return 0;
}

static struct fuse_operations tgfs_oper = {
	.getattr	= tgfs_getattr,
	.readdir	= tgfs_readdir,
	.open		= tgfs_open,
	.read		= tgfs_read,
	.write		= tgfs_write,
	.create     = tgfs_create,
	.truncate   = tgfs_truncate,
	.release	= tgfs_release
};

int main(int argc, char *argv[]) {
	socket_init();
	tg_init();
	return fuse_main(argc, argv, &tgfs_oper, NULL);
}
