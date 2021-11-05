/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "os.h"
#include "tworker.h"
#include "tglobal.h"
#include "mnodeMnode.h"
#include "mnodeSdb.h"
#include "mnodeShow.h"
#include "mnodeSync.h"
#include "mnodeWorker.h"

static struct {
  SWorkerPool read;
  SWorkerPool write;
  SWorkerPool peerReq;
  SWorkerPool peerRsp;
  taos_queue        readQ;
  taos_queue        writeQ;
  taos_queue        peerReqQ;
  taos_queue        peerRspQ;
  int32_t (*writeMsgFp[TSDB_MSG_TYPE_MAX])(SMnMsg *);
  int32_t (*readMsgFp[TSDB_MSG_TYPE_MAX])(SMnMsg *);
  int32_t (*peerReqFp[TSDB_MSG_TYPE_MAX])(SMnMsg *);
  void (*peerRspFp[TSDB_MSG_TYPE_MAX])(SRpcMsg *);
  void (*msgFp[TSDB_MSG_TYPE_MAX])(SRpcMsg *pMsg);
} tsMworker = {0};

static SMnMsg *mnodeInitMsg2(SRpcMsg *pRpcMsg) {
  int32_t    size = sizeof(SMnMsg) + pRpcMsg->contLen;
  SMnMsg *pMsg = taosAllocateQitem(size);

  pMsg->rpcMsg = *pRpcMsg;
  pMsg->rpcMsg.pCont = pMsg->pCont;
  pMsg->createdTime = taosGetTimestampSec();
  memcpy(pMsg->pCont, pRpcMsg->pCont, pRpcMsg->contLen);

  SRpcConnInfo connInfo = {0};
  if (rpcGetConnInfo(pMsg->rpcMsg.handle, &connInfo) == 0) {
    pMsg->pUser = sdbGetRow(MN_SDB_USER, connInfo.user);
  }

  if (pMsg->pUser == NULL) {
    mError("can not get user from conn:%p", pMsg->rpcMsg.handle);
    taosFreeQitem(pMsg);
    return NULL;
  }

  return pMsg;
}

static void mnodeCleanupMsg2(SMnMsg *pMsg) {
  if (pMsg == NULL) return;
  if (pMsg->rpcMsg.pCont != pMsg->pCont) {
    tfree(pMsg->rpcMsg.pCont);
  }

  taosFreeQitem(pMsg);
}

static void mnodeDispatchToWriteQueue(SRpcMsg *pRpcMsg) {
  if (mnodeGetStatus() != MN_STATUS_READY || tsMworker.writeQ == NULL) {
    mnodeSendRedirectMsg(pRpcMsg, true);
  } else {
    SMnMsg *pMsg = mnodeInitMsg2(pRpcMsg);
    if (pMsg == NULL) {
      SRpcMsg rpcRsp = {.handle = pRpcMsg->handle, .code = TSDB_CODE_MND_INVALID_USER};
      rpcSendResponse(&rpcRsp);
    } else {
      mTrace("msg:%p, app:%p type:%s is put into wqueue", pMsg, pMsg->rpcMsg.ahandle, taosMsg[pMsg->rpcMsg.msgType]);
      taosWriteQitem(tsMworker.writeQ, pMsg);
    }
  }

  rpcFreeCont(pRpcMsg->pCont);
}

void mnodeReDispatchToWriteQueue(SMnMsg *pMsg) {
  if (mnodeGetStatus() != MN_STATUS_READY || tsMworker.writeQ == NULL) {
    mnodeSendRedirectMsg(&pMsg->rpcMsg, true);
    mnodeCleanupMsg2(pMsg);
  } else {
    taosWriteQitem(tsMworker.writeQ, pMsg);
  }
}

static void mnodeDispatchToReadQueue(SRpcMsg *pRpcMsg) {
  if (mnodeGetStatus() != MN_STATUS_READY || tsMworker.readQ == NULL) {
    mnodeSendRedirectMsg(pRpcMsg, true);
  } else {
    SMnMsg *pMsg = mnodeInitMsg2(pRpcMsg);
    if (pMsg == NULL) {
      SRpcMsg rpcRsp = {.handle = pRpcMsg->handle, .code = TSDB_CODE_MND_INVALID_USER};
      rpcSendResponse(&rpcRsp);
    } else {
      mTrace("msg:%p, app:%p type:%s is put into rqueue", pMsg, pMsg->rpcMsg.ahandle, taosMsg[pMsg->rpcMsg.msgType]);
      taosWriteQitem(tsMworker.readQ, pMsg);
    }
  }

  rpcFreeCont(pRpcMsg->pCont);
}

static void mnodeDispatchToPeerQueue(SRpcMsg *pRpcMsg) {
  if (mnodeGetStatus() != MN_STATUS_READY || tsMworker.peerReqQ == NULL) {
    mnodeSendRedirectMsg(pRpcMsg, false);
  } else {
    SMnMsg *pMsg = mnodeInitMsg2(pRpcMsg);
    if (pMsg == NULL) {
      SRpcMsg rpcRsp = {.handle = pRpcMsg->handle, .code = TSDB_CODE_MND_INVALID_USER};
      rpcSendResponse(&rpcRsp);
    } else {
      mTrace("msg:%p, app:%p type:%s is put into peer req queue", pMsg, pMsg->rpcMsg.ahandle,
             taosMsg[pMsg->rpcMsg.msgType]);
      taosWriteQitem(tsMworker.peerReqQ, pMsg);
    }
  }

  rpcFreeCont(pRpcMsg->pCont);
}

void mnodeDispatchToPeerRspQueue(SRpcMsg *pRpcMsg) {
  SMnMsg *pMsg = mnodeInitMsg2(pRpcMsg);
  if (pMsg == NULL) {
    SRpcMsg rpcRsp = {.handle = pRpcMsg->handle, .code = TSDB_CODE_MND_INVALID_USER};
    rpcSendResponse(&rpcRsp);
  } else {
    mTrace("msg:%p, app:%p type:%s is put into peer rsp queue", pMsg, pMsg->rpcMsg.ahandle,
           taosMsg[pMsg->rpcMsg.msgType]);
    taosWriteQitem(tsMworker.peerRspQ, pMsg);
  }

  // rpcFreeCont(pRpcMsg->pCont);
}

void mnodeSendRsp(SMnMsg *pMsg, int32_t code) {
  if (pMsg == NULL) return;
  if (code == TSDB_CODE_MND_ACTION_IN_PROGRESS) return;
  if (code == TSDB_CODE_MND_ACTION_NEED_REPROCESSED) {
    mnodeReDispatchToWriteQueue(pMsg);
    return;
  }

  SRpcMsg rpcRsp = {
      .handle = pMsg->rpcMsg.handle,
      .pCont = pMsg->rpcRsp.rsp,
      .contLen = pMsg->rpcRsp.len,
      .code = code,
  };

  rpcSendResponse(&rpcRsp);
  mnodeCleanupMsg2(pMsg);
}

static void mnodeInitMsgFp() {
//   // peer req
//   tsMworker.msgFp[TSDB_MSG_TYPE_DM_CONFIG_TABLE] = mnodeDispatchToPeerQueue;
//   tsMworker.peerReqFp[TSDB_MSG_TYPE_DM_CONFIG_TABLE] = mnodeProcessTableCfgMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_DM_CONFIG_VNODE] = mnodeDispatchToPeerQueue;
//   tsMworker.peerReqFp[TSDB_MSG_TYPE_DM_CONFIG_VNODE] = mnodeProcessVnodeCfgMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_AUTH] = mnodeDispatchToPeerQueue;
//   tsMworker.peerReqFp[TSDB_MSG_TYPE_AUTH] = mnodeProcessAuthMsg;
//   // tsMworker.msgFp[TSDB_MSG_TYPE_GRANT] = mnodeDispatchToPeerQueue;
//   // tsMworker.peerReqFp[TSDB_MSG_TYPE_GRANT] = grantProcessMsgInMgmt;
//   tsMworker.msgFp[TSDB_MSG_TYPE_STATUS] = mnodeDispatchToPeerQueue;
//   tsMworker.peerReqFp[TSDB_MSG_TYPE_STATUS] = mnodeProcessDnodeStatusMsg;

//   // peer rsp
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_CONFIG_DNODE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_CONFIG_DNODE_RSP] = mnodeProcessCfgDnodeMsgRsp;

//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_DROP_STABLE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_DROP_STABLE_RSP] = mnodeProcessDropSuperTableRsp;
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_CREATE_TABLE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_CREATE_TABLE_RSP] = mnodeProcessCreateChildTableRsp;
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_DROP_TABLE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_DROP_TABLE_RSP] = mnodeProcessDropChildTableRsp;
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_ALTER_TABLE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_ALTER_TABLE_RSP] = mnodeProcessAlterTableRsp;

//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_CREATE_VNODE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_CREATE_VNODE_RSP] = mnodeProcessCreateVnodeRsp;
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_ALTER_VNODE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_ALTER_VNODE_RSP] = mnodeProcessAlterVnodeRsp;
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_COMPACT_VNODE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_COMPACT_VNODE_RSP] = mnodeProcessCompactVnodeRsp;
//   tsMworker.msgFp[TSDB_MSG_TYPE_MD_DROP_VNODE_RSP] = mnodeDispatchToPeerRspQueue;
//   tsMworker.peerRspFp[TSDB_MSG_TYPE_MD_DROP_VNODE_RSP] = mnodeProcessDropVnodeRsp;

//   // read msg
//   tsMworker.msgFp[TSDB_MSG_TYPE_HEARTBEAT] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_HEARTBEAT] = mnodeProcessHeartBeatMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_CONNECT] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_CONNECT] = mnodeProcessConnectMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_USE_DB] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_USE_DB] = mnodeProcessUseMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_TABLE_META] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_TABLE_META] = mnodeProcessTableMetaMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_TABLES_META] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_TABLES_META] = mnodeProcessMultiTableMetaMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_STABLE_VGROUP] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_STABLE_VGROUP] = mnodeProcessSuperTableVgroupMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_SHOW] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_SHOW] = mnodeProcessShowMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_SHOW_RETRIEVE] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_SHOW_RETRIEVE] = mnodeProcessRetrieveMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_RETRIEVE_FUNC] = mnodeDispatchToReadQueue;
//   tsMworker.readMsgFp[TSDB_MSG_TYPE_RETRIEVE_FUNC] = mnodeProcessRetrieveFuncReq;

//   // tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_ACCT] = mnodeDispatchToWriteQueue;
//   // tsMworker.readMsgFp[TSDB_MSG_TYPE_CREATE_ACCT] = acctProcessCreateAcctMsg;
//   // tsMworker.msgFp[TSDB_MSG_TYPE_ALTER_ACCT] = mnodeDispatchToWriteQueue;
//   // tsMworker.readMsgFp[TSDB_MSG_TYPE_ALTER_ACCT] = acctProcessDropAcctMsg;
//   // tsMworker.msgFp[TSDB_MSG_TYPE_DROP_ACCT] = mnodeDispatchToWriteQueue;
//   // tsMworker.readMsgFp[TSDB_MSG_TYPE_DROP_ACCT] = acctProcessAlterAcctMsg;

//   // write msg
//   tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_USER] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_CREATE_USER] = mnodeProcessCreateUserMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_ALTER_USER] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_ALTER_USER] = mnodeProcessAlterUserMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_DROP_USER] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_DROP_USER] = mnodeProcessDropUserMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_DNODE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_CREATE_DNODE] = mnodeProcessCreateDnodeMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_DROP_DNODE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_DROP_DNODE] = mnodeProcessDropDnodeMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_CONFIG_DNODE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_CONFIG_DNODE] = mnodeProcessCfgDnodeMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_DB] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_CREATE_DB] = mnodeProcessCreateDbMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_ALTER_DB] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_ALTER_DB] = mnodeProcessAlterDbMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_DROP_DB] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_DROP_DB] = mnodeProcessDropDbMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_SYNC_DB] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_SYNC_DB] = mnodeProcessSyncDbMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_COMPACT_VNODE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_COMPACT_VNODE] = mnodeProcessCompactMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_FUNCTION] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_CREATE_FUNCTION] = mnodeProcessCreateFuncMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_DROP_FUNCTION] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_DROP_FUNCTION] = mnodeProcessDropFuncMsg;

//   // tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_TP] = mnodeDispatchToWriteQueue;
//   // tsMworker.readMsgFp[TSDB_MSG_TYPE_CREATE_TP] = tpProcessCreateTpMsg;
//   // tsMworker.msgFp[TSDB_MSG_TYPE_DROP_TP] = mnodeDispatchToWriteQueue;
//   // tsMworker.readMsgFp[TSDB_MSG_TYPE_DROP_TP] = tpProcessAlterTpMsg;
//   // tsMworker.msgFp[TSDB_MSG_TYPE_ALTER_TP] = mnodeDispatchToWriteQueue;
//   // tsMworker.readMsgFp[TSDB_MSG_TYPE_ALTER_TP] = tpProcessDropTpMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_CREATE_TABLE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_CREATE_TABLE] = mnodeProcessCreateTableMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_DROP_TABLE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_DROP_TABLE] = mnodeProcessDropTableMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_ALTER_TABLE] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_ALTER_TABLE] = mnodeProcessAlterTableMsg;

//   tsMworker.msgFp[TSDB_MSG_TYPE_ALTER_STREAM] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_ALTER_STREAM] = NULL;
//   tsMworker.msgFp[TSDB_MSG_TYPE_KILL_QUERY] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_KILL_QUERY] = mnodeProcessKillQueryMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_KILL_STREAM] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_KILL_STREAM] = mnodeProcessKillStreamMsg;
//   tsMworker.msgFp[TSDB_MSG_TYPE_KILL_CONN] = mnodeDispatchToWriteQueue;
//   tsMworker.writeMsgFp[TSDB_MSG_TYPE_KILL_CONN] = mnodeProcessKillConnectionMsg;
}

static void mnodeProcessWriteReq(SMnMsg *pMsg, void *unused) {
  int32_t msgType = pMsg->rpcMsg.msgType;
  void   *ahandle = pMsg->rpcMsg.ahandle;
  int32_t code = 0;

  if (pMsg->rpcMsg.pCont == NULL) {
    mError("msg:%p, app:%p type:%s content is null", pMsg, ahandle, taosMsg[msgType]);
    code = TSDB_CODE_MND_INVALID_MSG_LEN;
    goto PROCESS_WRITE_REQ_END;
  }

  if (!mnodeIsMaster()) {
    SMnRsp *rpcRsp = &pMsg->rpcRsp;
    SEpSet *epSet = rpcMallocCont(sizeof(SEpSet));
    mnodeGetMnodeEpSetForShell(epSet, true);
    rpcRsp->rsp = epSet;
    rpcRsp->len = sizeof(SEpSet);

    mDebug("msg:%p, app:%p type:%s in write queue, is redirected, numOfEps:%d inUse:%d", pMsg, ahandle,
           taosMsg[msgType], epSet->numOfEps, epSet->inUse);

    code = TSDB_CODE_RPC_REDIRECT;
    goto PROCESS_WRITE_REQ_END;
  }

  if (tsMworker.writeMsgFp[msgType] == NULL) {
    mError("msg:%p, app:%p type:%s not processed", pMsg, ahandle, taosMsg[msgType]);
    code = TSDB_CODE_MND_MSG_NOT_PROCESSED;
    goto PROCESS_WRITE_REQ_END;
  }

  code = (*tsMworker.writeMsgFp[msgType])(pMsg);

PROCESS_WRITE_REQ_END:
  mnodeSendRsp(pMsg, code);
}

static void mnodeProcessReadReq(SMnMsg *pMsg, void *unused) {
  int32_t msgType = pMsg->rpcMsg.msgType;
  void   *ahandle = pMsg->rpcMsg.ahandle;
  int32_t code = 0;

  if (pMsg->rpcMsg.pCont == NULL) {
    mError("msg:%p, app:%p type:%s in mread queue, content is null", pMsg, ahandle, taosMsg[msgType]);
    code = TSDB_CODE_MND_INVALID_MSG_LEN;
    goto PROCESS_READ_REQ_END;
  }

  if (!mnodeIsMaster()) {
    SMnRsp *rpcRsp = &pMsg->rpcRsp;
    SEpSet *epSet = rpcMallocCont(sizeof(SEpSet));
    if (!epSet) {
      code = TSDB_CODE_MND_OUT_OF_MEMORY;
      goto PROCESS_READ_REQ_END;
    }
    mnodeGetMnodeEpSetForShell(epSet, true);
    rpcRsp->rsp = epSet;
    rpcRsp->len = sizeof(SEpSet);

    mDebug("msg:%p, app:%p type:%s in mread queue is redirected, numOfEps:%d inUse:%d", pMsg, ahandle, taosMsg[msgType],
           epSet->numOfEps, epSet->inUse);
    code = TSDB_CODE_RPC_REDIRECT;
    goto PROCESS_READ_REQ_END;
  }

  if (tsMworker.readMsgFp[msgType] == NULL) {
    mError("msg:%p, app:%p type:%s in mread queue, not processed", pMsg, ahandle, taosMsg[msgType]);
    code = TSDB_CODE_MND_MSG_NOT_PROCESSED;
    goto PROCESS_READ_REQ_END;
  }

  mTrace("msg:%p, app:%p type:%s will be processed in mread queue", pMsg, ahandle, taosMsg[msgType]);
  code = (*tsMworker.readMsgFp[msgType])(pMsg);

PROCESS_READ_REQ_END:
  mnodeSendRsp(pMsg, code);
}

static void mnodeProcessPeerReq(SMnMsg *pMsg, void *unused) {
  int32_t msgType = pMsg->rpcMsg.msgType;
  void   *ahandle = pMsg->rpcMsg.ahandle;
  int32_t code = 0;

  if (pMsg->rpcMsg.pCont == NULL) {
    mError("msg:%p, ahandle:%p type:%s in mpeer queue, content is null", pMsg, ahandle, taosMsg[msgType]);
    code = TSDB_CODE_MND_INVALID_MSG_LEN;
    goto PROCESS_PEER_REQ_END;
  }

  if (!mnodeIsMaster()) {
    SMnRsp *rpcRsp = &pMsg->rpcRsp;
    SEpSet *epSet = rpcMallocCont(sizeof(SEpSet));
    mnodeGetMnodeEpSetForPeer(epSet, true);
    rpcRsp->rsp = epSet;
    rpcRsp->len = sizeof(SEpSet);

    mDebug("msg:%p, ahandle:%p type:%s in mpeer queue is redirected, numOfEps:%d inUse:%d", pMsg, ahandle,
           taosMsg[msgType], epSet->numOfEps, epSet->inUse);

    code = TSDB_CODE_RPC_REDIRECT;
    goto PROCESS_PEER_REQ_END;
  }

  if (tsMworker.peerReqFp[msgType] == NULL) {
    mError("msg:%p, ahandle:%p type:%s in mpeer queue, not processed", pMsg, ahandle, taosMsg[msgType]);
    code = TSDB_CODE_MND_MSG_NOT_PROCESSED;
    goto PROCESS_PEER_REQ_END;
  }

  code = (*tsMworker.peerReqFp[msgType])(pMsg);

PROCESS_PEER_REQ_END:
  mnodeSendRsp(pMsg, code);
}

static void mnodeProcessPeerRsp(SMnMsg *pMsg, void *unused) {
  int32_t  msgType = pMsg->rpcMsg.msgType;
  SRpcMsg *pRpcMsg = &pMsg->rpcMsg;

  if (!mnodeIsMaster()) {
    mError("msg:%p, ahandle:%p type:%s not processed for not master", pRpcMsg, pRpcMsg->ahandle, taosMsg[msgType]);
    mnodeCleanupMsg2(pMsg);
  }

  if (tsMworker.peerRspFp[msgType]) {
    (*tsMworker.peerRspFp[msgType])(pRpcMsg);
  } else {
    mError("msg:%p, ahandle:%p type:%s is not processed", pRpcMsg, pRpcMsg->ahandle, taosMsg[msgType]);
  }

  mnodeCleanupMsg2(pMsg);
}

int32_t mnodeInitWorker() {
  mnodeInitMsgFp();

  SWorkerPool *pPool = &tsMworker.write;
  pPool->name = "mnode-write";
  pPool->min = 1;
  pPool->max = 1;
  if (tWorkerInit(pPool) != 0) {
    return TSDB_CODE_MND_OUT_OF_MEMORY;
  } else {
    tsMworker.writeQ = tWorkerAllocQueue(pPool, NULL, (FProcessItem)mnodeProcessWriteReq);
  }

  pPool = &tsMworker.read;
  pPool->name = "mnode-read";
  pPool->min = 2;
  pPool->max = (int32_t)(tsNumOfCores * tsNumOfThreadsPerCore / 2);
  pPool->max = MAX(2, pPool->max);
  pPool->max = MIN(4, pPool->max);
  if (tWorkerInit(pPool) != 0) {
    return TSDB_CODE_MND_OUT_OF_MEMORY;
  } else {
    tsMworker.readQ = tWorkerAllocQueue(pPool, NULL, (FProcessItem)mnodeProcessReadReq);
  }

  pPool = &tsMworker.peerReq;
  pPool->name = "mnode-peer-req";
  pPool->min = 1;
  pPool->max = 1;
  if (tWorkerInit(pPool) != 0) {
    return TSDB_CODE_MND_OUT_OF_MEMORY;
  } else {
    tsMworker.peerReqQ = tWorkerAllocQueue(pPool, NULL, (FProcessItem)mnodeProcessPeerReq);
  }

  pPool = &tsMworker.peerRsp;
  pPool->name = "mnode-peer-rsp";
  pPool->min = 1;
  pPool->max = 1;
  if (tWorkerInit(pPool) != 0) {
    return TSDB_CODE_MND_OUT_OF_MEMORY;
  } else {
    tsMworker.peerRspQ = tWorkerAllocQueue(pPool, NULL, (FProcessItem)mnodeProcessPeerRsp);
  }

  mInfo("mnode worker is initialized");
  return 0;
}

void mnodeCleanupWorker() {
  tWorkerFreeQueue(&tsMworker.write, tsMworker.writeQ);
  tWorkerCleanup(&tsMworker.write);
  tsMworker.writeQ = NULL;

  tWorkerFreeQueue(&tsMworker.read, tsMworker.readQ);
  tWorkerCleanup(&tsMworker.read);
  tsMworker.readQ = NULL;

  tWorkerFreeQueue(&tsMworker.peerReq, tsMworker.peerReqQ);
  tWorkerCleanup(&tsMworker.peerReq);
  tsMworker.peerReqQ = NULL;

  tWorkerFreeQueue(&tsMworker.peerRsp, tsMworker.peerRspQ);
  tWorkerCleanup(&tsMworker.peerRsp);
  tsMworker.peerRspQ = NULL;

  mInfo("mnode worker is closed");
}

SMnodeMsg *mnodeInitMsg(int32_t msgNum) { return NULL; }

int32_t mnodeAppendMsg(SMnodeMsg *pMsg, SRpcMsg *pRpcMsg) { return 0; }

void mnodeCleanupMsg(SMnodeMsg *pMsg) {}
void mnodeProcessMsg(SMnodeMsg *pMsg, EMnMsgType msgType) {}
