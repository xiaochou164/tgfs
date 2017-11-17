#define _DEFAULT_SOURCE

#include "tg.h"
#include "auth.h"

#include <unistd.h>
#include <pthread.h>
#include <sqlite3.h>
#include <event2/event.h>
#include <event2/bufferevent.h>

extern struct tgl_update_callback upd_cb;
extern int ready;
struct tgl_state *TLS;

pthread_t t;
sqlite3 *tg_storage;

static void* _tg_tgl_init(void*);
void tg_tgl_init() {
	pthread_create(&t, NULL, _tg_tgl_init, NULL);	
}
void tg_tgl_destruct() {
	sqlite3_close(tg_storage);
}

static void _tg_storage_init() {
	int rc = sqlite3_open("/tmp/tg", &tg_storage);
	char* err_msg = NULL;
	if(rc != SQLITE_OK) {
		fprintf(stderr, "Unable to open database: %s\n", sqlite3_errmsg(tg_storage));
		sqlite3_close(tg_storage);
		exit(1);
	}
	char *tables = "create table peers(type int, id int primary key, access_hash bigint);";
	rc = sqlite3_exec(tg_storage, tables, 0, 0, &err_msg);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "Failed to create tables\nSQL Error: %s\n", err_msg);
		exit(1);
	}
}

void tg_storage_peer_add(tgl_peer_id_t p) {
	char* insert;
	char* err_msg = NULL;
	tasprintf(&insert, "insert into peers(type, id, access_hash) values(%i, %i, %lli);", p.peer_type, p.peer_id, p.access_hash);
	int rc = sqlite3_exec(tg_storage, insert, 0, 0, &err_msg);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "Failed to insert peer: %s\n", err_msg);
	}
	tfree_str(insert);
}

static tgl_peer_id_t convert_id(sqlite3_stmt* res) {
	tgl_peer_id_t id;
	id.peer_type = sqlite3_column_int(res, 0);
	id.peer_id = sqlite3_column_int(res, 1);
	id.access_hash = sqlite3_column_int64(res, 2);
	return id;
}

void tg_storage_peer_enumerate(void *buf, fuse_fill_dir_t filler) {
	char* select = "select * from peers;";
	char* err_msg = NULL;
	sqlite3_stmt* res;
	int rc = sqlite3_prepare_v2(tg_storage, select, -1, &res, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "Failed to enumerate peers: %s\n", err_msg);
		return;
	}
	int step = sqlite3_step(res);
	while(step == SQLITE_ROW) {
		tgl_peer_id_t id = convert_id(res);
		step = sqlite3_step(res);
		tgl_peer_t *peer = tgl_peer_get(TLS, id);
		if(peer->print_name) 
			filler(buf, peer->print_name, NULL, 0); 
	}
	sqlite3_finalize(res);
}

static void* _tg_tgl_init(void* arg) {
	(void)arg;
	TLS = tgl_state_alloc();
	tgl_set_rsa_key(TLS, "tg-server.pub");
	tgl_set_download_directory(TLS, "~/.tgfs");
	tgl_set_callback(TLS, &upd_cb);

	struct event_base *ev = event_base_new ();
	tgl_set_ev_base (TLS, ev);
	tgl_set_net_methods (TLS, &tgl_conn_methods);
	tgl_set_timer_methods (TLS, &tgl_libevent_timers);
	assert (TLS->timer_methods);


	tgl_register_app_id (TLS, TGFS_APP_ID, TGFS_APP_HASH); 
  	tgl_set_app_version (TLS, "tgfs " PACKAGE_VERSION);

	int init = tgl_init(TLS);
	assert(init >= 0);

	// Simulate empty auth file
	// TODO: Change to prefered DC in runtime
    	bl_do_dc_option (TLS, 0, 1, "", 0, TG_SERVER_1, strlen (TG_SERVER_1), 443);
    	bl_do_dc_option (TLS, 0, 2, "", 0, TG_SERVER_2, strlen (TG_SERVER_2), 443);
    	bl_do_dc_option (TLS, 0, 3, "", 0, TG_SERVER_3, strlen (TG_SERVER_3), 443);
    	bl_do_dc_option (TLS, 0, 4, "", 0, TG_SERVER_4, strlen (TG_SERVER_4), 443);
    	bl_do_dc_option (TLS, 0, 5, "", 0, TG_SERVER_5, strlen (TG_SERVER_5), 443);
    	bl_do_set_working_dc (TLS, TG_SERVER_DEFAULT);

	tgl_set_rsa_key_direct (TLS, tglmp_get_default_e (), tglmp_get_default_key_len (), tglmp_get_default_key ());

	read_auth_file();

  	tgl_login (TLS);

	_tg_storage_init();

	while (1) {
    		event_base_loop (TLS->ev_base, EVLOOP_ONCE);
    		usleep(500);
	}
	return NULL;
}

pthread_t t;