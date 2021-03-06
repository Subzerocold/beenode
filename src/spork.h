// Copyright (c) 2020 The BeeGroup developers are EternityGroup
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include "hash.h"
#include "net.h"
#include "utilstrencodings.h"
#include "key.h"
#include "primitives/transaction.h"
class CSporkMessage;
class CSporkManager;
class CEvolutionManager;


/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
static const int SPORK_2_INSTANTSEND_ENABLED                            = 10001;
static const int SPORK_3_INSTANTSEND_BLOCK_FILTERING                    = 10002;
static const int SPORK_5_INSTANTSEND_MAX_VALUE                          = 10004;
static const int SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT                 = 10007;
static const int SPORK_10_MASTERNODE_PAY_UPDATED_NODES                  = 10009;
static const int SPORK_12_RECONSIDER_BLOCKS                             = 10011;
static const int SPORK_14_REQUIRE_SENTINEL_FLAG                         = 10013;
static const int SPORK_15_DETERMINISTIC_MNS_ENABLED                     = 10014;
static const int SPORK_16_INSTANTSEND_AUTOLOCKS                         = 10015;
static const int SPORK_17_QUORUM_DKG_ENABLED                            = 10016;
static const int SPORK_18_EVOLUTION_PAYMENTS							= 10017;
static const int SPORK_19_EVOLUTION_PAYMENTS_ENFORCEMENT				= 10018;
static const int SPORK_21_MASTERNODE_ORDER_ENABLE			        	= 10020;
static const int SPORK_24_DETERMIN_UPDATE			        	        = 10023;

static const int SPORK_START                                            = SPORK_2_INSTANTSEND_ENABLED;
static const int SPORK_END                                              = SPORK_24_DETERMIN_UPDATE;

extern std::map<int, int64_t> mapSporkDefaults;
extern CSporkManager sporkManager;
extern CEvolutionManager evolutionManager;

/**
 * Sporks are network parameters used primarily to prevent forking and turn
 * on/off certain features. They are a soft consensus mechanism.
 *
 * We use 2 main classes to manage the spork system.
 *
 * SporkMessages - low-level constructs which contain the sporkID, value,
 *                 signature and a signature timestamp
 * SporkManager  - a higher-level construct which manages the naming, use of
 *                 sporks, signatures and verification, and which sporks are active according
 *                 to this node
 */

/**
 * CSporkMessage is a low-level class used to encapsulate Spork messages and
 * serialize them for transmission to other peers. This includes the internal
 * spork ID, value, spork signature and timestamp for the signature.
 */
class CSporkMessage
{
private:
    std::vector<unsigned char> vchSig;

public:
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;
	std::string	sWEvolution;	

    CSporkMessage(int nSporkID, int64_t nValue, std::string sEvolution, int64_t nTimeSigned) :
        nSporkID(nSporkID),
        nValue(nValue),
        nTimeSigned(nTimeSigned),
		sWEvolution( sEvolution )
        {}

    CSporkMessage() :
        nSporkID(0),
        nValue(0),
        nTimeSigned(0),
		sWEvolution("")
        {}


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
		READWRITE(sWEvolution);
		READWRITE(vchSig);
       /* if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }*/
    }

    /**
     * GetHash returns the double-sha256 hash of the serialized spork message.
     */
    uint256 GetHash() const;

    /**
     * GetSignatureHash returns the hash of the serialized spork message
     * without the signature included. The intent of this method is to get the
     * hash to be signed.
     */
    uint256 GetSignatureHash() const;

    /**
     * Sign will sign the spork message with the given key.
     */
    bool Sign(const CKey& key);

    /**
     * CheckSignature will ensure the spork signature matches the provided public
     * key hash.
     */
    bool CheckSignature(const CKeyID& pubKeyId) const;

    /**
     * GetSignerKeyID is used to recover the spork address of the key used to
     * sign this spork message.
     *
     * This method was introduced along with the multi-signer sporks feature,
     * in order to identify which spork key signed this message.
     */
    bool GetSignerKeyID(CKeyID& retKeyidSporkSigner);

    /**
     * Relay is used to send this spork message to other peers.
     */
    void Relay(CConnman& connman);
};

/**
 * CSporkManager is a higher-level class which manages the node's spork
 * messages, rules for which sporks should be considered active/inactive, and
 * processing for certain sporks (e.g. spork 12).
 */
class CSporkManager
{
private:
    static const std::string SERIALIZATION_VERSION_STRING;

    mutable CCriticalSection cs;
    std::map<uint256, CSporkMessage> mapSporksByHash;
    std::map<int, uint256> mapSporkHashesByID;
    std::map<int, std::map<CKeyID, CSporkMessage> > mapSporksActive;

    std::set<CKeyID> setSporkPubKeyIDs;
    int nMinSporkKeys;
    CKey sporkPrivKey;

    /**
     * SporkValueIsActive is used to get the value agreed upon by the majority
     * of signed spork messages for a given Spork ID.
     */
    bool SporkValueIsActive(int nSporkID, int64_t& nActiveValueRet) const;

public:

    CSporkManager() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
            if (strVersion != SERIALIZATION_VERSION_STRING) {
                return;
            }
        } else {
            strVersion = SERIALIZATION_VERSION_STRING;
            READWRITE(strVersion);
        }
        // we don't serialize pubkey ids because pubkeys should be
        // hardcoded or be setted with cmdline or options, should
        // not reuse pubkeys from previous beenoded run
        READWRITE(mapSporksByHash);
        READWRITE(mapSporksActive);
        // we don't serialize private key to prevent its leakage
    }

    /**
     * Clear is used to clear all in-memory active spork messages. Since spork
     * public and private keys are set in init.cpp, we do not clear them here.
     *
     * This method was introduced along with the spork cache.
     */
    void Clear();

    /**
     * CheckAndRemove is defined to fulfill an interface as part of the on-disk
     * cache used to cache sporks between runs. If sporks that are restored
     * from cache do not have valid signatures when compared against the
     * current spork private keys, they are removed from in-memory storage.
     *
     * This method was introduced along with the spork cache.
     */
    void CheckAndRemove();

    /**
     * ProcessSpork is used to handle the 'getsporks' and 'spork' p2p messages.
     *
     * For 'getsporks', it sends active sporks to the requesting peer. For 'spork',
     * it validates the spork and adds it to the internal spork storage and
     * performs any necessary processing.
     */
    void ProcessSpork(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    /**
     * ExecuteSpork is used to perform certain actions based on the spork value.
     *
     * Currently only used with Spork 12.
     */
    void ExecuteSpork(int nSporkID, int nValue);
   	 /**
     * UpdateSpork is used by the spork RPC command to set a new spork value, sign
     * and broadcast the spork message.
     */
    bool UpdateSpork(int nSporkID, int64_t nValue,std::string sEvol,  CConnman& connman);
	
	void setActiveSpork( CSporkMessage &spork );
	int64_t getActiveSporkValue( int nSporkID,CSporkMessage& spork  );
	bool isActiveSporkInMap(int nSporkID);
	
    bool IsSporkActive(int nSporkID);

    /**
     * GetSporkValue returns the spork value given a Spork ID. If no active spork
     * message has yet been received by the node, it returns the default value.
     */
	bool IsSporkWorkActive(int nSporkID);
    int64_t GetSporkValue(int nSporkID);

    /**
     * GetSporkIDByName returns the internal Spork ID given the spork name.
     */
    int GetSporkIDByName(const std::string& strName);

    /**
     * GetSporkNameByID returns the spork name as a string, given a Spork ID.
     */
    std::string GetSporkNameByID(int nSporkID);

    /**
     * GetSporkByHash returns a spork message given a hash of the spork message.
     *
     * This is used when a requesting peer sends a MSG_SPORK inventory message with
     * the hash, to quickly lookup and return the full spork message. We maintain a
     * hash-based index of sporks for this reason, and this function is the access
     * point into that index.
     */
    bool GetSporkByHash(const uint256& hash, CSporkMessage &sporkRet);

    /**
     * SetSporkAddress is used to set a public key ID which will be used to
     * verify spork signatures.
     *
     * This can be called multiple times to add multiple keys to the set of
     * valid spork signers.
     */
    bool SetSporkAddress(const std::string& strAddress);

    /**
     * SetMinSporkKeys is used to set the required spork signer threshold, for
     * a spork to be considered active.
     *
     * This value must be at least a majority of the total number of spork
     * keys, and for obvious resons cannot be larger than that number.
     */
    bool SetMinSporkKeys(int minSporkKeys);

    /**
     * SetPrivKey is used to set a spork key to enable setting / signing of
     * spork values.
     *
     * This will return false if the private key does not match any spork
     * address in the set of valid spork signers (see SetSporkAddress).
     */
    bool SetPrivKey(const std::string& strPrivKey);

    /**
     * ToString returns the string representation of the SporkManager.
     */
    std::string ToString() const;
};
class CEvolutionManager
{
private:
	std::map<int, std::string> mapEvolution;
	std::map<int, std::string> mapDisableNodes;

public:

	CEvolutionManager() {}
	
	void setNewEvolutions( const std::string &sEvol );
	void setDisableNodes( const std::string &sEvol );
	std::string getEvolution( int nBlockHeight );
    std::string getDisableNode( int nBlockHeight );
	bool IsTransactionValid( const CTransactionRef& txNew, int nBlockHeight, CAmount blockCurEvolution );
	bool checkEvolutionString( const std::string &sEvol );
};
#endif
