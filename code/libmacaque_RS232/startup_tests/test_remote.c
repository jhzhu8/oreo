#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define WIN_TEST
#ifdef WIN_TEST
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib,"ws2_32.lib") // Winsock library
#else
#include <unistd.h>
#include "sock_interface/sock.h"
#endif

#define BUFF_SIZE       (255)
#define LOOPBACK_IP     "127.0.0.1"

#define MAX_RS232_BYTES 15 // bytes
typedef struct {
	uint8_t length;
	uint8_t RS232_data[MAX_RS232_BYTES];
} RS232_MSG;

#define ADD_BYTES(hi,lo)        (uint16_t)(((uint16_t)hi << 8)|(lo))
#define ADD_WORDS(hi, lo)       (uint32_t)(((uint32_t)hi << 16)|lo)
#define GET_ID(code)            (uint8_t)(code >> 4)

#define CONN_MSG1		            ((uint8_t[]){0x01,0xA4,0x06})
#define CONN_RESP1		            ((uint8_t)0x02)
#define CONN_MSG2                   ((uint8_t[]){0x03,0x80,0x25,0x00,0x00})
#define CONN_RESP2                  ((uint8_t)0x04)
#define CONN_MSG3                   ((uint8_t[]){0x07,0x00})
#define CONN_RESP3                  ((uint8_t)0x08)
#define CONN_MSG4	                ((uint8_t[]){0x03,0x00,0xC2,0x01,0x00})//1Mbit config
#define CONN_RESP4      	        ((uint8_t)0x04)
#define NUM_CONN_MSG                (4)
#define CONN_SYNC_RESP		        (0x0Du)
#define CONN_SYNC_BYTES		        (15)
#define CONN_SYNC_BYTE		        (0xFFu)
#define DISCONN_MSG		            ((uint8_t[]){0x05})
#define DISCONN_RESP		        ((uint8_t)0x06)

#define LOCAL_IP                    ("192.168.2.8")
#define NECK_LOCAL_PORT		        (51244u)
#define EYE_LOCAL_PORT		        (51243u)
#define NECK_IP			            ("192.168.2.2")
#define EYE_IP			            ("192.168.2.2")
#define CMD_PORT                    (uint16_t)(1700u)
#define CONN_PORT		            (uint16_t)(30689u)
#define EYE_CMD_PORT                CMD_PORT
#define EYE_CONN_PORT               CONN_PORT
#define NECK_CMD_PORT               (CMD_PORT+1)
#define NECK_CONN_PORT              (CONN_PORT+1)
#define MSG_ACK			            ((uint8_t)0x4F)

// Offsets to get info from payload
typedef enum {
    OFFSET_LENGTH,
    OFFSET_IDCODE_HIGH,
    OFFSET_IDCODE_LOW,
    OFFSET_OPTCODE_HIGH,
    OFFSET_OPTCODE_LOW,
    OFFSET_DATA_WORD1_HIGH,
    OFFSET_DATA_WORD1_LOW,
    OFFSET_DATA_WORD2_HIGH,
    OFFSET_DATA_WORD2_LOW,
    OFFSET_DATA_WORD3_HIGH,
    OFFSET_DATA_WORD3_LOW,
    OFFSET_DATA_WORD4_HIGH,
    OFFSET_DATA_WORD4_LOW,
    OFFSET_CHECKSUM,
    RS232_MAX_BYTES,
} offset_t;

static char* get_msg_str(RS232_MSG* frame, char* buff)
{
    snprintf(buff, BUFF_SIZE, "0x");
    char* ptr = buff+2;
    for(int i = 0; i < frame->length; i++) {
        snprintf(ptr, BUFF_SIZE, " %02x", frame->RS232_data[i]);
        ptr+=sizeof(frame->RS232_data[i]) + 2;
    }

    return buff;
}

static bool msg_comp(RS232_MSG* frame, RS232_MSG* exp_frame) 
{
    // Simple check
    if(frame->length != exp_frame->length) {
        printf("Size: Frame=%d Expected=%d\n", frame->length, exp_frame->length);
        return false;
    }
    
    // Compare byte-by-byte 
    if(memcmp(frame, exp_frame, frame->length) == 0) {
        return true;
    }

    char frame_str[BUFF_SIZE];
    char exp_frame_str[BUFF_SIZE];
    printf("Mismatch: Frame=%s Expected=%s\n", get_msg_str(frame, frame_str), get_msg_str(exp_frame, exp_frame_str));
    
    return false;
}

typedef struct _test_handle_t {
    char* local_ip;
    uint16_t local_port;
    uint16_t cmd_port;
    char* dest_ip;
    uint16_t dest_port;
    int conn_fd;
    int cmd_fd;
    LPDWORD tid;
    HANDLE handle;
    char* name;
} test_handle_t;

typedef enum {
    ARG_APP_NAME,
    ARG_DEV,
    ARG_FILE,
} cmd_args_t;

bool PerformSync(test_handle_t* dev)
{
    int recv_bytes;
    RS232_MSG frame;
    memset(&frame.RS232_data, 0, MAX_RS232_BYTES);
    recv_bytes = recv(dev->cmd_fd, frame.RS232_data, MAX_RS232_BYTES, 0);
    if(recv_bytes < 0) {
        printf("Failed to recv msg : %d\n", WSAGetLastError());
        return false;
    }
    frame.length = recv_bytes;

    for(int i = 0; i < CONN_SYNC_BYTES; i++) {
        if(frame.RS232_data[i] != 0xFF) {
            printf("Not a sync message\n");
            return false;
        }
    }

    memset(&frame.RS232_data, 0, MAX_RS232_BYTES);
    frame.length = CONN_SYNC_BYTES;
    for(int i = 0; i < CONN_SYNC_BYTES; i++) {
        if(i == CONN_SYNC_BYTES-1) {
            frame.RS232_data[i] = CONN_SYNC_RESP;
        } else {
            frame.RS232_data[i] = 0x00;
        }
    }
    int send_bytes = send(dev->cmd_fd, (const char*)frame.RS232_data, frame.length, 0);
    if(send_bytes != frame.length) {
        printf("Failed to send sync resp : %d\n", WSAGetLastError());
        return false;
    }

    return true;
}

/*DWORD WINAPI TxThread(LPVOID input)
{
    test_handle_t* dev = (test_handle_t*)(input);
    Sleep(1000);

    while(1) {

    }
    
    done_tx:
    printf("Exiting rx thread\n");
    return 0;
}*/

DWORD WINAPI RxThread(LPVOID input)
{
    test_handle_t* dev = (test_handle_t*)(input);

    printf("%s syncing....", dev->name);
    if(!PerformSync(dev)) {
        printf("failed\n");
        return 0;
    }
    printf("done\n");

    while(1) {
        int recv_bytes;
        RS232_MSG frame;
        memset(&frame, 0, sizeof(frame));
        recv_bytes = recv(dev->cmd_fd, frame.RS232_data, MAX_RS232_BYTES, 0);
        if(recv_bytes < 0) {
            printf("%s failed to recv msg : %d\n", dev->name, WSAGetLastError());
            break;
        } else if(recv_bytes == 0) {
            printf("zero msg\n");
            continue;
        }
        frame.length = recv_bytes;

        if(frame.length == CONN_SYNC_BYTES && frame.RS232_data[0] == CONN_SYNC_BYTE && frame.RS232_data[14] == CONN_SYNC_BYTE) {
            // Sync message
            printf("Resync\n");
            memset(&frame.RS232_data, 0, MAX_RS232_BYTES);
            frame.length = CONN_SYNC_BYTES;
            frame.RS232_data[CONN_SYNC_BYTES-1] = CONN_SYNC_RESP;
            int send_bytes = send(dev->cmd_fd, (const char*)frame.RS232_data, frame.length, 0);
            if(send_bytes != frame.length) {
                printf("Failed to send sync resp : %d\n", WSAGetLastError());
                goto done_rx;
            }
        } else {
            uint8_t length = frame.RS232_data[OFFSET_LENGTH];
            uint16_t destCode = ADD_BYTES(frame.RS232_data[OFFSET_IDCODE_HIGH], frame.RS232_data[OFFSET_IDCODE_LOW]);
            uint16_t optCode = ADD_BYTES(frame.RS232_data[OFFSET_OPTCODE_HIGH], frame.RS232_data[OFFSET_OPTCODE_LOW]);
            uint8_t payloadLength = length - sizeof(optCode) - sizeof(destCode);
            uint8_t id = GET_ID(destCode);
            printf("%s recv: opt=0x%x id=%d\n", dev->name, optCode, id);

            memset(&frame.RS232_data, 0, MAX_RS232_BYTES);
            frame.RS232_data[0] = 0x4F;
            int send_bytes = send(dev->cmd_fd, (const char*)frame.RS232_data, frame.length, 0);
            if(send_bytes != frame.length) {
                printf("Failed to send resp : %d\n", WSAGetLastError());
                goto done_rx;
            }
        }
    }

    done_rx:
    printf("Exiting rx thread\n");
    return 0;
}

int startup(test_handle_t* dev)
{
    printf("Initiating connection with following settings:\n");
    printf("Local: %s:%d\n", dev->local_ip, dev->local_port);
    printf("Remote: %s:%d\n\n", dev->dest_ip, dev->dest_port);
    
    printf("Creating conn socket....");
    dev->conn_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(dev->conn_fd == INVALID_SOCKET) {
        printf("Could not create socket: %d", WSAGetLastError());
        return -1;
    }
    printf("done\n");

    // reuse address
    int optval = 1;
    setsockopt(dev->conn_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));

    struct sockaddr_in localAddr;
    ZeroMemory(&localAddr, sizeof(localAddr));
    printf("Binding conn socket....");
    if(inet_pton(AF_INET, dev->local_ip, &(localAddr.sin_addr.s_addr)) != 1) {
        printf("could not format ip=%s err=%d\n", dev->local_ip, WSAGetLastError());
        return -1;
    }
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(dev->local_port);
    if(bind(dev->conn_fd, (struct sockaddr*)(&localAddr), sizeof(localAddr)) < 0) {
        printf("could not bind to ip=%s port=%d err=%d\n", dev->local_ip, dev->local_port, WSAGetLastError());
        return -1;
    }
    printf("done\n");
    
    struct sockaddr_in remoteAddr;
    ZeroMemory(&remoteAddr, sizeof(remoteAddr));
    printf("Connecting conn socket....");
    if(inet_pton(AF_INET, dev->dest_ip, &(remoteAddr.sin_addr.s_addr)) != 1) {
        printf("could not format ip=%s err=%d\n", dev->dest_ip, WSAGetLastError());
        return -1;
    }
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(dev->dest_port);
    if(connect(dev->conn_fd, (struct sockaddr*)(&remoteAddr), sizeof(remoteAddr)) < 0) {
        printf("could not connect to ip=%s port=%d err=%d\n", dev->dest_ip, dev->dest_port, WSAGetLastError());
        return -1;
    }
    printf("done\n");

    printf("Creating cmd socket....");
    dev->cmd_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(dev->cmd_fd == INVALID_SOCKET) {
        printf("Could not create socket: %d", WSAGetLastError());
        return -1;
    }
    printf("done\n");

    // reuse address
    optval = 1;
    setsockopt(dev->cmd_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));

    ZeroMemory(&localAddr, sizeof(localAddr));
    printf("Binding cmd socket....");
    if(inet_pton(AF_INET, dev->local_ip, &(localAddr.sin_addr.s_addr)) != 1) {
        printf("could not format ip=%s err=%d\n", dev->local_ip, WSAGetLastError());
        return -1;
    }
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(dev->cmd_port);
    if(bind(dev->cmd_fd, (struct sockaddr*)(&localAddr), sizeof(localAddr)) < 0) {
        printf("could not bind to ip=%s port=%d err=%d\n", dev->local_ip, dev->cmd_port, WSAGetLastError());
        return -1;
    }
    printf("done\n");
    
    ZeroMemory(&remoteAddr, sizeof(remoteAddr));
    printf("Connecting cmd socket....");
    if(inet_pton(AF_INET, dev->dest_ip, &(remoteAddr.sin_addr.s_addr)) != 1) {
        printf("could not format ip=%s err=%d\n", dev->dest_ip, WSAGetLastError());
        return -1;
    }
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(dev->dest_port);
    if(connect(dev->cmd_fd, (struct sockaddr*)(&remoteAddr), sizeof(remoteAddr)) < 0) {
        printf("could not connect to ip=%s port=%d err=%d\n", dev->dest_ip, dev->dest_port, WSAGetLastError());
        return -1;
    }
    printf("done\n");

    // Startup
    const uint8_t* recv_conn[NUM_CONN_MSG] = {CONN_MSG1, CONN_MSG2, CONN_MSG3, CONN_MSG4};
    const uint8_t recv_size[NUM_CONN_MSG] = {sizeof(CONN_MSG1), sizeof(CONN_MSG2), sizeof(CONN_MSG3), sizeof(CONN_MSG4)};
    const uint8_t conn_resp[NUM_CONN_MSG] = {CONN_RESP1, CONN_RESP2, CONN_RESP3, CONN_RESP4};

    int recv_bytes;
    for(int i = 0; i < NUM_CONN_MSG; i++) {
        RS232_MSG recv_msg;
        memset(&recv_msg.RS232_data, 0, MAX_RS232_BYTES);
        printf("Recving conn msg %d....", i);
        recv_bytes = recv(dev->conn_fd, recv_msg.RS232_data, MAX_RS232_BYTES, 0);
        if(recv_bytes <= 0) {
            printf("Failed to recv %d conn msg\n", i);
            return -1;
        }
        recv_msg.length = recv_bytes;
        printf("recvd\n");

        RS232_MSG exp_msg;
        exp_msg.length = recv_size[i];
        memset(exp_msg.RS232_data, 0, MAX_RS232_BYTES);
        memcpy(exp_msg.RS232_data, recv_conn[i], exp_msg.length);
        exp_msg.length = recv_size[i];
        if(!msg_comp(&recv_msg, &exp_msg)) {
            printf("Recvd unexpected conn msg %d\n", i);
            return -1;
        }
        
        RS232_MSG send_msg;
        printf("Sending resp msg %d....", i);
        memset(send_msg.RS232_data, 0, MAX_RS232_BYTES);
        send_msg.length = sizeof(conn_resp[i]);
        memcpy(send_msg.RS232_data, &(conn_resp[i]), send_msg.length);
        if(send(dev->conn_fd, send_msg.RS232_data, send_msg.length, 0) < 0) {
            printf("Failed to send conn msg %d\n", i);
            return -1;
        }
        printf("sent\n");
    }

    printf("Test finished startup sequence\n");

    return 0;
}

int main(int argc, char* argv[])
{
    test_handle_t eye, neck;
    neck.dest_ip = LOCAL_IP;
    neck.dest_port = NECK_LOCAL_PORT;
    neck.local_ip = NECK_IP;
    neck.local_port = NECK_CONN_PORT;
    neck.cmd_port = NECK_CMD_PORT;
    neck.conn_fd = -1;
    neck.cmd_fd = -1;
    neck.handle = NULL;
    neck.tid = NULL;
    neck.name = "neck";

    eye.dest_ip = LOCAL_IP;
    eye.dest_port = EYE_LOCAL_PORT;
    eye.local_ip = EYE_IP;
    eye.local_port = EYE_CONN_PORT;
    eye.cmd_port = EYE_CMD_PORT;
    eye.conn_fd = -1;
    eye.cmd_fd = -1;
    eye.tid = NULL;
    eye.name = "eye";

    WSADATA wsa;
    printf("\nInitialising Winsock...");
    if(WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed. Error Code: %d\n", WSAGetLastError());
        goto done;
    }
    printf("initialized\n\n");

    // Startup sequence
    printf("***Starting up eye dev***\n");
    if(startup(&eye) < 0) {
        printf("***Failed***\n");
        goto done;
    }
    printf("***Successful***\n\n");


    printf("***Starting up neck dev***\n");
    if(startup(&neck)) {
        printf("***Failed***\n");
        goto done;
    }
    printf("***Successful***\n\n");

    eye.handle = CreateThread(NULL, 0, RxThread, &eye, 0, eye.tid);
    if(eye.handle == NULL) {
        printf("failed\n");
        return -1;
    }

    neck.handle = CreateThread(NULL, 0, RxThread, &neck, 0, neck.tid);
    if(neck.handle == NULL) {
        printf("failed\n");
        return -1;
    }

    done:
    while(1) {};
}