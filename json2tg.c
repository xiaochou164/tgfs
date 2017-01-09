#define _XOPEN_SOURCE
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "json2tg.h"
#include "jsmn/jsmn.h"

static time_t json_datetime_to_posix(char* json) {
	time_t result = 0;
	char time[20];
	strncpy(time, json, 19);
	time[19] = 0;
	struct tm *tm = (struct tm*)malloc(sizeof(struct tm));
	if(strptime(time, "%Y-%m-%d %H:%M:%S", tm)) {
		result = mktime(tm);
	}
	free(tm);
	return result;
}

static void json_parse_peer(char* json, jsmntok_t* tokens, size_t* pos, tg_peer_t* peer) {	
	size_t r = *pos + 1, i = 0;
	
	while(i < tokens[*pos].size) {	
		size_t token_size = tokens[r].end - tokens[r].start;
		size_t inner_size = tokens[r+1].end - tokens[r+1].start;
		
		if(strncmp("admin", json + tokens[r].start, token_size) == 0) {
			r += 2*tokens[r+1].size + 2;
			i++;
			continue;
		}
		if(strncmp("peer_type", json + tokens[r].start, token_size) == 0) {
			switch(json[tokens[r+1].start + 3]) {
				case 'r':
					peer->peer_type	= TG_USER;
					break;
				case 't':
					peer->peer_type	= TG_CHAT;
					break;
				case 'n':
					peer->peer_type	= TG_CHANNEL;
					break;
				default:
					peer->peer_type	= TG_UNKNOWN;
					break;
				
			} 
			i++;
			r += 2;
			continue;
		}
		if(strncmp("print_name", json + tokens[r].start, token_size) == 0) {
			peer->print_name = (char*)malloc((inner_size + 1) * sizeof(char));
			strncpy(peer->print_name, json + tokens[r+1].start, inner_size);
			peer->print_name[inner_size] = 0;
			peer->print_name_hash = tg_string_hash(peer->print_name);
			i++;
			r += 2;
			continue;
		}
		if(strncmp("when", json + tokens[r].start, token_size) == 0) {
			peer->last_seen = json_datetime_to_posix(json + tokens[r+1].start);
			i++;
			r += 2;
			continue;
		}
		if(strncmp("id", json + tokens[r].start, token_size) == 0) {
			strncpy(peer->id, json + tokens[r+1].start, inner_size);
			i++;
			r += 2;
			continue;
		}
		if(strncmp("peer_id", json + tokens[r].start, token_size) == 0) {
			peer->peer_id = strtol(json  + tokens[r+1].start, NULL, 10);
			i++;
			r += 2;
			continue;
		}
		
		
		i++;
		r += 2;
	}
	*pos = r - 1;
}

static void json_parse_media(char* json, jsmntok_t* tokens, size_t pos, tg_msg_t* msg) {
	char a[10000];
	strncpy(a, json + tokens[pos].start, tokens[pos].end - tokens[pos].start);
	a[tokens[pos].end - tokens[pos].start] = 0;
	printf("json: %s\n", a);
	
	size_t i = 0, r = pos + 1;
	while(i < tokens[pos].size) {
		size_t token_size = tokens[r].end - tokens[r].start;
		size_t inner_size = tokens[r + 1].end - tokens[r + 1].start;
		
		char m[1000];
		strncpy(m, json + tokens[r].start, token_size);
		m[token_size] = 0;
		printf("token: %s = ", m);
		strncpy(m, json + tokens[r+1].start, inner_size);
		m[inner_size] = 0;
		printf("%s\n", m);
		
		if(strncmp(json + tokens[r].start, "caption", token_size) == 0) {
			msg->caption = (char*)malloc((inner_size + 1) * sizeof(char));
			strncpy(msg->caption, json + tokens[r + 1].start, inner_size);
			msg->caption[inner_size] = 0;
			msg->caption_hash = tg_string_hash(msg->caption);
			i++;
			r += 2;
			continue; 
		}
		if(strncmp(json + tokens[r].start, "size", token_size) == 0) {
			char a[100];
			strncpy(a, json + tokens[r + 1].start, inner_size);
			a[inner_size] = 0;
			msg->size = strtol(a, NULL, 10);
			i++;
			r += 2;
			continue; 
		}
		
		i++;
		r += 2;
	}
}

static void json_parse_msg(char* json, jsmntok_t* tokens, size_t* pos, tg_msg_t* msg) {
	size_t r = *pos + 1, i = 0;
	
	msg->caption = NULL;
	while(i < tokens[*pos].size) {	
		size_t token_size = tokens[r].end - tokens[r].start;
		size_t inner_size = tokens[r+1].end - tokens[r+1].start;

		if(strncmp("media", json + tokens[r].start, token_size) == 0) {
			json_parse_media(json, tokens, r + 1, msg);
			r += 2*tokens[r+1].size + 2;
			i++;
			continue;
		}
		
		if(strncmp("fwd_from", json + tokens[r].start, token_size) == 0 ||
		   strncmp("from", json + tokens[r].start, token_size) == 0 ||
		   strncmp("to", json + tokens[r].start, token_size) == 0) {\
			r += 2*tokens[r+1].size + 2;
			i++;
			continue;
		}
		if(strncmp("id", json + tokens[r].start, token_size) == 0) {
			strncpy(msg->id, json + tokens[r+1].start, inner_size);
			msg->id[inner_size] = 0;
			i++;
			r += 2;
			continue;
		}
		if(strncmp("date", json + tokens[r].start, token_size) == 0) {
			char* buff = (char*)malloc((inner_size + 1) * sizeof(char));
			strncpy(buff, json + tokens[r+1].start, inner_size);
			buff[inner_size] = 0;
			msg->timestamp = strtol(buff, NULL, 10);
			free(buff);
			i++;
			r += 2;
			continue;
		}
		
		i++;
		r += 2;
	}
	*pos = r - 1;
	tg_print_msg_t(msg);
}

int json_parse_dialog_list(char* json, size_t size, tg_peer_t** peers, size_t* peers_count) {
	jsmn_parser parser;
	jsmntok_t *tokens;
	jsmn_init(&parser);
	
	size_t tokens_count = jsmn_parse(&parser, json, size, NULL, 0);

	jsmn_init(&parser);
	tokens = (jsmntok_t*)malloc(tokens_count*sizeof(jsmntok_t));
	jsmn_parse(&parser, json, size, tokens, tokens_count);
	
#ifdef DEBUG
	printf("json_parse_dialog_list(): Dialog count: %i\n", tokens[0].size);
#endif
	
	if(*peers == NULL) {
		*peers = (tg_peer_t*)malloc(tokens[0].size * sizeof(tg_peer_t));
	} else if(tokens[0].size != *peers_count) {
		*peers = (tg_peer_t*)realloc(*peers, tokens[0].size * sizeof(tg_peer_t));
	}
	
	for(size_t i = 1; i < tokens_count; i++) {
		json_parse_peer(json, tokens, &i, &(*peers)[*peers_count]);
		(*peers_count)++;
	}
	return 0;
}

int json_parse_messages(char* json, size_t size, tg_peer_t* peer, int media_type) {
	jsmn_parser parser;
	jsmntok_t *tokens;
	jsmn_init(&parser);
	
	size_t tokens_count = jsmn_parse(&parser, json, size, NULL, 0);

	jsmn_init(&parser);
	tokens = (jsmntok_t*)malloc(tokens_count*sizeof(jsmntok_t));
	jsmn_parse(&parser, json, size, tokens, tokens_count);
#ifdef DEBUG
	printf("json_parse_messages(): Messages count: %i\n", tokens[0].size);
#endif
	size_t message_count;
	tg_msg_t* messages;
	tg_get_msg_array_by_media_type(&messages, &message_count, peer, media_type);
	
	if(message_count > 0) {
		messages = (tg_msg_t*)realloc(messages, (message_count + tokens_count) * sizeof(tg_msg_t));
	} else {
		messages = (tg_msg_t*)malloc(tokens_count * sizeof(tg_msg_t));
	}
	
	
	for(size_t i = 1; i < tokens_count; i++) {
		json_parse_msg(json, tokens, &i, &messages[message_count]);
		message_count++;
	}
	
	tg_set_msg_array_by_media_type(messages, message_count, peer, media_type);
	return 0;
}

char* json_parse_filelink(char* json) {
	/*char* name;
	jsmn_parser parser;
	jsmntok_t* tokens = (jsmntok_t*)malloc(10 * sizeof(jsmntok_t));
	jsmn_init(&parser);
	
	name = (char*)malloc();*/
	
	return NULL;
}
