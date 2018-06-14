websocket
=========

WebSocket server on C++ &amp; libevent#
#ws_liveplay_v1.0

#information
#### 这是一个ws 处理程序
#### 支持普通的连接
#### 可以在上线运行
#build
 >[root@localhost ws_server]make
 >生成sunlands_ws_server
 >运行./sunlands_ws_server
 

# struction

[ws_server]------(json 2 pb)----------->[db_server]
[db_server_con]----(pb 2 json)<-----------            
                


