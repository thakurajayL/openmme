/*
 * Copyright 2019-present Open Networking Foundation
 * Copyright (c) 2003-2018, Great Software Laboratory Pvt. Ltd.
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "mme_app.h"
#include "ue_table.h"
#include "err_codes.h"
#include "message_queues.h"
#include "ipc_api.h"
#include "detach_stage1_info.h"


/************************************************************************
Current file : Stage 1  Dettach handler.
DETACH stages :

**************************************************************************/

/****Globals and externs ***/


extern struct UE_info * g_UE_list[];
extern int g_mme_hdlr_status;

static int g_Q_detachread_fd;
static int g_Q_S11_detach_fd;
static int g_Q_S6A_detach_fd;

/*Making global just to avoid stack passing*/

static char detachreq[S1AP_DETACHREQ_STAGE1_BUF_SIZE];
/* static char detach_resp[S11_DETACHRESP_STAGE2_BUF_SIZE]; * */

struct DS_Q_msg g_ds_msg;
struct s6a_purge_Q_msg g_purge_msg;

extern uint32_t detach_stage1_counter;

/****Global and externs end***/

/**
Initialize the stage settings, Q,
destination communication etc.
*/

static void
init_stage()
{
	log_msg(LOG_INFO, "Stage 1 Detach Reader - s1ap detach request : waiting\n");

	/*Reading for the detach signal from S1Ap */
	if (( g_Q_detachread_fd  = open_ipc_channel(S1AP_DTCHREQ_STAGE1_QUEUE,
						IPC_READ)) == -1){
		log_msg(LOG_ERROR, "Error in opening reader stage 1 Detach  S1AP IPC channel.\n");
		pthread_exit(NULL);
	}
	log_msg(LOG_INFO, "Stage 1 Detach reader - s1ap Detach complete: Connected\n");

	log_msg(LOG_INFO, "Stage 1 Detach  reader - s11 deta response: waiting\n");

	/*Writing the detach signal to S11 Queue*/
	if ((g_Q_S11_detach_fd  = open_ipc_channel(S11_DTCHREQ_STAGE1_QUEUE,
						IPC_WRITE)) == -1){
		log_msg(LOG_ERROR, "Error in opening reader stage 1 detach  S11 IPC channel.\n");
		pthread_exit(NULL);
	}
	log_msg(LOG_INFO, "Stage 1 Detach  reader - s11 detach response: Connected\n");


	/*Writing the detach signal to S6a Queue*/
	if ((g_Q_S6A_detach_fd  = open_ipc_channel(S6A_DTCHREQ_STAGE1_QUEUE,
					IPC_WRITE)) == -1){
		log_msg(LOG_ERROR, "Error in opening reader stage 1 detach  S6A IPC channel.\n");
		pthread_exit(NULL);
	}

	log_msg(LOG_INFO, "Stage 1 Detach  reader - s6a detach response: Connected\n");

#if 0
	/*Writing the detach signal to S1AP queue*/
	log_msg(LOG_INFO, "Stage 1 Detach  writer  - s11 detach Request: waiting\n");
	g_Q_S1AP_detch_fd = open_ipc_channel(S1AP_DTCHRES_STAGE1_QUEUE, IPC_WRITE);
	if (g_Q_MBreq_fd == -1) {
		log_msg(LOG_ERROR, "Error in opening Writer  stage 1 detach IPC channel:MB Request\n");
		pthread_exit(NULL);
	}
	log_msg(LOG_INFO, "Stage 1 Detach  writer - s11 detach request: Connected\n");

#endif

	return;
}

/**
* Read next message from stage Q for processing.
*/
static int
read_next_msg()
{
	int bytes_read=0;

	memset(detachreq, 0, S1AP_DETACHREQ_STAGE1_BUF_SIZE);
	while (bytes_read < S1AP_DETACHREQ_STAGE1_BUF_SIZE) {//TODO : Recheck condition
		if ((bytes_read = read_ipc_channel(
			g_Q_detachread_fd, detachreq,
			S1AP_DETACHREQ_STAGE1_BUF_SIZE))
			 == -1) {
			log_msg(LOG_ERROR, "Error in reading \n");
			/* TODO : Add proper error handling */
		}
		log_msg(LOG_INFO, "**************** Detach request Received - Len: %d\n",
			bytes_read);
	}

	return bytes_read;
}

/**
* Stage specific message processing.
*/
static int
detach_stage1_processing()
{
	struct detach_req_Q_msg *detach_req =
			(struct detach_req_Q_msg *) detachreq;
	struct UE_info *ue_entry = NULL;
    unsigned int ue_index = -1;
    if(detach_req->ue_idx != -1)
    {
	    ue_entry = GET_UE_ENTRY(detach_req->ue_idx);
        ue_index = detach_req->ue_idx;
    }
    else if (detach_req->ue_m_tmsi != -1)
    {
        ue_index = get_ue_index_from_tmsi(detach_req->ue_m_tmsi); 
        if(ue_index == -1)
        {
          log_msg(LOG_ERROR, "Invalid TMSI entry received = %d \n",detach_req->ue_m_tmsi);
          return E_FAIL;
        }
	    ue_entry = GET_UE_ENTRY(ue_index);

    }
    else
    {
        log_msg(LOG_ERROR, "detach message received without any valid identifier \n");
        return E_FAIL;
    }


	if((ue_entry == NULL) || 
       (UNASSIGNED_ENTRY == ue_entry->ue_state) || 
       (!IS_VALID_UE_INFO(ue_entry)))
    {
        log_msg(LOG_ERROR, "UE Entry invalid. Drop the packet. UE index %d , TMSI : %d\n", 
                 detach_req->ue_idx, detach_req->ue_m_tmsi);
        return E_FAIL;
    }

	log_msg(LOG_INFO, "Detach request received for ue %d\n", ue_index);
    ue_entry->ue_state = DETACH_STAGE1;
    ue_entry->ul_seq_no++;
	ue_entry->s1ap_enb_ue_id = detach_req->s1ap_enb_ue_id;

	g_ds_msg.bearer_id = ue_entry->bearer_id;

	memset(g_ds_msg.indication, 0, S11_DS_INDICATION_FLAG_SIZE);
	g_ds_msg.indication[0] = 8; /* TODO : define macro or enum */

	memcpy(&(g_ds_msg.s11_sgw_c_fteid),
			&(ue_entry->s11_sgw_c_fteid),
			sizeof(ue_entry->s11_sgw_c_fteid));
	g_purge_msg.ue_idx = ue_index;
	memcpy(g_purge_msg.IMSI, ue_entry->IMSI, BINARY_IMSI_LEN);

	return SUCCESS;
}

/**
* Post message to next handler of the stage
*/
static int
post_to_next()
{
	/* Post to s11-app */
	write_ipc_channel(g_Q_S11_detach_fd, (char *)&(g_ds_msg),
			S11_DTCHREQ_STAGE1_BUF_SIZE);
	log_msg(LOG_INFO, "Posted Delete Session message to s11-app.\n");

	write_ipc_channel(g_Q_S6A_detach_fd, (char *)&(g_purge_msg),
			  S6A_PURGEREQ_STAGE1_BUF_SIZE);
	log_msg(LOG_INFO, "Posted purge message to s6a-app.\n");

	detach_stage1_counter++;
	return SUCCESS;
}

/**
* Thread exit function for future reference.
static void
shutdown_detach_stage1()
{
	close_ipc_channel(g_Q_detachread_fd);
	close_ipc_channel(g_Q_S11_detach_fd);
	close_ipc_channel(g_Q_S1AP_detach_fd);
	log_msg(LOG_INFO, "Shutdown Stage 1 Detach handler \n");
	pthread_exit(NULL);
	return;
}
*/

/**
* Thread function for stage.
*/
void*
detach_stage1_mme_handler(void *data)
{
	init_stage();
	log_msg(LOG_INFO, "Detach Stage 1  Ready \n");
	g_mme_hdlr_status |= 1;
	g_mme_hdlr_status <<= 1;
	check_mme_hdlr_status();

	while(1){
		read_next_msg();

		if (detach_stage1_processing() == SUCCESS)
			post_to_next();
	}

	return NULL;
}
