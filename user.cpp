#include "user.h"
#include "DBServConn.h"
#include "LoginServConn.h"
#include "RouteServConn.h"

#include "AttachData.h"
#include "IM.Buddy.pb.h"
#include "IM.Message.pb.h"
#include "IM.Login.pb.h"
#include "IM.Other.pb.h"
#include "IM.Group.pb.h"
#include "IM.BaseDefine.pb.h"
#include "IM.Live.pb.h"
#include "IM.Server.pb.h"
#include "IM.SwitchService.pb.h"
#include "IM.Consult.pb.h"
#include "public_define.h"
#include "ImPduBase.h"
#include "BaseSocket.h"
#include "StringUtils.h"

using namespace IM::BaseDefine;
using namespace IM::Live;




extern vector<user_t*> user_vec;
extern map<int /*imid*/ ,user_t *>g_user_socketmap;
//extern vector<user_t *>valid_user;

pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;


user_t *user_create() {
	user_t *user = new (nothrow) user_t;
	if (user) {
		user->id = 0;
		user->wscon = ws_conn_new();
		user->msg = "";
	}
	return user;
}


void user_destroy(user_t *user) {
	if (user) {
		if (user->wscon) {
			ws_conn_free(user->wscon);
		}
		delete user;
	}
}



//handle login 
/*
{
"cmd":"loginChatRoom",   
"data":
{
"imId":登录者imid,
"name":登录者名字,
"portrait":登录者头像地址,
"liveId":唯一的直播Id
}
}
*/
#if 1
void _HandleIMliveLogin(jsonxx::Object & obj,user_t *user){

         jsonxx::Object object=obj.get<jsonxx::Object>("data");
		 
		 int imid=object.get<jsonxx::Number>("imId");
		 string username=object.get<jsonxx::String>("name");
		 string portrait=object.get<jsonxx::String>("portrait");
		 int liveId=object.get<jsonxx::Number>("liveId");
		 INFO("user_id=%u,username=%s,portrait=%s liveId=%u",imid,username.c_str(),portrait.c_str(),liveId);
		 
         vector<user_t *>::iterator iter=user_vec.begin();
         int i=0;
		 for(;i<user_vec.size();i++)
		 {
		       if(user_vec[i]==user){
			   	  break;
			   }
		 }
		 if(user_vec[i]==user&&user_vec[i]->id==0){
		 	    INFO("start init login user infomation");
		 	    user_vec[i]->id=imid;
				user_vec[i]->liveId=liveId;
		        user_vec[i]->portrait=portrait;
				user_vec[i]->name=username;
		 }else if(user_vec[i]==user&&user_vec[i]->id!=0){
		 	    INFO("over login ");
				return;
		 }
		 
		 //add just login 
		 pthread_mutex_lock(&mutex);
		 g_user_socketmap.insert(map<int,user_t *>::value_type(user_vec[i]->id,user_vec[i]));
		 pthread_mutex_unlock(&mutex);
		 
		 INFO("send message");
		 CImPdu pdu;
	     IMLiveLoginReq msg;
		 INFO("send message end1");
		 msg.set_liveid(liveId);
		 msg.set_name(username);
		 msg.set_user_id(imid);
		 msg.set_portrait(portrait);
		 pdu.SetPBMsg(&msg);
		 pdu.SetServiceId(SID_LIVE);
		 pdu.SetCommandId(CID_LIVE_LOGIN_REQUEST);		 
		
		
         CDBServConn *dbcon=get_db_serv_conn(); 
		 if(dbcon&&dbcon->IsOpen())
		 {      
		    dbcon->SendPdu(&pdu);
		 }


		 //send msg to route server 
		 //kickout
		 
}


#endif

void frame_recv_cb(void *arg) {
    INFO("frame_recv_cv function enter!");
	user_t *user = (user_t*)arg;
	if (user->wscon->frame->payload_len > 0) {
	    
		user->msg += string(user->wscon->frame->payload_data, user->wscon->frame->payload_len);
	}
		
	if (user->wscon->frame->fin == 1) {
		
		//INFO("start handle client  data  %s", user->msg.c_str());
		frame_buffer_t *fb = frame_buffer_new(1, 1, user->wscon->frame->payload_len, user->wscon->frame->payload_data);
	    //receive client json data
	    //parse json data
	    jsonxx::Object obj;
		std::istringstream input(user->wscon->frame->payload_data);
		INFO("fb->data=%s",fb->data);
        
        if(!obj.parse(input)){			
			 INFO("client sended json data format error! please check it");
			 string pstr="data format error";
			 for(auto i=0;i<user_vec.size();i++)
			 {
			      if(user!=user_vec[i]){
				  	 send_a_frame(user_vec[i]->wscon,fb);
				  }
			 }
			 frame_buffer_free(fb);
			 return;
		}
		INFO("jsonstring %s %s",obj.json().c_str(),obj.get<jsonxx::String>("cmd").c_str());
		if(obj.get<jsonxx::String>("cmd")=="loginChatRoom"){
			
			    
			      //todo something()
			      INFO("start handle login");
			      _HandleIMliveLogin(obj,user);//添加实际的全局用户信息
			      
			 
			 
		}else{
		     //todo exception()
		     
		     INFO("JSON data format error!");
		     send_a_frame(user->wscon,fb);
			 frame_buffer_free(fb);
			 return;
		}

		if (fb) {
			//send to other users
			for (int32_t i = 0; i < user_vec.size(); ++i) {
				if (user_vec[i] != user) {
#if 1
					if (send_a_frame(user_vec[i]->wscon, fb) == 0) {
						INFO("i send a message");
					}
#endif
				}
			}

			frame_buffer_free(fb);
		}

		user->msg = "";
	}
}
