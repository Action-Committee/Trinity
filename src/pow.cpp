// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

extern CChain chainActive;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    // Workaround: Disable pow check between 915235 and 955000
    if (915235 <= pindexLast->nHeight && pindexLast->nHeight <= 955000)
        return pblock->nBits;

    int algo = pblock->GetAlgo();
    unsigned int nProofOfWorkLimit = Params().ProofOfWorkLimit(algo).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    if (Params().AllowMinDifficultyBlocks())
    {
        // Special difficulty rule for testnet:
        // If the new block's timestamp is more than 2* 10 minutes
        // then allow mining of a min-difficulty block.
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + Params().TargetSpacing()*2)
            return nProofOfWorkLimit;
        else
        {
            // Return the last non-special-min-difficulty-rules-block
            const CBlockIndex* pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % Params().Interval() != 0 && pindex->nBits == nProofOfWorkLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
    }

    // find previous block with same algo
    const CBlockIndex* pindexPrev = chainActive.GetLastBlockIndexForAlgo(pindexLast, algo);

    // find first block in averaging interval
    // Go back by what we want to be nAveragingInterval blocks
    const CBlockIndex* pindexFirst = pindexPrev;
    int64_t nAveragingInterval = Params().TargetTimespan() / Params().TargetSpacing(); // 10 blocks
    for (int i = 0; pindexFirst && i < nAveragingInterval - 1; i++)
        pindexFirst = chainActive.GetLastBlockIndexForAlgo(pindexFirst->pprev, algo);

    if (pindexFirst == NULL)
        return nProofOfWorkLimit; // not nAveragingInterval blocks of this algo available

    // Limit adjustment step
    int64_t nActualTimespan = pindexPrev->GetBlockTime() - pindexFirst->GetBlockTime();
    int64_t nMaxAdjustDown = pindexLast->nHeight+1 >= Params().BlockDiffAdjustV2() ? Params().MaxAdjustDownV2() : Params().MaxAdjustDownV1();
    int64_t nMaxAdjustUp = pindexLast->nHeight+1 >= Params().BlockDiffAdjustV2() ? Params().MaxAdjustUpV2() : Params().MaxAdjustUpV1();
    int64_t nMinActualTimespan = Params().TargetTimespan() * (100 - nMaxAdjustUp) / 100;
    int64_t nMaxActualTimespan = Params().TargetTimespan() * (100 + nMaxAdjustDown) / 100;
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    if (nActualTimespan < nMinActualTimespan)
        nActualTimespan = nMinActualTimespan;
    if (nActualTimespan > nMaxActualTimespan)
        nActualTimespan = nMaxActualTimespan;

    // Retarget
    uint256 bnNew;
    uint256 bnOld;
    bnNew.SetCompact(pindexPrev->nBits);
    bnOld = bnNew;
    // Trinity: intermediate uint256 can overflow by 1 bit
    bool fShift = bnNew.bits() > 235;
    if (fShift)
        bnNew >>= 1;
    bnNew *= nActualTimespan;
    bnNew /= Params().TargetTimespan();
    if (fShift)
        bnNew <<= 1;

    if (bnNew > Params().ProofOfWorkLimit(algo))
        bnNew = Params().ProofOfWorkLimit(algo);

    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET %s\n", GetAlgoName(algo));
    LogPrintf("Params().TargetTimespan() = %d    nActualTimespan = %d\n", Params().TargetTimespan(), nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexPrev->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, int algo)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    if (Params().SkipProofOfWorkCheck())
       return true;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > Params().ProofOfWorkLimit(algo))
        return error("CheckProofOfWork(algo=%d) : nBits below minimum work", algo);

    // Check proof of work matches claimed amount
    if (hash > bnTarget)
        return error("CheckProofOfWork(algo=%d) : hash doesn't match nBits", algo);

    return true;
}

uint256 GetBlockWork(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return ((~bnTarget / (bnTarget + 1)) + 1);
}

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return ((~bnTarget / (bnTarget + 1)) + 1) * block.GetAlgoWorkFactor();
}
