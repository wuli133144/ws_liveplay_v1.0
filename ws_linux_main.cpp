#include "ws_linux_main.h"
#include <pthread.h>
#include "netlib.h"
#include "EncDec.h"
#include "ConfigFileReader.h"
#include "LoginServConn.h"
#include "RouteServConn.h"
#include "DBServConn.h"


#include <map>
//#include "version.h"
#include "../base/FileMonitor.h"



static struct event_base *base = NULL;
static evconnlistener *listener = NULL;
vector<user_t*> user_vec;
map<uint32_t ,user_t*> g_user_socketmap;
ConnMap_t g_system_conn_map;
static const uint32_t WS_REQ_ONCE_READ = 1;







#define DEFAULT_CONCURRENT_DB_CONN_CNT  10



time_forbid *tf;




void user_disconnect(user_t *user) {
	if (user) {
		//update user list
		for (vector<user_t*>::iterator iter = user_vec.begin(); iter != user_vec.end(); ++iter) {
			if (*iter == user) {
				user_vec.erase(iter);
				break;
			}
		}
		user_destroy(user);
	}
	
}


void user_disconnect_cb(void *arg) {
	LOG("%s", __func__);
	user_t *user = (user_t*)arg;
	user_disconnect(user);
}


void listencb(struct evconnlistener *listener, evutil_socket_t clisockfd, struct sockaddr *addr, int len, void *ptr) {
	struct event_base *eb = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(eb, clisockfd, BEV_OPT_CLOSE_ON_FREE);
	LOG("a user logined in, socketfd = %d", bufferevent_getfd(bev));

	//create a user
	user_t *user = user_create();
	if(user->wscon==NULL){
		 INFO("error create_user");
	}
	INFO("new user enter!");
	user->wscon->bev = bev;
	user_vec.push_back(user);
	//ws_conn_setcb(wscon, HANDSHAKE, testfunc, (void*)"haha");
	ws_conn_setcb(user->wscon, FRAME_RECV, frame_recv_cb, user);
	ws_conn_setcb(user->wscon, CLOSE, user_disconnect_cb, user);

	ws_serve_start(user->wscon);
}







static void * HandleReceivePeerServerData(void *arg){
        pthread_detach(pthread_self());
		
        CConfigFileReader config_file("WS.conf");

		char* listen_ip = config_file.GetConfigName("ListenIP");
		char* str_listen_port = config_file.GetConfigName("ListenPort");
		char* ip_addr1 = config_file.GetConfigName("IpAddr1");	// 电信IP
		char* ip_addr2 = config_file.GetConfigName("IpAddr2");	// 网通IP
		char* str_max_conn_cnt = config_file.GetConfigName("MaxConnCnt");
		uint32_t db_server_count = 0;
		serv_info_t* db_server_list = read_server_config(&config_file, "DBServerIP", "DBServerPort", db_server_count);

		uint32_t login_server_count = 0;
		serv_info_t* login_server_list = read_server_config(&config_file, "LoginServerIP", "LoginServerPort", login_server_count);

		uint32_t route_server_count = 0;
		serv_info_t* route_server_list = read_server_config(&config_file, "RouteServerIP", "RouteServerPort", route_server_count);

	    uint32_t push_server_count = 0;
	    serv_info_t* push_server_list = read_server_config(&config_file, "PushServerIP", "PushServerPort", push_server_count);

	    uint32_t rpcproxy_server_count = 0;
	    serv_info_t* rpcproxy_server_list = read_server_config(&config_file, "RPCServerIP", "RPCServerPort", rpcproxy_server_count);	

		//初始化日志限制级别
		char confPath[256];
		strcpy(confPath, "./WS.conf");
		char* str_log_level = config_file.GetConfigName("LogLevel");
		if(!LogLevel::getInstance() || !LogLevel::getInstance()->init(atoi(str_log_level), string(confPath))){
			ERROR("log level load something wrong.exit");
			return NULL;
		}
		CFileMonitor MonitorEvent(confPath,logLevelUpdate);
		pthread_t tid_level = 0;
		pthread_create(&tid_level, NULL, fileMonitor,&MonitorEvent);

	

												  
	    tf = new time_forbid();
	    char* wday_forbid = config_file.GetConfigName("wday_forbid");
	    char* hourlower_forbid = config_file.GetConfigName("hourlower_forbid");
	    char* hourupper_forbid = config_file.GetConfigName("hourupper_forbid");
	    CStrExplode wday(wday_forbid,',');
	    for(uint32_t i = 0; i < wday.GetItemCnt(); i++){
	        char* day = wday.GetItem(i);
	        int iday = atoi(day);
	        tf->wday_forbid.insert(iday);
	    }
	    tf->hourlower_forbid = atoi(hourlower_forbid);
	    tf->hourupper_forbid = atoi(hourupper_forbid);
	    //---------------------------

	    
		// 必须至少配置2个BusinessServer实例, 一个用于用户登录业务，一个用于其他业务
		// 这样当其他业务量非常繁忙时，也不会影响客服端的登录验证
		// 建议配置4个实例，这样更新BusinessServer时，不会影响业务
		if (db_server_count < 2) {
			INFO("DBServerIP need 2 instance at lest ");
			return NULL;
		}

		// 到BusinessServer的开多个并发的连接
		uint32_t concurrent_db_conn_cnt = DEFAULT_CONCURRENT_DB_CONN_CNT;
		uint32_t db_server_count2 = db_server_count * DEFAULT_CONCURRENT_DB_CONN_CNT;
		char* concurrent_db_conn = config_file.GetConfigName("ConcurrentDBConnCnt");
		if (concurrent_db_conn) {
			concurrent_db_conn_cnt  = atoi(concurrent_db_conn);
			db_server_count2 = db_server_count * concurrent_db_conn_cnt;
		}

		serv_info_t* db_server_list2 = new serv_info_t [ db_server_count2];
		for (uint32_t i = 0; i < db_server_count2; i++) {
			db_server_list2[i].server_ip = db_server_list[i / concurrent_db_conn_cnt].server_ip.c_str();
			db_server_list2[i].server_port = db_server_list[i / concurrent_db_conn_cnt].server_port;
		}

		if (!listen_ip || !str_listen_port || !ip_addr1) {
			INFO("config file miss, exit... ");
			return NULL;
		}
        
		// 没有IP2，就用第一个IP
		if (!ip_addr2) {
			ip_addr2 = ip_addr1;
		}

		uint16_t listen_port = atoi(str_listen_port);
		uint32_t max_conn_cnt = atoi(str_max_conn_cnt);

		int ret = netlib_init();

		if (ret == NETLIB_ERROR)
			return NULL;

		printf("receive im_server start listen on: %s:%d\n", listen_ip, listen_port);
		//init_msg_conn();
		
		init_db_serv_conn(db_server_list2, db_server_count2, concurrent_db_conn_cnt);

		init_login_serv_conn(login_server_list, login_server_count, ip_addr1, ip_addr2, listen_port, max_conn_cnt);

		init_route_serv_conn(route_server_list, route_server_count);		
		printf("now enter the event loop...\n");
	    
		netlib_eventloop();

	    return NULL;
		   
}

int main() {
	//SIGPIPE ignore
	pthread_t pid;	
	srand(time(NULL));
	
	struct sigaction act;
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) == 0) {
		INFO("SIGPIPE ignore");
	}
	//
     writePid();
	//initialize
	setbuf(stdout, NULL);
	base = event_base_new();
	assert(base);

	struct sockaddr_in srvaddr;
	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr =inet_addr("172.16.103.125");
	srvaddr.sin_port = htons(9000);
     
	listener = evconnlistener_new_bind(base, listencb, NULL, LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, 500, (const struct sockaddr*)&srvaddr, sizeof(struct sockaddr));
	assert(listener);

	//启动另外一个服务
      
    pthread_create(&pid,NULL,HandleReceivePeerServerData,NULL);

	
	event_base_dispatch(base);
    
	INFO("loop exit");

	evconnlistener_free(listener);
	event_base_free(base);

	return 0;
}
