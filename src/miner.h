// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "primitives/block.h"

#include <stdint.h>
#include "key.h"

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CReserveKey;
class CScript;
class CWallet;

struct CBlockTemplate;

/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, const CPubKey& txPub, const CKey& txPriv, CWallet* pwallet, bool fProofOfStake);

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
/** Check mined block */
void UpdateTime(CBlockHeader* block, const CBlockIndex* pindexPrev);

#ifdef ENABLE_WALLET
    /** Run the miner threads */
    void GeneratePrcycoins(bool fGenerate, CWallet* pwallet, int nThreads);
    /** Generate a new block, without valid proof-of-work */
    CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet, bool fProofOfStake);

    /** Run the PoA miner threads */
    void GeneratePoAPrcycoin(CWallet* pwallet, int period);
    /** Generate a new PoA block */
    CBlockTemplate* CreateNewPoABlock(const CScript& scriptPubKeyIn, const CPubKey& txPub, const CKey& txPriv, CWallet* pwallet);
    CBlockTemplate* CreateNewPoABlockWithKey(CReserveKey& reservekey, CWallet* pwallet);

    void BitcoinMiner(CWallet* pwallet, bool fProofOfStake);
    void ThreadStakeMinter();
#endif // ENABLE_WALLET

extern double dHashesPerSec;
extern int64_t nHPSTimerStart;

struct CBlockTemplate {
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};

#endif // BITCOIN_MINER_H
