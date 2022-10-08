#include "client.h"
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include "sys.h"
#include "crc.h"
#include "modbus_tcp_main.h"
#include "modbus.h"
#include "my_socket.h"
#include <sys/socket.h>
#include <sys/msg.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "importBams.h"
#include "logicAndControl.h"
//当使用Modbus/TCP时，modbus poll一般模拟客户端，modbus slave一般模拟服务端
int wait_flag = 0;
char modbus_sockt_state[MAX_PCS_NUM];
int modbus_client_sockptr[MAX_PCS_NUM];

MyData clent_data_temp[MAX_PCS_NUM];
int g_comm_qmegid[MAX_PCS_NUM];

unsigned short g_num_frame = 1;
void RunAccordingtoStatus(int id_thread)
{
	printf("\n\nLCD:%d StateMachine...\n", id_thread);
	int ret = 1;
	switch (lcd_state[id_thread])
	{
	case LCD_RUNNING:
	{
		printf("LCD:%d  doFun03Tasks!!!!\n", id_thread);
		ret = doFun03Tasks(id_thread, &curPcsId);
	}
	break;
	case LCD_SET_TIME:
	{
		printf("lcd_state[%d]:%d \n", id_thread, lcd_state[id_thread]);
		printf("初始化时间...\n");
		ret = setTime(id_thread);
	}
	break;
	case LCD_INIT:
	{
		printf("LCD:%d 初始化...\n", id_thread);
		ret = ReadNumPCS(id_thread);
	}
	break;
	case LCD_SET_MODE:
	{
		// 0x3046	产品运行模式设置	uint16	整机	1	5	"需在启机前设置，模块运行后无法进行设置
		// 1：PQ模式（高低穿功能，需选择1）；
		// 5：VSG模式（并离网功能，需选择5）；"
		printf("LCD:%d 设置运行模式...\n", id_thread);
		ret = SetLcdFun06(id_thread, 0x3046, g_emu_op_para.LcdOperatingMode[id_thread]);
	}
	break;
	case LCD_PQ_PCS_MODE:
	{
		printf("LCD:%d PCS 设置成PQ模式...\n", id_thread);
		unsigned short regaddr; // = pq_pcspw_set[curPcsId][curTaskId];
		unsigned short val;
		if (curTaskId == 0)
		{
			regaddr = pqpcs_mode_set[curPcsId];
			val = g_emu_op_para.pq_mode[id_thread][curPcsId];
		}
		else
		{
			if (g_emu_op_para.pq_mode[id_thread][curPcsId] == PQ_STP) //设置恒功率
			{
				regaddr = pqpcs_pw_set[curPcsId];
				val = g_emu_op_para.pq_pw[id_thread][curPcsId];
			}
			else // PQ_STA 设置恒流
			{
				regaddr = pqpcs_cur_set[curPcsId];
				val = g_emu_op_para.pq_cur[id_thread][curPcsId];
			}
		}

		ret = SetLcdFun06(id_thread, regaddr, val);
	}
	break;

	case LCD_VSG_MODE:
	{
		printf("LCD:%d PCS 设置成VSG模式...\n", id_thread);
		unsigned short regaddr = 0x3047; // = pq_pcspw_set[curPcsId][curTaskId];
		unsigned short val = g_emu_op_para.vsg_mode[id_thread];
		ret = SetLcdFun06(id_thread, regaddr, val);
	}
	break;

	case LCD_VSG_PW_PCS_MODE:
	{
		printf("LCD:%d  VSG 有功参数设置 ...\n", id_thread);
		unsigned short regaddr; // = pq_pcspw_set[curPcsId][curTaskId];
		unsigned short val;

		regaddr = vsgpcs_pw_set[curPcsId];
		val = g_emu_op_para.vsg_pw[id_thread][curPcsId];
		// regaddr = vsgpcs_qw_set[curPcsId];
		// val = g_emu_op_para.pq_cur[id_thread][curPcsId];

		ret = SetLcdFun06(id_thread, regaddr, val);
	}
	break;

	case LCD_VSG_QW_PCS_MODE:
	{
		printf("LCD:%d VSG 无功参数设置 ...\n", id_thread);
		unsigned short regaddr; // = pq_pcspw_set[curPcsId][curTaskId];
		unsigned short val;

		regaddr = vsgpcs_qw_set[curPcsId];
		val = g_emu_op_para.vsg_qw[id_thread][curPcsId];

		ret = SetLcdFun06(id_thread, regaddr, val);
	}
	break;

		// case LCD_VSG_QW_PCS_MODE:
		// {
		// 	printf("VSG 无功参数设置 ...\n");
		// 	unsigned short regaddr; // = pq_pcspw_set[curPcsId][curTaskId];
		// 	unsigned short val;

		// 	regaddr = vsgpcs_qw_set[curPcsId];
		// 	val = g_emu_op_para.vsg_qw[id_thread][curPcsId];

		// 	ret = SetLcdFun06(id_thread, regaddr, val);
		// }
		// break;

	default:
		break;
	}
	if (ret == 0)
		wait_flag = 1;
}

void *Modbus_clientSend_thread(void *arg) // 25
{

	int id_thread = (int)arg;

	int ret_value = 0;
	msgClient pmsg;
	MyData pcsdata;
	int waittime = 0;
	int id_frame;

	printf("PCS[%d] Modbus_clientSend_thread  is Starting!\n", id_thread);
	key_t key = 0;
	g_comm_qmegid[id_thread] = os_create_msgqueue(&key, 1);

	// unsigned char code_fun[] = {0x03, 0x06, 0x10};
	// unsigned char errid_fun[] = {0x83, 0x86, 0x90};

	while (modbus_sockt_state[id_thread] == STATUS_OFF)
	{
		usleep(10000);
	}

	wait_flag = 0;

	// printf("modbus_sockt_state[id_thread] == STATUS_ON\n") ;
	while (modbus_sockt_state[id_thread] == STATUS_ON) //
	{
		// printf("wait_flag:%d\n", wait_flag);
		ret_value = os_rev_msgqueue(g_comm_qmegid[id_thread], &pmsg, sizeof(msgClient), 0, 100);
		if (ret_value >= 0)
		{
			waittime = 0;
			memcpy((char *)&pcsdata, pmsg.data, sizeof(MyData));

			id_frame = pcsdata.buf[0] * 256 + pcsdata.buf[1];

			if ((id_frame != 0xffff && (g_num_frame - 1) == id_frame) || (id_frame == 0xffff && g_num_frame == 1))
			{
				printf("recv form pcs!!!!!g_num_frame=%d  id_frame=%d\n", g_num_frame, id_frame);
				int res = AnalysModbus(id_thread, pcsdata.buf, pcsdata.len);
				if (0 == res)
				{
					printf("数据解析成功！！！\n");
				}
			}
			else
				printf("检查是否发生丢包现象！！！！！g_num_frame=%d  id_frame=%d\n", g_num_frame, id_frame);
			wait_flag = 0;
			continue;
		}

		if (wait_flag == 1)
		{
			waittime++;
			if (waittime == 1000)
			{
				wait_flag = 0;
				waittime = 0;
			}
			continue;
		}

		RunAccordingtoStatus(id_thread);
	}
	return NULL;
}

static int recvFrame(int fd, int qid, MyData *recvbuf)
{
	int readlen;

	// int index = 0, length = 0;
	//  int i;
	msgClient msg;
	// MyData *precv = (MyData *)&msg.data;
	readlen = recv(fd, recvbuf->buf, MAX_MODBUS_FLAME, 0);
	//		readlen = recv(fd, (recvbuf.buf + recvbuf.len),
	//				(MAX_BUF_SIZE - recvbuf.len), 0);
	//		printf("*****  F:%s L:%d recv readlen=%d\n", __FUNCTION__, __LINE__,	readlen);
	if (readlen < 0)
	{
		printf("连接断开或异常\r\n");
		return -1;
	}
	else if (readlen == 0)
		return 1;

	printf("收到一包数据 wait_flag=%d", wait_flag);
	recvbuf->len = readlen;
	myprintbuf(readlen, recvbuf->buf);
	msg.msgtype = 1;
	memcpy((char *)&msg.data, recvbuf->buf, readlen);
	sleep(1);
	if (msgsnd(qid, &msg, sizeof(msgClient), IPC_NOWAIT) != -1)
	{

		printf("succ succ succ succ !!!!!!!"); //连接主站的网络参数I
		return 0;
	}
	else
	{
		return 1;
	}

	// for(i=0;i<readlen;i++)
	// 	printf("0x%2x ",recvbuf->buf[i]);
	// printf("\n");
}

//参数初始化
void init_emu_op_para(int id_thread)
{
	int i;
	// LCD
	g_emu_op_para.ems_commnication_status = OFF_LINE;
	g_emu_op_para.ifNeedResetLcdOp[id_thread] = _NEED_RESET;
	g_emu_op_para.LcdOperatingMode[id_thread] = PQ;
	g_emu_op_para.LcdStatus[id_thread] = STATUS_OFF;
	g_send_data[id_thread].flag_waiting = 0;
	g_emu_op_para.vsg_mode[id_thread] = VSG_PQ_PP;

	// PCS
	for (i = 0; i < MAX_PCS_NUM; i++)
	{
		// PQ
		g_emu_op_para.pq_mode[id_thread][i] = PQ_STP;
		g_emu_op_para.pq_pw[id_thread][i] = 500;  // 50.0kW
		g_emu_op_para.pq_cur[id_thread][i] = 500; // 50.0A

		// VSG
		g_emu_op_para.vsg_pw[id_thread][i] = 50; // 50.0kW
		g_emu_op_para.vsg_qw[id_thread][i] = 0;	 // kVar
	}

	g_flag_RecvNeed_LCD = countRecvFlag(pPara_Modtcp->lcdnum);
}

void *Modbus_clientRecv_thread(void *arg) // 25
{
	int id_thread = (int)arg;
	int fd = -1;
	fd_set maxFd;
	struct timeval tv;
	int ret;
	int i = 0, jj = 0;
	MyData recvbuf;
	printf("PCS[%d] Modbus_clientRecv_thread is Starting!\n", id_thread);

	//	printf("network parameters  connecting to server IP=%s   port=%d\n", pPara_Modtcp->server_ip[id_thread], pPara_Modtcp->server_port[id_thread]); //
	_SERVER_SOCKET server_sock;
	server_sock.protocol = TCP;
	server_sock.port = htons(pPara_Modtcp->server_port);
	server_sock.addr = inet_addr(pPara_Modtcp->server_ip);
	server_sock.fd = -1;

	sleep(4);
loop:
	while (1)
	{
		server_sock.fd = -1;
		if (_socket_client_init(&server_sock) != 0)
		{
			sleep(10);
		}
		else
			break;
	}
	printf("连接服务器成功！！！！\n");

	modbus_client_sockptr[id_thread] = server_sock.fd;
	modbus_sockt_state[id_thread] = STATUS_ON;
	init_emu_op_para(id_thread);
	// >>>>>>> db5448e3e13a7539dcb9a4a0240a049b602dcd2b

	jj = 0; //未接收到数据累计标志，大于1000清零
	i = 0;

	while (1)
	{
		fd = modbus_client_sockptr[id_thread];
		if (fd == -1)
			break;
		FD_ZERO(&maxFd);
		FD_SET(fd, &maxFd);
		tv.tv_sec = 0;
		//    tv.tv_usec = 50000;
		tv.tv_usec = 200000;
		ret = select(fd + 1, &maxFd, NULL, NULL, &tv);
		if (ret < 0)
		{

			printf("网络有问题！！！！！！！！！！！！");
			break;
		}
		else if (ret == 0)
		{
			jj++;

			if (jj > 1000)
			{
				printf("暂时没有数据传入！！！！未接收到数据次数=%d！！！！！！！！！！！！！！！！\r\n", jj);
				jj = 0;

				//				break;
			}
			continue;
		}
		else
		{

			jj = 0;

			// printf("貌似收到数据！！！！！！！！！！！！");
			if (FD_ISSET(fd, &maxFd))
			{
				ret = recvFrame(fd, g_comm_qmegid[id_thread], &recvbuf);
				if (ret == -1)
				{
					i++;

					if (i > 30)
					{
						printf("接收不成功！！！！！！！！！！！！！！！！i=%d\r\n", i);
						break;
					}
					else
						continue;
				}
				else if (ret == 1)
				{
					//                 i++;

					// if(i>30)
					// {
					// 	printf("接收数据长度为0！！！！！！！！！！！！！！！！\r\n");

					// 	i=0;

					// }
					continue;
				}
				else
				{
					i = 0;
					printf("接收成功！！！！！！！！！！！！！！！！wait_flag=%d modbus_sockt_state[id_thread]=%d\r\n", wait_flag, modbus_sockt_state[id_thread]);
				}
			}
			else
			{
				printf("未知错误////////////////////////////////r/n");
				break;
			}
		}
	}
	modbus_sockt_state[id_thread] = STATUS_OFF;
	printf("网络断开，重连！！！！");
	goto loop;
}

void CreateThreads(void)
{
	pthread_t ThreadID;
	pthread_attr_t Thread_attr;
	int i;
	printf("pPara_Modtcp lcd数量:%d\n", pPara_Modtcp->lcdnum);

	for (i = 0; i < pPara_Modtcp->lcdnum; i++)
	{
		pPara_Modtcp->pcsnum[i] = 0;
		pPara_Modtcp->devNo[i] = 0xa;
		modbus_sockt_state[i] = STATUS_OFF;
		if (FAIL == CreateSettingThread(&ThreadID, &Thread_attr, (void *)Modbus_clientRecv_thread, (int *)i, 1, 1))
		{
			printf("MODBUS CONNECT THTREAD CREATE ERR!\n");

			exit(1);
		}
		if (FAIL == CreateSettingThread(&ThreadID, &Thread_attr, (void *)Modbus_clientSend_thread, (int *)i, 1, 1))
		{
			printf("MODBUS THTREAD CREATE ERR!\n");
			exit(1);
		}
	}
	// initInterface61850();
	//  bams_Init();
	printf("MODBUS THTREAD CREATE success!\n");
}
