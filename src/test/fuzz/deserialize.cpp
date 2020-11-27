// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addrdb.h>
#include <addrman.h>
#include <addrman_impl.h>
#include <blockencodings.h>
#include <blockfileinfo.h>
#include <blockfilter.h>
#include <chain.h>
#include <coins.h>
#include <compressor.h>
#include <consensus/merkle.h>
#include <key.h>
#include <merkleblock.h>
#include <net.h>
#include <netbase.h>
#include <node/utxo_snapshot.h>
#include <primitives/block.h>
#include <protocol.h>
#include <psbt.h>
#include <pubkey.h>
#include <script/keyorigin.h>
#include <streams.h>
#include <undo.h>
#include <version.h>

#include <test/fuzz/fuzz.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <unistd.h>
#include <vector>

using node::SnapshotMetadata;

void initialize() {
    // Fuzzers using pubkey must hold an ECCVerifyHandle.
    static const ECCVerifyHandle verify_handle;
}

namespace {

struct invalid_fuzzing_input_exception : public std::exception {};

template <typename T>
CDataStream Serialize(const T &obj, const int version = INIT_PROTO_VERSION,
                      const int ser_type = SER_NETWORK) {
    CDataStream ds(ser_type, version);
    ds << obj;
    return ds;
}

template <typename T> T Deserialize(CDataStream ds) {
    T obj;
    ds >> obj;
    return obj;
}

template <typename T>
void DeserializeFromFuzzingInput(
    const std::vector<uint8_t> &buffer, T &obj,
    const std::optional<int> protocol_version = std::nullopt,
    const int ser_type = SER_NETWORK) {
    CDataStream ds(buffer, ser_type, INIT_PROTO_VERSION);
    if (protocol_version) {
        ds.SetVersion(*protocol_version);
    } else {
        try {
            int version;
            ds >> version;
            ds.SetVersion(version);
        } catch (const std::ios_base::failure &) {
            throw invalid_fuzzing_input_exception();
        }
    }
    try {
        ds >> obj;
    } catch (const std::ios_base::failure &) {
        throw invalid_fuzzing_input_exception();
    }
    assert(buffer.empty() || !Serialize(obj).empty());
}

template <typename T>
void AssertEqualAfterSerializeDeserialize(
    const T &obj, const int version = INIT_PROTO_VERSION,
    const int ser_type = SER_NETWORK) {
    assert(Deserialize<T>(Serialize(obj, version, ser_type)) == obj);
}

} // namespace

void test_one_input(const std::vector<uint8_t> &buffer) {
    try {
#if BLOCK_FILTER_DESERIALIZE
        BlockFilter block_filter;
        DeserializeFromFuzzingInput(buffer, block_filter);
#elif ADDR_INFO_DESERIALIZE
        AddrInfo addr_info;
        DeserializeFromFuzzingInput(buffer, addr_info);
#elif BLOCK_FILE_INFO_DESERIALIZE
        CBlockFileInfo block_file_info;
        DeserializeFromFuzzingInput(buffer, block_file_info);
#elif BLOCK_HEADER_AND_SHORT_TXIDS_DESERIALIZE
        CBlockHeaderAndShortTxIDs block_header_and_short_txids;
        DeserializeFromFuzzingInput(buffer, block_header_and_short_txids);
#elif FEE_RATE_DESERIALIZE
        CFeeRate fee_rate;
        DeserializeFromFuzzingInput(buffer, fee_rate);
        AssertEqualAfterSerializeDeserialize(fee_rate);
#elif MERKLE_BLOCK_DESERIALIZE
        CMerkleBlock merkle_block;
        DeserializeFromFuzzingInput(buffer, merkle_block);
#elif OUT_POINT_DESERIALIZE
        COutPoint out_point;
        DeserializeFromFuzzingInput(buffer, out_point);
        AssertEqualAfterSerializeDeserialize(out_point);
#elif PARTIAL_MERKLE_TREE_DESERIALIZE
        CPartialMerkleTree partial_merkle_tree;
        DeserializeFromFuzzingInput(buffer, partial_merkle_tree);
#elif PUB_KEY_DESERIALIZE
        CPubKey pub_key;
        DeserializeFromFuzzingInput(buffer, pub_key);
        // TODO: The following equivalence should hold for CPubKey? Fix.
        // AssertEqualAfterSerializeDeserialize(pub_key);
#elif SCRIPT_DESERIALIZE
        CScript script;
        DeserializeFromFuzzingInput(buffer, script);
#elif SUB_NET_DESERIALIZE
        CSubNet sub_net_1;
        DeserializeFromFuzzingInput(buffer, sub_net_1, INIT_PROTO_VERSION);
        AssertEqualAfterSerializeDeserialize(sub_net_1, INIT_PROTO_VERSION);
        CSubNet sub_net_2;
        DeserializeFromFuzzingInput(buffer, sub_net_2,
                                    INIT_PROTO_VERSION | ADDRV2_FORMAT);
        AssertEqualAfterSerializeDeserialize(sub_net_2, INIT_PROTO_VERSION |
                                                            ADDRV2_FORMAT);
        CSubNet sub_net_3;
        DeserializeFromFuzzingInput(buffer, sub_net_3);
        AssertEqualAfterSerializeDeserialize(sub_net_3, INIT_PROTO_VERSION |
                                                            ADDRV2_FORMAT);
#elif TX_IN_DESERIALIZE
        CTxIn tx_in;
        DeserializeFromFuzzingInput(buffer, tx_in);
        AssertEqualAfterSerializeDeserialize(tx_in);
#elif FLAT_FILE_POS_DESERIALIZE
        FlatFilePos flat_file_pos;
        DeserializeFromFuzzingInput(buffer, flat_file_pos);
        AssertEqualAfterSerializeDeserialize(flat_file_pos);
#elif KEY_ORIGIN_INFO_DESERIALIZE
        KeyOriginInfo key_origin_info;
        DeserializeFromFuzzingInput(buffer, key_origin_info);
        AssertEqualAfterSerializeDeserialize(key_origin_info);
#elif PARTIALLY_SIGNED_TRANSACTION_DESERIALIZE
        PartiallySignedTransaction partially_signed_transaction;
        DeserializeFromFuzzingInput(buffer, partially_signed_transaction);
#elif PSBT_INPUT_DESERIALIZE
        PSBTInput psbt_input;
        DeserializeFromFuzzingInput(buffer, psbt_input);
#elif PSBT_OUTPUT_DESERIALIZE
        PSBTOutput psbt_output;
        DeserializeFromFuzzingInput(buffer, psbt_output);
#elif BLOCK_DESERIALIZE
        CBlock block;
        DeserializeFromFuzzingInput(buffer, block);
#elif BLOCKLOCATOR_DESERIALIZE
        CBlockLocator bl;
        DeserializeFromFuzzingInput(buffer, bl);
#elif BLOCKMERKLEROOT
        CBlock block;
        DeserializeFromFuzzingInput(buffer, block);
        bool mutated;
        BlockMerkleRoot(block, &mutated);
#elif ADDRMAN_DESERIALIZE
        AddrMan am(/* asmap= */ std::vector<bool>(),
                   /* consistency_check_ratio= */ 0);
        DeserializeFromFuzzingInput(buffer, am);
#elif BLOCKHEADER_DESERIALIZE
        CBlockHeader bh;
        DeserializeFromFuzzingInput(buffer, bh);
#elif BANENTRY_DESERIALIZE
        CBanEntry be;
        DeserializeFromFuzzingInput(buffer, be);
#elif TXUNDO_DESERIALIZE
        CTxUndo tu;
        DeserializeFromFuzzingInput(buffer, tu);
#elif BLOCKUNDO_DESERIALIZE
        CBlockUndo bu;
        DeserializeFromFuzzingInput(buffer, bu);
#elif COINS_DESERIALIZE
        Coin coin;
        DeserializeFromFuzzingInput(buffer, coin);
#elif NETADDR_DESERIALIZE
        CNetAddr na;
        DeserializeFromFuzzingInput(buffer, na);
        if (na.IsAddrV1Compatible()) {
            AssertEqualAfterSerializeDeserialize(na);
        }
        AssertEqualAfterSerializeDeserialize(na, INIT_PROTO_VERSION |
                                                     ADDRV2_FORMAT);
#elif SERVICE_DESERIALIZE
        CService s;
        DeserializeFromFuzzingInput(buffer, s);
        if (s.IsAddrV1Compatible()) {
            AssertEqualAfterSerializeDeserialize(s);
        }
        AssertEqualAfterSerializeDeserialize(s, INIT_PROTO_VERSION |
                                                    ADDRV2_FORMAT);
        CService s1;
        DeserializeFromFuzzingInput(buffer, s1, INIT_PROTO_VERSION);
        AssertEqualAfterSerializeDeserialize(s1, INIT_PROTO_VERSION);
        assert(s1.IsAddrV1Compatible());
        CService s2;
        DeserializeFromFuzzingInput(buffer, s2,
                                    INIT_PROTO_VERSION | ADDRV2_FORMAT);
        AssertEqualAfterSerializeDeserialize(s2, INIT_PROTO_VERSION |
                                                     ADDRV2_FORMAT);
#elif MESSAGEHEADER_DESERIALIZE
        const CMessageHeader::MessageMagic pchMessageStart = {
            {0x00, 0x00, 0x00, 0x00}};
        CMessageHeader mh(pchMessageStart);
        DeserializeFromFuzzingInput(buffer, mh);
        (void)mh.IsValidWithoutConfig(pchMessageStart);
#elif ADDRESS_DESERIALIZE_V1_NOTIME
        CAddress a;
        DeserializeFromFuzzingInput(buffer, a, INIT_PROTO_VERSION);
        // A CAddress without nTime (as is expected under INIT_PROTO_VERSION)
        // will roundtrip in all 5 formats (with/without nTime, v1/v2,
        // network/disk)
        AssertEqualAfterSerializeDeserialize(a, INIT_PROTO_VERSION);
        AssertEqualAfterSerializeDeserialize(a, PROTOCOL_VERSION);
        AssertEqualAfterSerializeDeserialize(a, 0, SER_DISK);
        AssertEqualAfterSerializeDeserialize(a,
                                             PROTOCOL_VERSION | ADDRV2_FORMAT);
        AssertEqualAfterSerializeDeserialize(a, ADDRV2_FORMAT, SER_DISK);
#elif ADDRESS_DESERIALIZE_V1_WITHTIME
        CAddress a;
        DeserializeFromFuzzingInput(buffer, a, PROTOCOL_VERSION);
        // A CAddress in V1 mode will roundtrip in all 4 formats that have
        // nTime.
        AssertEqualAfterSerializeDeserialize(a, PROTOCOL_VERSION);
        AssertEqualAfterSerializeDeserialize(a, 0, SER_DISK);
        AssertEqualAfterSerializeDeserialize(a,
                                             PROTOCOL_VERSION | ADDRV2_FORMAT);
        AssertEqualAfterSerializeDeserialize(a, ADDRV2_FORMAT, SER_DISK);
#elif ADDRESS_DESERIALIZE_V2
        CAddress a;
        DeserializeFromFuzzingInput(buffer, a,
                                    PROTOCOL_VERSION | ADDRV2_FORMAT);
        // A CAddress in V2 mode will roundtrip in both V2 formats, and also in
        // the V1 formats with time if it's V1 compatible.
        if (a.IsAddrV1Compatible()) {
            AssertEqualAfterSerializeDeserialize(a, PROTOCOL_VERSION);
            AssertEqualAfterSerializeDeserialize(a, 0, SER_DISK);
        }
        AssertEqualAfterSerializeDeserialize(a,
                                             PROTOCOL_VERSION | ADDRV2_FORMAT);
        AssertEqualAfterSerializeDeserialize(a, ADDRV2_FORMAT, SER_DISK);
#elif INV_DESERIALIZE
        CInv i;
        DeserializeFromFuzzingInput(buffer, i);
#elif BLOOMFILTER_DESERIALIZE
        CBloomFilter bf;
        DeserializeFromFuzzingInput(buffer, bf);
#elif DISKBLOCKINDEX_DESERIALIZE
        CDiskBlockIndex dbi;
        DeserializeFromFuzzingInput(buffer, dbi);
#elif TXOUTCOMPRESSOR_DESERIALIZE
        CTxOut to;
        auto toc = Using<TxOutCompression>(to);
        DeserializeFromFuzzingInput(buffer, toc);
#elif BLOCKTRANSACTIONS_DESERIALIZE
        BlockTransactions bt;
        DeserializeFromFuzzingInput(buffer, bt);
#elif BLOCKTRANSACTIONSREQUEST_DESERIALIZE
        BlockTransactionsRequest btr;
        DeserializeFromFuzzingInput(buffer, btr);
#elif SNAPSHOTMETADATA_DESERIALIZE
        SnapshotMetadata snapshot_metadata;
        DeserializeFromFuzzingInput(buffer, snapshot_metadata);
#elif UINT160_DESERIALIZE
        uint160 u160;
        DeserializeFromFuzzingInput(buffer, u160);
        AssertEqualAfterSerializeDeserialize(u160);
#elif UINT256_DESERIALIZE
        uint256 u256;
        DeserializeFromFuzzingInput(buffer, u256);
        AssertEqualAfterSerializeDeserialize(u256);
#else
#error Need at least one fuzz target to compile
#endif
        // Classes intentionally not covered in this file since their
        // deserialization code is fuzzed elsewhere:
        // * Deserialization of CTxOut is fuzzed in src/test/fuzz/tx_out.cpp
        // * Deserialization of CMutableTransaction is fuzzed in
        //   src/test/fuzz/transaction.cpp
    } catch (const invalid_fuzzing_input_exception &) {
    }
}
