cmake_minimum_required(VERSION 2.6)
PROJECT(wsserver)

AUX_SOURCE_DIRECTORY(./ SRC_LIST)

#SET(LIBRARY_OUTPUT_PATH ./)
SET(EXECUTABLE_OUTPUT_PATH  ./)
SET(PB_LIB_DIR ../base/pb/lib/linux)
SET(SECURITY_LIB_DIR ../base/security/lib/linux)


ADD_DEFINITIONS( -g -W -Wall -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DAC_HAS_INFO
-DAC_HAS_WARNING -DAC_HAS_ERROR -DAC_HAS_CRITICAL -DTIXML_USE_STL
-Wno-literal-suffix -Wno-unused-variable -Wno-unused-function -Wwrite-strings -Wparentheses
-Wno-sign-compare -Wno-deprecated -Wno-unused-parameter
-DAC_HAS_DEBUG -DLINUX_DAEMON -std=c++11)

INCLUDE_DIRECTORIES(./ ../base ../base/pb/protocol ../base/pb/ ../base/security/)
LINK_DIRECTORIES(./ ./libs ../base ${PB_LIB_DIR} ../base/slog/lib ${SECURITY_LIB_DIR})

#ADD_XXXX必须在TARGET_LINK_LIBRARIES前面，否则会报错
#ADD_LIBRARY(${PROJECTNAME} SHARED/STATIC ${SRC_LIST})
ADD_EXECUTABLE(sunlands_ws_server ${SRC_LIST})

TARGET_LINK_LIBRARIES(sunlands_ws_server base protobuf-lite slog crypto  event pthread )
 
