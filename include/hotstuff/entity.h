/**
 * Copyright 2018 VMware
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HOTSTUFF_ENT_H
#define _HOTSTUFF_ENT_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstddef>
#include <ios>

#include "salticidae/netaddr.h"
#include "salticidae/ref.h"
#include "hotstuff/type.h"
#include "hotstuff/util.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

    enum EntityType {
        ENT_TYPE_CMD = 0x0,
        ENT_TYPE_BLK = 0x1
    };

    struct ReplicaInfo {
        ReplicaID id;
        salticidae::PeerId peer_id;
        pubkey_bt pubkey;

        ReplicaInfo(ReplicaID id,
                    const salticidae::PeerId &peer_id,
                    pubkey_bt &&pubkey):
                id(id), peer_id(peer_id), pubkey(std::move(pubkey)) {}

        ReplicaInfo(const ReplicaInfo &other):
                id(other.id), peer_id(other.peer_id),
                pubkey(other.pubkey->clone()) {}

        ReplicaInfo(ReplicaInfo &&other):
                id(other.id), peer_id(other.peer_id),
                pubkey(std::move(other.pubkey)) {}
    };

    class ReplicaConfig {
        std::unordered_map<ReplicaID, ReplicaInfo> replica_map;

    public:
        size_t nreplicas;
        size_t nmajority;

        ReplicaConfig(): nreplicas(0), nmajority(0) {}

        void add_replica(ReplicaID rid, const ReplicaInfo &info) {
            replica_map.insert(std::make_pair(rid, info));
            nreplicas++;
        }

        const ReplicaInfo &get_info(ReplicaID rid) const {
            auto it = replica_map.find(rid);
            if (it == replica_map.end())
            {
                HOTSTUFF_LOG_INFO("rid not found, returning null");
//                return nullptr;
//                throw HotStuffError("rid %s not found",
//                                    get_hex(rid).c_str());
            }

            return it->second;
        }

        const PubKey &get_pubkey(ReplicaID rid) const {
            return *(get_info(rid).pubkey);
        }

        const salticidae::PeerId &get_peer_id(ReplicaID rid) const {
            return get_info(rid).peer_id;
        }
    };

    class Block;
    class HotStuffCore;

    using block_t = salticidae::ArcObj<Block>;

    class Command: public Serializable {
        friend HotStuffCore;
    public:
        virtual ~Command() = default;
        virtual const uint256_t &get_hash() const = 0;
        virtual const uint32_t &get_cid() const = 0;
        virtual const uint32_t &get_key() const = 0;
        virtual const uint32_t &get_val() const = 0;

        virtual const uint32_t &get_cluster_num() const = 0;
        virtual const uint32_t &get_node_num() const = 0;
        virtual const uint32_t &get_reconfig_mode() const = 0;


        virtual bool verify() const = 0;
        virtual operator std::string () const {
            DataStream s;
            s << "<cmd id=" << get_hex10(get_hash()) << ">";
            return s;
        }
    };

    using command_t = ArcObj<Command>;

    template<typename Hashable>
    inline static std::vector<uint256_t>
    get_hashes(const std::vector<Hashable> &plist) {
        std::vector<uint256_t> hashes;
        for (const auto &p: plist)
            hashes.push_back(p->get_hash());
        return hashes;
    }

    class Block {
        friend HotStuffCore;
        std::vector<uint256_t> parent_hashes;
        std::vector<uint256_t> cmds;
        std::vector<int> keys;
        std::vector<int> vals;


        std::unordered_set<ReplicaID> lcomplains;
        std::unordered_set<ReplicaID> rcomplains;


        quorum_cert_bt qc;
        bytearray_t extra;

        /* the following fields can be derived from above */
        uint256_t hash;
        std::vector<block_t> parents;
        block_t qc_ref;
        quorum_cert_bt self_qc;
        quorum_cert_bt cluster_mc;
        uint32_t height;
        uint32_t cluster_number;
        bool delivered;
        int8_t decision;

        std::unordered_set<ReplicaID> voted;

    public:
        Block():
                qc(nullptr),
                qc_ref(nullptr),
                self_qc(nullptr), height(0), cluster_number(0),
                delivered(false), decision(0) {}

        Block(bool delivered, int8_t decision):
                qc(new QuorumCertDummy()),
                hash(salticidae::get_hash(*this)),
                qc_ref(nullptr),
                self_qc(nullptr), height(0), cluster_number(0),
                delivered(delivered), decision(decision) {}

        Block(const std::vector<block_t> &parents,
              const std::vector<uint256_t> &cmds,
              const std::vector<int> &keys,
              const std::vector<int> &vals,
              quorum_cert_bt &&qc,
              bytearray_t &&extra,
              uint32_t height,
              const block_t &qc_ref,
              quorum_cert_bt &&self_qc,
              int8_t decision = 0):
                parent_hashes(get_hashes(parents)),
                cmds(cmds),
                keys(keys),
                vals(vals),
                qc(std::move(qc)),
                extra(std::move(extra)),
                hash(salticidae::get_hash(*this)),
                parents(parents),
                qc_ref(qc_ref),
                self_qc(std::move(self_qc)),
                height(height), cluster_number(0),
                delivered(0),
                decision(decision) {}

        Block(const std::vector<block_t> &parents,
              const std::vector<uint256_t> &cmds,
              quorum_cert_bt &&qc,
              bytearray_t &&extra,
              uint32_t height,
              uint32_t cluster_number,
              const block_t &qc_ref,
              quorum_cert_bt &&self_qc,
              int8_t decision = 0):
                parent_hashes(get_hashes(parents)),
                cmds(cmds),
                qc(std::move(qc)),
                extra(std::move(extra)),
                hash(salticidae::get_hash(*this)),
                parents(parents),
                qc_ref(qc_ref),
                self_qc(std::move(self_qc)),
                height(height), cluster_number(cluster_number),
                delivered(0),
                decision(decision) {}

        void serialize(DataStream &s) const;

        void unserialize(DataStream &s, HotStuffCore *hsc);

        const std::vector<uint256_t> &get_cmds() const {
            return cmds;
        }

        const std::vector<int> &get_keys() const {
            return keys;
        }

        const std::vector<int> &get_vals() const {
            return vals;
        }
        const std::vector<block_t> &get_parents() const {
            return parents;
        }

        const std::vector<uint256_t> &get_parent_hashes() const {
            return parent_hashes;
        }

        const uint256_t &get_hash() const { return hash; }

        bool verify(const HotStuffCore *hsc) const;

        promise_t verify(const HotStuffCore *hsc, VeriPool &vpool) const;

        int8_t get_decision() const { return decision; }

        bool is_delivered() const { return delivered; }

        uint32_t get_height() const { return height; }

        uint32_t get_cluster_number() const { return cluster_number; }

        const quorum_cert_bt &get_qc() const { return qc; }

        const block_t &get_qc_ref() const { return qc_ref; }

        const bytearray_t &get_extra() const { return extra; }

        operator std::string () const {
            DataStream s;
            s << "<block "
              << "id="  << get_hex10(hash) << " "
              << "height=" << std::to_string(height) << " "
              << "parent=" << get_hex10(parent_hashes[0]) << " "
              << "qc_ref=" << (qc_ref ? get_hex10(qc_ref->get_hash()) : "null") << ">";
            return s;
        }
    };

    struct BlockHeightCmp {
        bool operator()(const block_t &a, const block_t &b) const {
            return a->get_height() < b->get_height();
        }
    };

    class EntityStorage {
        std::unordered_map<const uint256_t, block_t> blk_cache;
        std::unordered_map<int, const uint256_t> cid_blkhash;

        std::unordered_map<const uint256_t, command_t> cmd_cache;

        std::unordered_map<int, int> cid_join1;
        std::unordered_map<int, int> cid_join2;

    public:
        bool is_blk_delivered(const uint256_t &blk_hash) {
            auto it = blk_cache.find(blk_hash);
            if (it == blk_cache.end()) return false;
            return it->second->is_delivered();
        }


        block_t find_blk_cid(int cid) {

            uint256_t key_temp;
            block_t btemp;

            for(auto kv : blk_cache) {
                HOTSTUFF_LOG_INFO("iterating through through blk_cache: with current block's height =  %d", kv.second->get_height());
                if (cid == int(kv.second->get_height()))
                {
                    btemp = kv.second;
                    return btemp;
                }
            }

            return btemp;
        }

        bool is_blk_fetched(const uint256_t &blk_hash) {
            return blk_cache.count(blk_hash);
        }



        block_t add_blk(Block &&_blk, const ReplicaConfig &/*config*/) {
            // seems fine
            block_t blk = new Block(std::move(_blk));

            HOTSTUFF_LOG_WARN("in add_blk(2) for blk height:  %d, with blk = %s, blk_hash = %d",
                              int(blk->get_height()), std::string(*blk).c_str(), blk->get_hash());
            return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
        }


        void add_cid_blkhash(const block_t &blk) {
            // seems fine
            HOTSTUFF_LOG_WARN("in add_cid_blkhash(2) for blk height:  %d, with blk = %s, blk_hash= %d",
                              int(blk->get_height()), std::string(*blk).c_str(), blk->get_hash());
            cid_blkhash.insert(std::make_pair(int(blk->get_height()), blk->get_hash() )).first->second;
        }


        bool add_cid_join1(int blk_height) {

            auto it = cid_join1.find(blk_height);

//        HOTSTUFF_LOG_INFO("add_cid_join1 it->second is %d", it->second);

            if (it ==cid_join1.end())
            {
                HOTSTUFF_LOG_INFO("add_cid_join1: Not in cid_join1 map");
                cid_join1.insert(std::make_pair(blk_height,1)).first->second;
                return false;
            }
            else
            {
                HOTSTUFF_LOG_INFO("add_cid_join1: Inside cid_join1 map with it->second = %d", int(it->second));
                if(++it->second <1) return false;
            }
//        HOTSTUFF_LOG_INFO("it->second is %d", int(it->second));

            return true;

        }


        bool add_cid_join2(int blk_height) {

            auto it = cid_join2.find(blk_height);

            if (it ==cid_join2.end())
            {
                HOTSTUFF_LOG_INFO("add_cid_join2: Not in cid_join2 map");
                cid_join2.insert(std::make_pair(blk_height,1)).first->second;
                return false;
            }
            else
            {
                HOTSTUFF_LOG_INFO("add_cid_join2: Inside cid_join2 map with it->second = %d", int(it->second));

                if(++it->second <1) return false;
            }

            return true;

        }







        block_t add_blk(Block &&_blk, const ReplicaConfig &/*config*/, int block_height) {
            //if (!_blk.verify(config))
            //{
            //    HOTSTUFF_LOG_WARN("invalid %s", std::string(_blk).c_str());
            //    return nullptr;
            //}
            block_t blk = new Block(std::move(_blk));


            HOTSTUFF_LOG_WARN("in add_blk(3) for blk height:  %d, with blk = %s, blk_hash = %d",
                              int(blk->get_height()), std::string(*blk).c_str(), blk->get_hash());
            return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
        }

        const block_t &add_blk(const block_t &blk) {

            HOTSTUFF_LOG_WARN("in &add_blk for blk_height:  %d, with blk_hash = %d",
                              int(blk->get_height()), blk->get_hash());

            return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
        }

        block_t find_blk(const uint256_t &blk_hash) {
            auto it = blk_cache.find(blk_hash);
            return it == blk_cache.end() ? nullptr : it->second;
        }

        const uint256_t find_blk_hash_for_cid(int cid) {
            auto it = cid_blkhash.find(cid);

            if (it==cid_blkhash.end())
            {
                HOTSTUFF_LOG_INFO("returning nullptr for find_blk_hash_for_cid(cid)");

                static const uint8_t negativeOne[] = { 0xFF };
                return negativeOne;

            }
            else
            {
                HOTSTUFF_LOG_INFO("returning %d for  find_blk_hash_for_cid(cid) with cid=%d",
                                  it->second, cid);

                return it->second;
            }
            return it == cid_blkhash.end() ? nullptr : it->second;
        }

        bool is_cmd_fetched(const uint256_t &cmd_hash) {
            return cmd_cache.count(cmd_hash);
        }

        const command_t &add_cmd(const command_t &cmd) {
            return cmd_cache.insert(std::make_pair(cmd->get_hash(), cmd)).first->second;
        }

        command_t find_cmd(const uint256_t &cmd_hash) {
            auto it = cmd_cache.find(cmd_hash);
            return it == cmd_cache.end() ? nullptr: it->second;
        }

        size_t get_cmd_cache_size() {
            return cmd_cache.size();
        }
        size_t get_blk_cache_size() {
            return blk_cache.size();
        }

        bool try_release_cmd(const command_t &cmd) {
            if (cmd.get_cnt() == 2) /* only referred by cmd and the storage */
            {
                const auto &cmd_hash = cmd->get_hash();
                cmd_cache.erase(cmd_hash);
                return true;
            }
            return false;
        }

        bool try_release_blk(const block_t &blk) {
            if (blk.get_cnt() == 2) /* only referred by blk and the storage */
            {
                const auto &blk_hash = blk->get_hash();
#ifdef HOTSTUFF_PROTO_LOG
                HOTSTUFF_LOG_INFO("releasing blk %.10s", get_hex(blk_hash).c_str());
#endif
//            for (const auto &cmd: blk->get_cmds())
//                try_release_cmd(cmd);
                blk_cache.erase(blk_hash);
                return true;
            }
#ifdef HOTSTUFF_PROTO_LOG
            else
            HOTSTUFF_LOG_INFO("cannot release (%lu)", blk.get_cnt());
#endif
            return false;
        }
    };

}

#endif