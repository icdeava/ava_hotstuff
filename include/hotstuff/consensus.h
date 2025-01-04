/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
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

#ifndef _HOTSTUFF_CONSENSUS_H
#define _HOTSTUFF_CONSENSUS_H

#include <cassert>
#include <set>
#include <unordered_map>

#include "hotstuff/promise.hpp"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

struct Proposal;
struct Vote;
struct LComplain;
struct recs;
struct recsHash;

struct RComplain;

struct Finality;

/** Abstraction for HotStuff protocol state machine (without network implementation). */
class HotStuffCore {
    block_t b0;                                  /** the genesis block */
    /* === state variables === */
    /** block containing the QC for the highest block having one */
    std::pair<block_t, quorum_cert_bt> hqc;   /**< highest QC */

    block_t b_lock;                            /**< locked block */
    block_t b_exec;                            /**< last executed block */
    uint32_t vheight;          /**< height of the block last voted for */

    int cluster_id;


    /* === auxilliary variables === */
    privkey_bt priv_key;            /**< private key for signing votes */
    std::set<block_t> tails;   /**< set of tail blocks */
    ReplicaConfig config;                   /**< replica configuration */
    /* === async event queues === */
    std::unordered_map<block_t, promise_t> qc_waiting;
    promise_t propose_waiting;
    promise_t receive_proposal_waiting;
    promise_t hqc_update_waiting;


    /* == feature switches == */
    /** always vote negatively, useful for some PaceMakers */
    bool vote_disabled;

    block_t get_delivered_blk(const uint256_t &blk_hash);
    void sanity_check_delivered(const block_t &blk);
    void update(const block_t &nblk);
    void update_hqc(const block_t &_hqc, const quorum_cert_bt &qc);
    void on_hqc_update();
    void on_qc_finish(const block_t &blk);
    void on_propose_(const Proposal &prop);
    void on_receive_proposal_(const Proposal &prop);

    protected:
    ReplicaID id;                  /**< identity of the replica itself */

    public:
    BoxObj<EntityStorage> storage;

    promise_t other_cluster_waiting;
    std::unordered_map<int, promise_t> iter_promise_map;



public:
    std::unordered_map<int, int> cluster_map;

    HotStuffCore(ReplicaID id, int cluster_id, privkey_bt &&priv_key);
    virtual ~HotStuffCore() {
        b0->qc_ref = nullptr;
    }

    /* Inputs of the state machine triggered by external events, should called
     * by the class user, with proper invariants. */

    /** Call to initialize the protocol, should be called once before all other
     * functions. */
    void on_init(uint32_t nfaulty);



    /* TODO: better name for "delivery" ? */
    /** Call to inform the state machine that a block is ready to be handled.
     * A block is only delivered if itself is fetched, the block for the
     * contained qc is fetched and all parents are delivered. The user should
     * always ensure this invariant. The invalid blocks will be dropped by this
     * function.
     * @return true if valid */
    bool on_deliver_blk(const block_t &blk);

    /** Call upon the delivery of a proposal message.
     * The block mentioned in the message should be already delivered. */
    void on_receive_proposal(const Proposal &prop);


    void start_local_complain(int height, const uint256_t &blk_hash);


    void on_receive_LComplain(const LComplain &vote);
    void on_receive_RComplain(const RComplain &vote);
    int get_recs_majority();


    /** Call upon the delivery of a vote message.
     * The block mentioned in the message should be already delivered. */
    void on_receive_vote(const Vote &vote);

    /** Call to submit new commands to be decided (executed). "Parents" must
     * contain at least one block, and the first block is the actual parent,
     * while the others are uncles/aunts */
    block_t on_propose(const std::vector<uint256_t> &cmds, const std::vector<int> &keys, const std::vector<int> &vals,
                    const std::vector<block_t> &parents,
                    bytearray_t &&extra = bytearray_t());


    void update_config_parameters(std::vector<PeerId> current_peers);

    /* Functions required to construct concrete instances for abstract classes.
     * */

    /* Outputs of the state machine triggering external events.  The virtual
     * functions should be implemented by the user to specify the behavior upon
     * the events. */
    protected:
    /** Called by HotStuffCore upon the decision being made for cmd. */
    virtual void do_decide(Finality &&fin, int key, int val) = 0;
    virtual void do_decide_read_only(Finality &&fin, int key, int value) = 0;
    virtual void send_reconfig_ack(Finality &&fin, recs r) = 0;

    virtual void do_consensus(const block_t &blk) = 0;
    /** Called by HotStuffCore upon broadcasting a new proposal.
     * The user should send the proposal message to all replicas except for
     * itself. */
    virtual void do_broadcast_proposal(const Proposal &prop) = 0;


    virtual void send_LComplain(ReplicaID last_proposer, const LComplain &vote) = 0;
    virtual void send_RComplain(ReplicaID last_proposer, const RComplain &vote) = 0;
    virtual void send_RComplain_local(ReplicaID last_proposer, const RComplain &vote) = 0;


//    virtual std::vector<PeerId> get_others_f_plus_one() = 0;







    virtual void do_broadcast_proposal_other_clusters(const Proposal &prop) = 0;

    virtual void update_other_f_plus_one(std::unordered_set<int> &leave_set, int cls_id) =0;


    virtual std::pair<int, std::unordered_set<int>> get_leave_set() = 0;



    virtual void receive_mc_local(Proposal &prop) = 0;




    virtual void start_remote_view_change_timer(int timer_cid, const block_t base) = 0;
    virtual void reset_remote_view_change_timer(int timer_blk_height) = 0;
    virtual bool did_receive_mc(int timer_blk_height) = 0;
    virtual bool did_update(int timer_blk_height) = 0;
    virtual void update_finished_update_cids(int timer_blk_height) = 0;


    virtual void decide_after_mc(int blk_height) = 0;
    virtual void store_in_map_for_mc(const block_t &blk) = 0;

    virtual bool check_leader() = 0;
    /** Called upon sending out a new vote to the next proposer.  The user
     * should send the vote message to a *good* proposer to have good liveness,
     * while safety is always guaranteed by HotStuffCore. */
    virtual void do_vote(ReplicaID last_proposer, const Vote &vote) = 0;

    /* The user plugs in the detailed instances for those
     * polymorphic data types. */
    public:
    /** Create a partial certificate that proves the vote for a block. */
    virtual part_cert_bt create_part_cert(const PrivKey &priv_key, const uint256_t &blk_hash) = 0;
    /** Create a partial certificate from its seralized form. */
    virtual part_cert_bt parse_part_cert(DataStream &s) = 0;
    /** Create a quorum certificate that proves 2f+1 votes for a block. */
    virtual quorum_cert_bt create_quorum_cert(const uint256_t &blk_hash) = 0;
    /** Create a quorum certificate from its serialized form. */
    virtual quorum_cert_bt parse_quorum_cert(DataStream &s) = 0;
    /** Create a command object from its serialized form. */
    //virtual command_t parse_cmd(DataStream &s) = 0;

    public:
    /** Add a replica to the current configuration. This should only be called
     * before running HotStuffCore protocol. */
    void add_replica(ReplicaID rid, const PeerId &peer_id, pubkey_bt &&pub_key);
    /** Try to prune blocks lower than last committed height - staleness. */
    void prune(uint32_t staleness);

    /* PaceMaker can use these functions to monitor the core protocol state
     * transition */
    /** Get a promise resolved when the block gets a QC. */
    promise_t async_qc_finish(const block_t &blk);
    /** Get a promise resolved when a new block is proposed. */
    promise_t async_wait_proposal();
    /** Get a promise resolved when a new proposal is received. */
    promise_t async_wait_receive_proposal();
    /** Get a promise resolved when hqc is updated. */
    promise_t async_hqc_update();

    /* Other useful functions */
    const block_t &get_genesis() const { return b0; }
    const block_t &get_hqc() { return hqc.first; }
    const ReplicaConfig &get_config() const { return config; }
    ReplicaID get_id() const { return id; }
    const std::set<block_t> get_tails() const { return tails; }
    operator std::string () const;
    void set_vote_disabled(bool f) { vote_disabled = f; }

    promise_t async_wait_other_cluster();

//    promise_t async_wait_other_cluster(const block_t &blk);

    void on_receive_other_cluster_(const Proposal &prop);

        promise_t async_wait_other_cluster(const block_t &blk);

        void on_receive_other_cluster_();

        void on_receive_other_cluster_(int cid);
    };

/** Abstraction for proposal messages. */
struct Proposal: public Serializable {
    ReplicaID proposer;
    /** block being proposed */
    block_t blk;

    uint32_t cluster_number;
    uint32_t pre_amp_cluster_number;

    uint32_t joining_node_number;


    uint32_t other_cluster_block_height;
    uint32_t msg_type;



    /** handle of the core object to allow polymorphism. The user should use
     * a pointer to the object of the class derived from HotStuffCore */
    HotStuffCore *hsc;

    std::unordered_set<int> current_peer_set;
    uint32_t reconfig_count;


    Proposal(): blk(nullptr), hsc(nullptr) {other_cluster_block_height = -1; }

    Proposal(ReplicaID proposer,
            const block_t &blk,
            HotStuffCore *hsc):
        proposer(proposer),
        blk(blk), hsc(hsc) {other_cluster_block_height = -1;}


    Proposal(ReplicaID proposer,
             const block_t &blk,
             HotStuffCore *hsc, uint32_t c, uint32_t pre_amp_cluster_number, uint32_t oc_h, uint32_t msg_type):
            proposer(proposer),
            blk(blk), hsc(hsc), cluster_number(c), pre_amp_cluster_number(pre_amp_cluster_number),
            other_cluster_block_height(oc_h), msg_type(msg_type) {}

    Proposal(ReplicaID proposer,
             const block_t &blk,
             HotStuffCore *hsc, uint32_t c, uint32_t pre_amp_cluster_number, uint32_t oc_h, uint32_t msg_type,
             const std::unordered_set<int>& current_peer_set, int reconfig_count):
            proposer(proposer),
            blk(blk), hsc(hsc), cluster_number(c), pre_amp_cluster_number(pre_amp_cluster_number),
            other_cluster_block_height(oc_h), msg_type(msg_type), current_peer_set(current_peer_set),
            reconfig_count(reconfig_count) {}

    void serialize(DataStream &s) const override {
        s << cluster_number << pre_amp_cluster_number << other_cluster_block_height << msg_type<< proposer
          << *blk;

        s << current_peer_set.size(); // Serialize the size of the set
        for (const auto& peerId : current_peer_set) {
            s << peerId; // Serialize each element in the set
        }
        s << reconfig_count;

    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> cluster_number >> pre_amp_cluster_number >> other_cluster_block_height >> msg_type >> proposer;
        Block _blk;
        _blk.unserialize(s, hsc);

        if (msg_type!=9)
            blk = hsc->storage->add_blk(std::move(_blk), hsc->get_config(), other_cluster_block_height);


        // EDIT: Unserialize the current_peer_set
        size_t current_peer_set_size;
        s >> current_peer_set_size; // Read the size of the set
        current_peer_set.clear(); // Clear the current set
        for (size_t i = 0; i < current_peer_set_size; ++i)
        {
            int nodeId;
            s >> nodeId; // Deserialize each element
            current_peer_set.insert(nodeId); // Add to the current_peer_set
        }
        s >> reconfig_count;

    }

    operator std::string () const {
        DataStream s;
        s << "<proposal "
          << "rid=" << std::to_string(proposer) << " "
          << "blk=" << get_hex10(blk->get_hash()) << ">";
        return s;
    }
};

/** Abstraction for vote messages. */
struct Vote: public Serializable {
    ReplicaID voter;
    /** block being voted */
    uint256_t blk_hash;
    /** proof of validity for the vote */
    part_cert_bt cert;
    
    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    Vote(): cert(nullptr), hsc(nullptr) {}
    Vote(ReplicaID voter,
        const uint256_t &blk_hash,
        part_cert_bt &&cert,
        HotStuffCore *hsc):
        voter(voter),
        blk_hash(blk_hash),
        cert(std::move(cert)), hsc(hsc) {}

    Vote(const Vote &other):
        voter(other.voter),
        blk_hash(other.blk_hash),
        cert(other.cert ? other.cert->clone() : nullptr),
        hsc(other.hsc) {}

    Vote(Vote &&other) = default;
    
    void serialize(DataStream &s) const override {
        s << voter << blk_hash << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> voter >> blk_hash;
        cert = hsc->parse_part_cert(s);
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter)) &&
                cert->get_obj_hash() == blk_hash;
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == blk_hash;
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<vote "
          << "rid=" << std::to_string(voter) << " "
          << "blk=" << get_hex10(blk_hash) << ">";
        return s;
    }
};


/** Abstraction for LComplain messages. */
struct LComplain: public Serializable {
    ReplicaID voter;

    int blk_height;
    /** block being prepared */
    uint256_t blk_hash;
    /** proof of validity for the vote */
    part_cert_bt cert;

    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    LComplain(): cert(nullptr), hsc(nullptr) {}
    LComplain(ReplicaID voter,
              int blk_height,
              const uint256_t &blk_hash,
              part_cert_bt &&cert,
              HotStuffCore *hsc):
            voter(voter),
            blk_height(blk_height),
            blk_hash(blk_hash),
            cert(std::move(cert)), hsc(hsc) {}

    LComplain(const LComplain &other):
            voter(other.voter),
            blk_height(other.blk_height),
            blk_hash(other.blk_hash),
            cert(other.cert ? other.cert->clone() : nullptr),
            hsc(other.hsc) {}

    LComplain(LComplain &&other) = default;

    void serialize(DataStream &s) const override {
        s << voter << blk_height << blk_hash << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> voter >> blk_height >> blk_hash;
        cert = hsc->parse_part_cert(s);
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter)) &&
               cert->get_obj_hash() == blk_hash;
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == blk_hash;
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<LComplain "
          << "rid=" << std::to_string(voter) << " "
          << "height=" << std::to_string(blk_height) << " "
          << "blk=" << get_hex10(blk_hash) << ">";
        return s;
    }
};






/** Abstraction for recs messages. */
struct recs: public Serializable {
    int cluster_number;
    int node_number;
    int reconfig_mode;


    recs(): cluster_number(-1), node_number(-1), reconfig_mode(-1) {}
    recs(int cluster_number,
    int node_number,
    int reconfig_mode):
            cluster_number(cluster_number),
            node_number(node_number),
            reconfig_mode(reconfig_mode){}


    recs(const recs &other):
            cluster_number(other.cluster_number),
            node_number(other.node_number),
            reconfig_mode(other.reconfig_mode) {}


    recs(recs &&other) = default;

    void serialize(DataStream &s) const override {
        s << cluster_number << node_number << reconfig_mode;
    }

    void unserialize(DataStream &s) override {
        s >> cluster_number >> node_number >> reconfig_mode;
    }


    // Define equality operator
    bool operator==(const recs& other) const {
        return cluster_number == other.cluster_number && node_number == other.node_number
        && reconfig_mode == other.reconfig_mode;
    }



    operator std::string () const {
        DataStream s;
        s << "<recs "
          << "cluster_number=" << std::to_string(cluster_number) << " "
          << "node_number=" << std::to_string(node_number) << " "
          << "reconfig_mode=" << std::to_string(reconfig_mode) << ">";
        return s;
    }
};






// Custom hash function for recs
struct recsHash {
    std::size_t operator()(const recs& r) const {
        return std::hash<int>()(r.cluster_number) ^
               (std::hash<int>()(r.node_number) << 1) ^
               (std::hash<int>()(r.reconfig_mode) << 2);
    }
};



/** Abstraction for RComplain messages. */
struct RComplain: public Serializable {
    ReplicaID voter;

    int blk_height;

    /** block being prepared */

    uint256_t blk_hash;
    /** proof of validity for the vote */
    part_cert_bt cert;

    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    RComplain(): cert(nullptr), hsc(nullptr) {}
    RComplain(ReplicaID voter,
              int blk_height,
              const uint256_t &blk_hash,
              part_cert_bt &&cert,
              HotStuffCore *hsc):
            voter(voter),
            blk_height(blk_height),
            blk_hash(blk_hash),
            cert(std::move(cert)), hsc(hsc) {}

    RComplain(const RComplain &other):
            voter(other.voter),
            blk_height(other.blk_height),
            blk_hash(other.blk_hash),
            cert(other.cert ? other.cert->clone() : nullptr),
            hsc(other.hsc) {}

    RComplain(RComplain &&other) = default;

    void serialize(DataStream &s) const override {
        s << voter << blk_height << blk_hash << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> voter >> blk_height >> blk_hash;
        cert = hsc->parse_part_cert(s);
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter)) &&
               cert->get_obj_hash() == blk_hash;
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == blk_hash;
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<RComplain "
            << "rid=" << std::to_string(voter) << " "
            << "height=" << std::to_string(blk_height) << " "
            << "blk=" << get_hex10(blk_hash) << ">";
        return s;
    }
};



struct Finality: public Serializable {
    ReplicaID rid;
    int8_t decision;
    uint32_t cmd_idx;
    uint32_t cmd_height;
    uint256_t cmd_hash;
    uint256_t blk_hash;

    public:
    Finality() = default;
    Finality(ReplicaID rid,
            int8_t decision,
            uint32_t cmd_idx,
            uint32_t cmd_height,
            uint256_t cmd_hash,
            uint256_t blk_hash):
        rid(rid), decision(decision),
        cmd_idx(cmd_idx), cmd_height(cmd_height),
        cmd_hash(cmd_hash), blk_hash(blk_hash) {}

    void serialize(DataStream &s) const override {
        s << rid << decision
          << cmd_idx << cmd_height
          << cmd_hash;
        if (decision == 1) s << blk_hash;
    }

    void unserialize(DataStream &s) override {
        s >> rid >> decision
          >> cmd_idx >> cmd_height
          >> cmd_hash;
        if (decision == 1) s >> blk_hash;
    }

    operator std::string () const {
        DataStream s;
        s << "<fin "
          << "decision=" << std::to_string(decision) << " "
          << "cmd_idx=" << std::to_string(cmd_idx) << " "
          << "cmd_height=" << std::to_string(cmd_height) << " "
          << "cmd=" << get_hex10(cmd_hash) << " "
          << "blk=" << get_hex10(blk_hash) << ">";
        return s;
    }
};

}

#endif
