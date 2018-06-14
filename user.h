/**
 *
 * filename: user.h
 * summary:
 * author: caosiyang
 * email: csy3228@gmail.com
 *
 */
#ifndef USER_H
#define USER_H

#include <istream>
#include "connection.h"
#include "jsonxx.h"


using namespace jsonxx;
using namespace  std;

struct user {

	 user(uint32_t userid,string nick,string portraits,uint32_t liveIds){
	 	  id=userid;
		  name=nick;
		  portrait=portraits;
		  liveId=liveIds;
		  
	 }
    user(){}
	
	uint32_t id;
	string name;
	string portrait;
	uint32_t liveId;
	ws_conn_t *wscon;
	string msg;
};

typedef struct user user_t;





//void _HandleIMliveLogin(jsonxx::Object &,user_t*);


user_t *user_create();


void user_destroy(user_t *user);


void frame_recv_cb(void *arg);


#endif
