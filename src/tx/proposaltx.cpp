// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "tx/proposaltx.h"
#include "main.h"
#include "entities/proposal.h"
#include <algorithm>

bool CheckIsGoverner(CRegID account, ProposalType proposalType, CCacheWrapper& cw ){

   if(proposalType == ProposalType::GOVERNER_UPDATE || proposalType == ProposalType::COIN_TRANSFER){
        VoteDelegateVector delegateList;
        if (!cw.delegateCache.GetActiveDelegates(delegateList)) {
            return false;
        }
        for(auto miner: delegateList){
            if(miner.regid == account)
                return true ;
        }
        return false ;

    } else{
        return cw.sysGovernCache.CheckIsGoverner(account) ;
    }

}

uint8_t GetNeedGovernerCount(ProposalType proposalType, CCacheWrapper& cw ){

    if(proposalType == ProposalType::GOVERNER_UPDATE){
        VoteDelegateVector delegateList;
        if (!cw.delegateCache.GetActiveDelegates(delegateList)) {
            return 8 ;
        }
        if(delegateList.size() == 11 )
            return 8 ;

        return ((delegateList.size()/3)*2+1) ;
    } else if( proposalType  == ProposalType::COIN_TRANSFER){
        VoteDelegateVector delegateList;
        if (!cw.delegateCache.GetActiveDelegates(delegateList)) {
            return 8 ;
        }
        return delegateList.size() ;
    } else {
        return cw.sysGovernCache.GetNeedGovernerCount();
    }
}


string CProposalCreateTx::ToString(CAccountDBCache &accountCache) {
    string proposalString = proposalBean.proposalPtr->ToString() ;
    return strprintf("txType=%s, hash=%s, ver=%d, %s, llFees=%ld, keyid=%s, valid_height=%d",
                     GetTxType(nTxType), GetHash().ToString(), nVersion, proposalString, llFees,
                     txUid.ToString(), valid_height);
}          // logging usage

Object CProposalCreateTx::ToJson(const CAccountDBCache &accountCache) const {

    Object result = CBaseTx::ToJson(accountCache);
    result.push_back(Pair("proposal", proposalBean.proposalPtr->ToJson()));

    return result;
}  // json-rpc usage

 bool CProposalCreateTx::CheckTx(CTxExecuteContext &context) {

     IMPLEMENT_DEFINE_CW_STATE
     IMPLEMENT_CHECK_TX_REGID_OR_PUBKEY(txUid);
     if (!CheckFee(context)) return false;

     if(!proposalBean.proposalPtr->CheckProposal(cw ,state)){
         return false ;
     }

     CAccount srcAccount;
     if (!cw.accountCache.GetAccount(txUid, srcAccount))
         return state.DoS(100, ERRORMSG("CProposalCreateTx::CheckTx, read account failed"), REJECT_INVALID,
                          "bad-getaccount");

     CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
     IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);
     return true ;
}


 bool CProposalCreateTx::ExecuteTx(CTxExecuteContext &context) {

     IMPLEMENT_DEFINE_CW_STATE

     CAccount srcAccount;
     if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
         return state.DoS(100, ERRORMSG("CProposalCreateTx::ExecuteTx, read source addr account info error"),
                          READ_ACCOUNT_FAIL, "bad-read-accountdb");
     }
     if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
         return state.DoS(100, ERRORMSG("CProposalCreateTx::ExecuteTx, account has insufficient funds"),
                          UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
     }

     if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
         return state.DoS(100, ERRORMSG("CProposalCreateTx::ExecuteTx, set account info error"),
                          WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

     uint64_t expireBlockCount ;

     if(!cw.sysParamCache.GetParam(PROPOSAL_EXPIRE_BLOCK_COUNT, expireBlockCount)) {
         return state.DoS(100, ERRORMSG("CProposalCreateTx::ExecuteTx,get proposal expire block count error"),
                          WRITE_ACCOUNT_FAIL, "get-expire-block-count-error");
     }

     auto newProposal = proposalBean.proposalPtr->GetNewInstance() ;
     newProposal->expire_block_height = context.height + expireBlockCount ;
     newProposal->need_governer_count = GetNeedGovernerCount(proposalBean.proposalPtr->proposal_type, cw);

     if(!cw.sysGovernCache.SetProposal(GetHash(), newProposal)){
         return state.DoS(100, ERRORMSG("CProposalCreateTx::ExecuteTx, set proposal info error"),
                          WRITE_ACCOUNT_FAIL, "bad-write-proposaldb");
     }
    return true ;
}


string CProposalAssentTx::ToString(CAccountDBCache &accountCache) {

    return strprintf("txType=%s, hash=%s, ver=%d, proposalid=%s, llFees=%ld, keyid=%s, valid_height=%d",
                     GetTxType(nTxType), GetHash().ToString(), nVersion, txid.GetHex(), llFees,
                     txUid.ToString(), valid_height);
}
 Object CProposalAssentTx::ToJson(const CAccountDBCache &accountCache) const {

     Object result = CBaseTx::ToJson(accountCache);
     result.push_back(Pair("proposal_id", txid.ToString()));
     return result;
} // json-rpc usage

 bool CProposalAssentTx::CheckTx(CTxExecuteContext &context) {

     IMPLEMENT_DEFINE_CW_STATE
     IMPLEMENT_CHECK_TX_REGID(txUid);
     if (!CheckFee(context)) return false;

     shared_ptr<CProposal> proposal ;
     if(!cw.sysGovernCache.GetProposal(txid,proposal)){
         return state.DoS(100, ERRORMSG("CProposalAssentTx::CheckTx, proposal(id=%s)  not found", txid.ToString()),
                          WRITE_ACCOUNT_FAIL, "proposal-not-found");
     }

     if(!CheckIsGoverner(txUid.get<CRegID>(), proposal->proposal_type,cw)){
         return state.DoS(100, ERRORMSG("CProposalAssentTx::CheckTx, the tx commiter(%s) is not a governer", txid.ToString()),
                          WRITE_ACCOUNT_FAIL, "permission-deney");
     }

     CAccount srcAccount;
     if (!cw.accountCache.GetAccount(txUid, srcAccount))
         return state.DoS(100, ERRORMSG("CProposalAssentTx::CheckTx, read account failed"), REJECT_INVALID,
                          "bad-getaccount");

     CPubKey pubKey = (txUid.is<CPubKey>() ? txUid.get<CPubKey>() : srcAccount.owner_pubkey);
     IMPLEMENT_CHECK_TX_SIGNATURE(pubKey);

    return true ;
}

bool CProposalAssentTx::ExecuteTx(CTxExecuteContext &context) {

     IMPLEMENT_DEFINE_CW_STATE
     CAccount srcAccount;
     if (!cw.accountCache.GetAccount(txUid, srcAccount)) {
         return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, read source addr account info error"),
                          READ_ACCOUNT_FAIL, "bad-read-accountdb");
     }

     if (!srcAccount.OperateBalance(fee_symbol, SUB_FREE, llFees)) {
         return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, account has insufficient funds"),
                          UPDATE_ACCOUNT_FAIL, "operate-minus-account-failed");
     }

     shared_ptr<CProposal> proposal ;
     if(!cw.sysGovernCache.GetProposal(txid,proposal)){
         return state.DoS(100, ERRORMSG("CProposalAssentTx::CheckTx, proposal(id=%s)  not found", txid.ToString()),
                          WRITE_ACCOUNT_FAIL, "proposal-not-found");
     }

     if(proposal->expire_block_height < context.height){
         return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, proposal(id=%s)  is expired", txid.ToString()),
                          WRITE_ACCOUNT_FAIL, "proposal-expired");
     }

     if (!cw.accountCache.SetAccount(CUserID(srcAccount.keyid), srcAccount))
         return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, set account info error"),
                          WRITE_ACCOUNT_FAIL, "bad-write-accountdb");

     if(!cw.sysGovernCache.SetAssention(txid, txUid.get<CRegID>())){
         return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, set proposal assention info error"),
                          WRITE_ACCOUNT_FAIL, "bad-write-proposaldb");
     }

     auto assentedCount = cw.sysGovernCache.GetAssentionCount(txid);

     if(assentedCount > proposal->need_governer_count){
         return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, proposal executed already"),
                          WRITE_ACCOUNT_FAIL, "proposal-executed-already");
     }

     if( assentedCount == proposal->need_governer_count){

         if(!proposal->ExecuteProposal(context)){
             return state.DoS(100, ERRORMSG("CProposalAssentTx::ExecuteTx, proposal execute error"),
                              WRITE_ACCOUNT_FAIL, "proposal-execute-error");
         }
     }

     return true ;
}