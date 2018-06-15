/*
 * DBServConn.cpp
 *
 *  Created on: 2013-7-8
 *      Author: ziteng@mogujie.com
 */

#include "EncDec.h"
#include "DBServConn.h"
#include "RouteServConn.h"

#include "security.h"
#include "AttachData.h"
#include "jsonxx.h"
#include "IM.Other.pb.h"
#include "IM.Buddy.pb.h"
#include "IM.Login.pb.h"
#include "IM.Group.pb.h"
#include "IM.Message.pb.h"
#include "IM.Server.pb.h"
#include "IM.Consult.pb.h"
#include "IM.Other.pb.h"
#include "IM.RPC.pb.h"
#include "IM.Live.pb.h"

#include "ImPduBase.h"
#include "public_define.h"
#include "util.h"
#include "LoginServConn.h"
#include "connection.h"
#include "user.h"
#include <pthread.h>

using namespace IM::BaseDefine;

static ConnMap_t g_db_server_conn_map;

serv_info_t*    g_db_server_list = NULL;
uint32_t		g_db_server_count = 0;			// 到DBServer的总连接数
uint32_t		g_db_server_login_count = 0;	// 到进行登录处理的DBServer的总连接数

extern ConnMap_t g_system_conn_map;

extern map<uint32_t,user_t *> g_user_socketmap;

static pthread_rwlock_t  rwlock=PTHREAD_RWLOCK_INITIALIZER;

//-------------------add by hfy
extern struct time_forbid *tf;
inline static int isTimevalid(){
    time_t now = time(NULL);
    struct tm * _tm = localtime(&now);
    if((tf->wday_forbid.find(_tm->tm_wday)!=tf->wday_forbid.end()) && (_tm->tm_hour>=tf->hourlower_forbid) && (_tm->tm_hour<tf->hourupper_forbid))
        return 0;
    return 1;
}




//------------------------------------------------------------

static void db_server_conn_timer_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
	ConnMap_t::iterator it_old;
	CDBServConn* pConn = NULL;
	uint64_t cur_time = get_tick_count();

	for (ConnMap_t::iterator it = g_db_server_conn_map.begin(); it != g_db_server_conn_map.end(); ) {
		it_old = it;
		it++;

		pConn = (CDBServConn*)it_old->second;
		if (pConn->IsOpen()) {
			pConn->OnTimer(cur_time);
		}
	}

	// reconnect DB Storage Server
	// will reconnect in 4s, 8s, 16s, 32s, 64s, 4s 8s ...
	serv_check_reconnect<CDBServConn>(g_db_server_list, g_db_server_count);
}

void init_db_serv_conn(serv_info_t* server_list, uint32_t server_count, uint32_t concur_conn_cnt)
{
	g_db_server_list = server_list;
	g_db_server_count = server_count;

	uint32_t total_db_instance = server_count / concur_conn_cnt;
	g_db_server_login_count = (total_db_instance / 2) * concur_conn_cnt;
	INFO("DB server connection index for login business: [0, %u), for other business: [%u, %u) ",
			g_db_server_login_count, g_db_server_login_count, g_db_server_count);

	serv_init<CDBServConn>(g_db_server_list, g_db_server_count);

	netlib_register_timer(db_server_conn_timer_callback, NULL, 1000);
}

// get a random db server connection in the range [start_pos, stop_pos)
static CDBServConn* get_db_server_conn_in_range(uint32_t start_pos, uint32_t stop_pos)
{
	uint32_t i = 0;
	CDBServConn* pDbConn = NULL;

	// determine if there is a valid DB server connection
	for (i = start_pos; i < stop_pos; i++) {
		pDbConn = (CDBServConn*)g_db_server_list[i].serv_conn;
		if (pDbConn && pDbConn->IsOpen()) {
			break;
		}
	}

	// no valid DB server connection
	if (i == stop_pos) {
		return NULL;
	}

	// return a random valid DB server connection
	while (true) {
		int i = rand() % (stop_pos - start_pos) + start_pos;
		pDbConn = (CDBServConn*)g_db_server_list[i].serv_conn;
		if (pDbConn && pDbConn->IsOpen()) {
			break;
		}
	}

	return pDbConn;
}

CDBServConn* get_db_serv_conn_for_login()
{
	// 先获取login业务的实例，没有就去获取其他业务流程的实例
	CDBServConn* pDBConn = get_db_server_conn_in_range(0, g_db_server_login_count);
	if (!pDBConn) {
		pDBConn = get_db_server_conn_in_range(g_db_server_login_count, g_db_server_count);
	}

	return pDBConn;
}

CDBServConn* get_db_serv_conn()
{
	// 先获取其他业务流程的实例，没有就去获取login业务的实例
	CDBServConn* pDBConn = get_db_server_conn_in_range(g_db_server_login_count, g_db_server_count);
	if (!pDBConn) {
		pDBConn = get_db_server_conn_in_range(0, g_db_server_login_count);
	}

	return pDBConn;
}

CImConn* GetSysConnByHandle(uint32_t handle)
{
    CImConn* pSysConn = NULL;
    ConnMap_t::iterator it = g_system_conn_map.find(handle);
    if (it != g_system_conn_map.end()) {
        pSysConn = it->second;
    }

    return pSysConn;
}

CDBServConn::CDBServConn()
{
	m_bOpen = false;
}

CDBServConn::~CDBServConn()
{

}

void CDBServConn::Connect(const char* server_ip, uint16_t server_port, uint32_t serv_idx)
{
	INFO("Connecting to DB Storage Server %s:%d ", server_ip, server_port);

	m_serv_idx = serv_idx;
	m_handle = netlib_connect(server_ip, server_port, imconn_callback, (void*)&g_db_server_conn_map);

	if (m_handle != NETLIB_INVALID_HANDLE) {
		g_db_server_conn_map.insert(make_pair(m_handle, this));
	}
}

void CDBServConn::Close()
{
    INFO("close from db server handle=%d, handle=%d", m_handle);

	// reset server information for the next connect
	serv_reset<CDBServConn>(g_db_server_list, g_db_server_count, m_serv_idx);

	if (m_handle != NETLIB_INVALID_HANDLE) {
		netlib_close(m_handle);
		g_db_server_conn_map.erase(m_handle);
	}

	ReleaseRef();
}

void CDBServConn::OnConfirm()
{
	INFO("connect to db server success %u  %s:%d.", m_serv_idx, g_db_server_list[m_serv_idx].server_ip.c_str(), g_db_server_list[m_serv_idx].server_port);
	m_bOpen = true;
	g_db_server_list[m_serv_idx].reconnect_cnt = MIN_RECONNECT_CNT / 2;
}

void CDBServConn::OnClose()
{
	WARN("onclose from db server handle=%d. from %s:%d.", m_handle, g_db_server_list[m_serv_idx].server_ip.c_str(), g_db_server_list[m_serv_idx].server_port);
	Close();
}

void CDBServConn::OnTimer(uint64_t curr_tick)
{
	if (curr_tick > m_last_send_tick + SERVER_HEARTBEAT_INTERVAL) {
        //DEBUG("Send heartbeat to db_pro server %u  %s:%d, curr_tick:%ld, m_last_send_tick:%ld,  %ld", m_serv_idx, g_db_server_list[m_serv_idx].server_ip.c_str(), g_db_server_list[m_serv_idx].server_port, curr_tick, m_last_send_tick, curr_tick-m_last_send_tick);
        IM::Other::IMHeartBeat msg;
        CImPdu pdu;
        pdu.SetPBMsg(&msg);
        pdu.SetServiceId(SID_OTHER);
        pdu.SetCommandId(CID_OTHER_HEARTBEAT);
		SendPdu(&pdu);
	}

	if (curr_tick > m_last_recv_tick + SERVER_TIMEOUT) {
		ERROR("connect to db_pro server %u timeout  %s:%d, curr_tick:%ld, m_last_recv_tick:%ld,  %ld.", m_serv_idx, g_db_server_list[m_serv_idx].server_ip.c_str(), g_db_server_list[m_serv_idx].server_port, curr_tick, m_last_recv_tick, curr_tick-m_last_recv_tick);
		Close();
	}
}



void CDBServConn::HandlePdu(CImPdu* pPdu)
{
	switch (pPdu->GetCommandId()) {
        case CID_OTHER_HEARTBEAT:
            //DEBUG("Recv heartbeat from db server %u  %s:%d.", m_serv_idx, g_db_server_list[m_serv_idx].server_ip.c_str(), g_db_server_list[m_serv_idx].server_port);
            break;
        //case CID_OTHER_VALIDATE_RSP:
        
       case CID_LIVE_LOGIN_RESPONSE:
	   	    _HandleLiveLoginResp(pPdu);
            break;


        default:
            WARN("db server, wrong cmd id=%d ", pPdu->GetCommandId());
	}
}



void CDBServConn::_HandleLiveLoginResp(CImPdu*pPdu){
        IM::Live::IMLiveLoginResp resp;
		if(!resp.ParseFromArray(pPdu->GetBodyData(),pPdu->GetBodyLength()))
		{
		    INFO("db server failed");
			return;
		}

		int32_t liveid=resp.liveid();
		int32_t userid=resp.user_id();
		int32_t number=resp.number();
		int32_t result=resp.result();
		//readlock start
	    pthread_rwlock_rdlock(&rwlock);
        auto iter=g_user_socketmap.find(userid);
		pthread_rwlock_unlock(&rwlock); 
		//readlock end
		
		if(iter!=g_user_socketmap.end())
		 {
          user_t * user=iter->second;
		  //user->wscon
		  //create json data
		  jsonxx::Object obj;
		  obj<<"cmd"<<"loginChatRoom";
		  obj<<"result"<<result;
          jsonxx::Object sonobj;
          sonobj<<"imId"<<userid;
		  sonobj<<"liveId"<<liveid;
		  sonobj<<"number"<<number;
		  obj<<"data"<<sonobj.json();
          string jsonResp=obj.json();
		  memcpy(user->wscon->frame->payload_data,jsonResp.c_str(),jsonResp.size());
		  user->wscon->frame->payload_len=jsonResp.size();
		  
		  frame_buffer_t *fb = frame_buffer_new(1, 1, user->wscon->frame->payload_len, user->wscon->frame->payload_data);
		  send_a_frame(user->wscon,fb);
		  frame_buffer_free(fb);
				  
		}
		
}


