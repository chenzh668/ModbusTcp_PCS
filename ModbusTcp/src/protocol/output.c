#include "output.h"
#include <stdio.h>
#include "modbus_tcp_main.h"
#include "modbus.h"
#include <sys/mman.h>
#include <string.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include "logicAndControl.h"
//所有的LCD整机信息数据数据标识1都用2来表示，#
//数据标识2编号从1-6，
//每个LCD下模块信息，数据标识1都3来表示，
//数据标识2编号从1-36，每个LCD模块信息占用6个编号，LCD1模块信息数据标识2从1-6，LCD2模块信息数据标识2从7-12，
// LCD3模块信息数据标识2从13-18，LCD4模块信息数据标识2从19-24，LCD5模块信息数据标识2从25-30，LCD6模块信息数据标识2从31-36.

LCD_YC_YX_DATA g_YcData[MAX_PCS_NUM * MAX_LCD_NUM];
// libName:订阅数据的模块； type：数据类型 ；ifcomp：是否与上次数据比较
LCD_YC_YX_DATA g_YxData[MAX_PCS_NUM * MAX_LCD_NUM];

LCD_YC_YX_DATA g_ZjyxData;
LCD_YC_YX_DATA g_ZjycData;

// static void outputdata(unsigned char libName,unsigned char type,unsigned char ifcomp)
static void outputdata(unsigned char type, int id)
{
	post_list_t *pnote = post_list_l;

	while (pnote != NULL)
	{
		if (pnote->type != type)
		{

			if (pnote->next != NULL)
			{
				pnote = pnote->next;
				continue;
			}
			else
				return;
		}
		switch (type)
		{
		case _YC_:
		{
			printf("g_YcData[id-1].sn=%d g_YcData[id-1].pcsid=%d\n", g_YcData[id - 1].sn, g_YcData[id - 1].pcsid);
			pnote->pfun(type, (void *)&g_YcData[id - 1]);
		}

		break;
		case _ZJYC_:
		{
			pnote->pfun(type, (void *)&g_YcData[id - 1]);
		}

		break;
		case _YX_:
		{
			pnote->pfun(type, (void *)&g_YxData[id - 1]);
		}

		break;

		default:
			break;
		}

		pnote = pnote->next;
	}
}

int SaveYcData(int id_thread, int pcsid, unsigned short *pyc, unsigned char len)
{

	int id = 0;
	int i;

	for (i = 0; i < id_thread; i++)
	{
		id += pPara_Modtcp->pcsnum[i];
	}
	id += pcsid;
	// printf("saveYcData id_thread=%d pcsid=%d id=%d num=%d\n", id_thread, pcsid, id, len);

	//  if(memcmp((char*)g_YcData[id].pcs_data,(char*)pyc,len))
	{
		g_YcData[id - 1].sn = id - 1;
		g_YcData[id - 1].lcdid = id_thread;
		g_YcData[id - 1].pcsid = pcsid;
		g_YcData[id - 1].data_len = len;
		memcpy((char *)g_YcData[id - 1].pcs_data, (char *)pyc, len);
		outputdata(_YC_, id);
	}

	return 0;
}

int SaveYxData(int id_thread, int pcsid, unsigned short *pyx, unsigned char len)
{

	int id = 0;
	int i;
	for (i = 0; i < id_thread; i++)
	{
		id += pPara_Modtcp->pcsnum[i];
	}
	id += pcsid;
	printf("saveYxData id_thread=%d pcsid=%d id=%d num=%d\n", id_thread, pcsid, id, len);
	//  if(memcmp((char*)g_YxData[id].pcs_data,(char*)pyx,len))
	{
		g_YxData[id - 1].sn = id - 1;
		g_YxData[id - 1].lcdid = id_thread;
		g_YxData[id - 1].pcsid = pcsid;
		g_YxData[id - 1].data_len = len;
		memcpy((char *)g_YxData[id - 1].pcs_data, (char *)pyx, len);
		outputdata(_YX_, id);
	}

	return 0;
}
int SaveZjyxData(int id_thread, unsigned short *pzjyx, unsigned char len)
{

	if (memcmp((char *)g_ZjyxData.pcs_data, (char *)pzjyx, len))
	{
		g_ZjyxData.sn = 0xff;
		g_ZjyxData.lcdid = id_thread;
		g_ZjyxData.pcsid = 0;
		g_ZjyxData.data_len = len;
		memcpy((char *)g_ZjyxData.pcs_data, (char *)pzjyx, len);
		outputdata(_ZJYX_, 0);
	}

	return 0;
}
int SaveZjycData(int id_thread, unsigned short *pzjyc, unsigned char len)
{

	//  if(memcmp((char*)g_ZjycData.pcs_data,(char*)pzjyc,len))
	{
		g_ZjycData.sn = 0xff;
		g_ZjycData.lcdid = id_thread;
		g_ZjycData.pcsid = 0;
		g_ZjycData.data_len = len;
		memcpy((char *)g_ZjycData.pcs_data, (char *)pzjyc, len);
		outputdata(_ZJYC_, 0);
	}

	return 0;
}

PARA_61850 para_61850;
void initInterface61850(void)
{
#define LIB_61850_PATH "/usr/lib/libiec61850_1.so"
	typedef int (*p_initlcd)(void *);
	void *handle;
	char *error;
	int i;
	printf("initInterface61850\n");
	p_initlcd my_func = NULL;
	para_61850.lcdnum = pconfig->lcd_num;
	for (i = 0; i < MAX_PCS_NUM; i++)
	{
		para_61850.pcsnum[i] = 0;
	}

	for (i = 0; i < para_61850.lcdnum; i++)
	{
		para_61850.pcsnum[i] = pPara_Modtcp->pcsnum[i];
	}
	para_61850.balance_rate = pconfig->balance_rate;
	printf("传输到61850接口的参数 %d %d \n", para_61850.lcdnum, para_61850.balance_rate);
	handle = dlopen(LIB_61850_PATH, RTLD_LAZY);
	if (!handle)
	{
		fprintf(stderr, "%s\n", dlerror());
		exit(EXIT_FAILURE);
	}
	dlerror();

	*(void **)(&my_func) = dlsym(handle, "lib61850_main");

	if ((error = dlerror()) != NULL)
	{
		fprintf(stderr, "%s\n", error);
		exit(EXIT_FAILURE);
	}

	my_func((void *)&para_61850);
}