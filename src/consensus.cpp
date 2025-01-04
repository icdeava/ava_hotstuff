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

#include <cassert>
#include <stack>
//#include<boost>

#include "hotstuff/util.h"
#include "hotstuff/consensus.h"

#include <chrono>
#include <thread>
#include <sstream>

#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN
#define LOG_PROTO HOTSTUFF_LOG_INFO

namespace hotstuff {

/* The core logic of HotStuff, is fairly simple :). */
/*** begin HotStuff protocol logic ***/
HotStuffCore::HotStuffCore(ReplicaID id, int cluster_id,
                            privkey_bt &&priv_key):
        b0(new Block(true, 1)),
        b_lock(b0),
        b_exec(b0),
        vheight(0),
        cluster_id(cluster_id),
        priv_key(std::move(priv_key)),
        tails{b0},
        vote_disabled(false),
        id(id),
        storage(new EntityStorage()) {
    storage->add_blk(b0);
}

void HotStuffCore::sanity_check_delivered(const block_t &blk) {
    if (!blk->delivered)
        throw std::runtime_error("block not delivered");
}

block_t HotStuffCore::get_delivered_blk(const uint256_t &blk_hash) {
    block_t blk = storage->find_blk(blk_hash);

    if (blk == nullptr || !blk->delivered)
        throw std::runtime_error("block not delivered");
    return blk;
}

bool HotStuffCore::on_deliver_blk(const block_t &blk) {
//    LOG_INFO("function on_deliver_blk: START");

    if (blk->delivered)
    {
        LOG_WARN("attempt to deliver a block twice: %s", std::string(*blk).c_str());
        return false;
    }
    blk->parents.clear();
    for (const auto &hash: blk->parent_hashes)
        blk->parents.push_back(get_delivered_blk(hash));
    blk->height = blk->parents[0]->height + 1;

    if (blk->qc)
    {
        block_t _blk = storage->find_blk(blk->qc->get_obj_hash());
        if (_blk == nullptr)
            throw std::runtime_error("block referred by qc not fetched");
        blk->qc_ref = std::move(_blk);
    } // otherwise blk->qc_ref remains null

    for (auto pblk: blk->parents) tails.erase(pblk);
    tails.insert(blk);

    blk->delivered = true;

    LOG_INFO("deliver %s", std::string(*blk).c_str());

    store_in_map_for_mc(blk);

//    LOG_INFO("function on_deliver_blk: END");

    return true;
}

void HotStuffCore::update_hqc(const block_t &_hqc, const quorum_cert_bt &qc) {
//    LOG_INFO("function update_hqc: START");

    if (_hqc->height > hqc.first->height)
    {
        hqc = std::make_pair(_hqc, qc->clone());
        on_hqc_update();
    }
//    LOG_INFO("function update_hqc: END");

}

void HotStuffCore::update(const block_t &nblk) {
//    LOG_INFO("function update: START");

    /* nblk = b*, blk2 = b'', blk1 = b', blk = b */
#ifndef HOTSTUFF_TWO_STEP
    /* three-step HotStuff */
    const block_t &blk2 = nblk->qc_ref;
    if (blk2 == nullptr)
    {
//        LOG_INFO("function update returning here 1");
        return;
    }
    /* decided blk could possible be incomplete due to pruning */
    if (blk2->decision)
    {
//        LOG_INFO("function update returning here 2");
        return;
    }
    update_hqc(blk2, nblk->qc);

    const block_t &blk1 = blk2->qc_ref;


    if (blk1 == nullptr)
    {
//        LOG_INFO("function update returning here 3");
        return;
    }
    if (blk1->decision)
    {
//        LOG_INFO("function update returning here 4");
        return;
    }
    if (blk1->height > b_lock->height) b_lock = blk1;

    const block_t &blk = blk1->qc_ref;
    if (blk == nullptr)
    {
//        LOG_INFO("function update returning here 5");
        return;
    }
    if (blk->decision)
    {
//        LOG_INFO("function update returning here 6");
        return;
    }

    /* commit requires direct parent */
    if (blk2->parents[0] != blk1 || blk1->parents[0] != blk)
    {
//        LOG_INFO("function update returning here 7");
        return;
    }
#else
    /* two-step HotStuff */
    const block_t &blk1 = nblk->qc_ref;
    if (blk1 == nullptr) return;
    if (blk1->decision) return;
    update_hqc(blk1, nblk->qc);
    if (blk1->height > b_lock->height) b_lock = blk1;

    const block_t &blk = blk1->qc_ref;
    if (blk == nullptr) return;
    if (blk->decision) return;

    /* commit requires direct parent */
    if (blk1->parents[0] != blk) return;
#endif
    /* otherwise commit */
    std::vector<block_t> commit_queue;
    block_t b;

//    b->cluster_number = 19;

    for (b = blk; b->height > b_exec->height; b = b->parents[0])
    { /* TODO: also commit the uncles/aunts */
        commit_queue.push_back(b);
    }

    LOG_INFO("commit_queue size is %d",int(commit_queue.size()));

    if (b != b_exec)
        throw std::runtime_error("safety breached :( " +
                                std::string(*blk) + " " +
                                std::string(*b_exec));
    for (auto it = commit_queue.rbegin(); it != commit_queue.rend(); it++)
    {
        const block_t &blk = *it;
        blk->decision = 1;

        do_consensus(blk);





        LOG_INFO("Going to send multi cluster message") ;

        {
            auto [reconfig_c, leave_set_sent] = get_leave_set();
            Proposal prop_other_clusters(id, blk, nullptr, cluster_id, cluster_id, blk->height,
                                         1, leave_set_sent, reconfig_c);


            bool leader_check = check_leader();
            if (leader_check)
            {

                if (!(cluster_id==0 && blk->height==-6000))
                {
                    do_broadcast_proposal_other_clusters(prop_other_clusters);
                }

            }
        }


        LOG_INFO("remote view change timer starting ");

        start_remote_view_change_timer(int(blk->height), blk);


        update_finished_update_cids(int(blk->height));

        LOG_INFO("did_receive_mc for blk height = %d, is %d ", int(blk->height),
                int( did_receive_mc(int(blk->height)))
                 );
        if (did_receive_mc(int(blk->height)))
        {

            for (size_t i = 0; i < (blk->get_cmds()).size(); i++) {
                LOG_INFO("Since MC Received already, do_decide for height:%d, btemp = %s",
                         int(blk->get_height()), std::string(*blk).c_str());



                    do_decide(Finality(id, 1, i, blk->height,
                                       blk->cmds[i], blk->get_hash()), blk->keys[i], blk->vals[i]);
            }

            reset_remote_view_change_timer(int(blk->height));


        }



        LOG_PROTO("---->commit %s, blk->height is %d", std::string(*blk).c_str(), int(blk->height));


    }

    b_exec = blk;



}

block_t HotStuffCore::on_propose(const std::vector<uint256_t> &cmds, const std::vector<int> &keys, const std::vector<int> &vals,
                            const std::vector<block_t> &parents,
                            bytearray_t &&extra) {
//    LOG_INFO("function on_propose: START");

    if (parents.empty())
        throw std::runtime_error("empty parents");
    for (const auto &_: parents) tails.erase(_);
    /* create the new block */

    block_t bnew = storage->add_blk(
            new Block(parents, cmds, keys, vals,
                      hqc.second->clone(), std::move(extra),
                      parents[0]->height + 1,
                      hqc.first,
                      nullptr
            ));

    LOG_INFO("NewBlock: height: %d with blk = %s with height = %d, blk_hash=%d",int(parents[0]->height + 1), std::string(*bnew).c_str(),
             bnew->height, bnew->get_hash() ) ;

    const uint256_t bnew_hash = bnew->get_hash();
    bnew->self_qc = create_quorum_cert(bnew_hash);
    on_deliver_blk(bnew);
    update(bnew);
    Proposal prop(id, bnew, nullptr);
    LOG_PROTO("propose %s", std::string(*bnew).c_str());
    if (bnew->height <= vheight)
        throw std::runtime_error("new block should be higher than vheight");
    /* self-receive the proposal (no need to send it through the network) */
    on_receive_proposal(prop);
    on_propose_(prop);
    /* boradcast to other replicas */

    if (int(parents[0]->height + 1)==6000) LOG_INFO("LatencyPlot: Processing message from client") ;




    do_broadcast_proposal(prop);

    return bnew;
}


void HotStuffCore::update_config_parameters(std::vector<PeerId> current_peers)
{
    config.nreplicas = current_peers.size()+1;
    int nfaulty = current_peers.size()/3;
    config.nmajority = config.nreplicas - nfaulty;
}




void HotStuffCore::on_receive_proposal(const Proposal &prop) {

    LOG_PROTO("got %s", std::string(prop).c_str());
    bool self_prop = prop.proposer == get_id();
    block_t bnew = prop.blk;
    if (!self_prop)
    {
        sanity_check_delivered(bnew);
        update(bnew);
    }
    bool opinion = false;
    if (bnew->height > vheight)
    {
        if (bnew->qc_ref && bnew->qc_ref->height > b_lock->height)
        {
            opinion = true; // liveness condition
            vheight = bnew->height;
        }
        else
        {   // safety condition (extend the locked branch)
            block_t b;
            for (b = bnew;
                b->height > b_lock->height;
                b = b->parents[0]);
            if (b == b_lock) /* on the same branch */
            {
                opinion = true;
                vheight = bnew->height;
            }
        }
    }
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    if (!self_prop && bnew->qc_ref)
        on_qc_finish(bnew->qc_ref);
    on_receive_proposal_(prop);
    if (opinion && !vote_disabled)
        do_vote(prop.proposer,
            Vote(id, bnew->get_hash(),
                create_part_cert(*priv_key, bnew->get_hash()), this));

}

void HotStuffCore::on_receive_vote(const Vote &vote) {
//    LOG_INFO("function on_receive_vote: START");


    LOG_PROTO("got %s", std::string(vote).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    HOTSTUFF_LOG_INFO("---> received vote from %d",
                      vote.voter);

    block_t blk = get_delivered_blk(vote.blk_hash);
    assert(vote.cert);
    size_t qsize = blk->voted.size();

    HOTSTUFF_LOG_INFO("qsize: %d, config.nmajority: %d",
                      int(qsize), int(config.nmajority));

    if (qsize >= config.nmajority) return;
    if (!blk->voted.insert(vote.voter).second)
    {
        LOG_WARN("duplicate vote for %s from %d", get_hex10(vote.blk_hash).c_str(), vote.voter);
        return;
    }
    auto &qc = blk->self_qc;
    if (qc == nullptr)
    {
        LOG_WARN("vote for block not proposed by itself");
        qc = create_quorum_cert(blk->get_hash());
    }
    qc->add_part(vote.voter, *vote.cert);
    if (qsize + 1 == config.nmajority)
    {
        LOG_INFO("-----> QUORUM REACHED");
        qc->compute();
        update_hqc(blk, qc);
        on_qc_finish(blk);
    }


}





void HotStuffCore::start_local_complain(int height, const uint256_t &blk_hash ) {


    LComplain lcmpl(id, height, blk_hash,
                                create_part_cert(*priv_key, blk_hash), this);
    send_LComplain(id, lcmpl);

    on_receive_LComplain(lcmpl);



}





void HotStuffCore::on_receive_LComplain(const LComplain &vote) {
//    LOG_PROTO("now state: %s", std::string(*this).c_str());
    block_t blk = get_delivered_blk(vote.blk_hash);


    assert(vote.cert);

    size_t qsize = blk->lcomplains.size();
    LOG_PROTO("got LComplain %s, with qsize = %d", std::string(vote).c_str(), int(qsize));

//    LOG_PROTO("here on receiving LComplain");

    if (qsize > config.nmajority) {
        LOG_INFO("returning due to qsize > majority, qsize = %d", int(qsize));
        return;
    }
    if (!blk->lcomplains.insert(vote.voter).second)
    {
        LOG_INFO("duplicate LComplain for %s from %d", get_hex10(vote.blk_hash).c_str(), vote.voter);
        return;
    }
    LOG_INFO("qsize = %d, config.nreplicas = %d, config.nmajority = %d", int(qsize+1) , int(config.nreplicas), int(config.nmajority));

    if (qsize + 1 == config.nreplicas- config.nmajority+1)
    {
        // amplify lcomplaint
        HOTSTUFF_LOG_INFO("Amplifying LComplain for height = %d with current qsize = ", vote.blk_height, int(qsize));

//        send_LComplain(id,
//                     vote);
    }

    if (qsize + 1 == config.nmajority)
    {
        // send rcomplaint

        HOTSTUFF_LOG_INFO("Sending RComplain for height = %d", vote.blk_height);

        RComplain rcmpl(id, vote.blk_height, vote.blk_hash,
                        create_part_cert(*priv_key, vote.blk_hash), this);
        
        send_RComplain(id, rcmpl);


    }

}



void HotStuffCore::on_receive_RComplain(const RComplain &vote) {
    LOG_PROTO("got RComplain %s", std::string(vote).c_str());
    block_t blk = get_delivered_blk(vote.blk_hash);
    assert(vote.cert);
    size_t qsize = blk->rcomplains.size();

    if (qsize >= 1) return;
    if (!blk->rcomplains.insert(vote.voter).second)
    {
        LOG_WARN("duplicate RComplain for %s from %d", get_hex10(vote.blk_hash).c_str(), vote.voter);
        return;
    }

    LOG_INFO("RComplain, qsize = %d, config.nreplicas = %d, config.nmajority = %d", int(qsize+1) , int(config.nreplicas), int(config.nmajority));

    if (qsize + 1 == 1)
    {
        // send to others
        LOG_INFO("sending RComplain to other local members");
        send_RComplain_local(id, vote);
    }
}

int HotStuffCore::get_recs_majority()
{
    return config.nmajority;
}

/*** end HotStuff protocol logic ***/
void HotStuffCore::on_init(uint32_t nfaulty) {
    config.nmajority = config.nreplicas - nfaulty;
    b0->qc = create_quorum_cert(b0->get_hash());
    b0->qc->compute();
    b0->self_qc = b0->qc->clone();
    b0->qc_ref = b0;
    hqc = std::make_pair(b0, b0->qc->clone());


    }





    void HotStuffCore::prune(uint32_t staleness) {
    block_t start;
    /* skip the blocks */
    for (start = b_exec; staleness; staleness--, start = start->parents[0])
        if (!start->parents.size()) return;
    std::stack<block_t> s;
    start->qc_ref = nullptr;
    s.push(start);
    while (!s.empty())
    {
        auto &blk = s.top();
        if (blk->parents.empty())
        {
            storage->try_release_blk(blk);
            s.pop();
            continue;
        }
        blk->qc_ref = nullptr;
        s.push(blk->parents.back());
        blk->parents.pop_back();
    }
}

void HotStuffCore::add_replica(ReplicaID rid, const PeerId &peer_id,
                                pubkey_bt &&pub_key) {
    config.add_replica(rid,
            ReplicaInfo(rid, peer_id, std::move(pub_key)));
    b0->voted.insert(rid);
}

promise_t HotStuffCore::async_qc_finish(const block_t &blk) {
    LOG_INFO("function async_qc_finish:START");
    if (blk->voted.size() >= config.nmajority)
        return promise_t([](promise_t &pm) {
            LOG_INFO("qc_waiting: promise resolved");
            pm.resolve();
        });
    auto it = qc_waiting.find(blk);
    if (it == qc_waiting.end())
        it = qc_waiting.insert(std::make_pair(blk, promise_t())).first;

    LOG_INFO("function async_qc_finish:END");

    return it->second;


}

void HotStuffCore::on_qc_finish(const block_t &blk) {
    LOG_DEBUG("function on_qc_finish:START");
    auto it = qc_waiting.find(blk);
    if (it != qc_waiting.end())
    {
        LOG_DEBUG("qc_waiting: promise resolved");
        it->second.resolve();
        qc_waiting.erase(it);
    }

    LOG_DEBUG("function on_qc_finish:END");

}

promise_t HotStuffCore::async_wait_proposal() {
    LOG_DEBUG("function async_wait_proposal:START");

    return propose_waiting.then([](const Proposal &prop) {
        LOG_DEBUG("function async_wait_proposal:END");

        return prop;
    });
}


promise_t HotStuffCore::async_wait_other_cluster() {
    LOG_DEBUG("function async_wait_other_cluster:START");

    return other_cluster_waiting.then([this](const Proposal &prop) {
        LOG_DEBUG("function async_wait_other_cluster:END");



        return prop;
    });
}





promise_t HotStuffCore::async_wait_receive_proposal() {
    LOG_DEBUG("function async_wait_receive_proposal:START");
    return receive_proposal_waiting.then([](const Proposal &prop) {
        LOG_DEBUG("function async_wait_receive_proposal:END");

        return prop;
    });
}



promise_t HotStuffCore::async_hqc_update() {
    LOG_DEBUG("function async_hqc_update:START");

    return hqc_update_waiting.then([this]() {
        LOG_DEBUG("function async_hqc_update:END");

        return hqc.first;
    });
}

void HotStuffCore::on_propose_(const Proposal &prop) {
    auto t = std::move(propose_waiting);
    propose_waiting = promise_t();

    LOG_DEBUG("propose_waiting: promise resolved");
    t.resolve(prop);
}



void HotStuffCore::on_receive_proposal_(const Proposal &prop) {
    auto t = std::move(receive_proposal_waiting);
    receive_proposal_waiting = promise_t();
    LOG_DEBUG("receive_proposal_waiting: promise resolved");
    t.resolve(prop);
}

//
void HotStuffCore::on_receive_other_cluster_() {
//    LOG_INFO("function on_receive_other_cluster_:START");
    auto t = std::move(other_cluster_waiting);
    other_cluster_waiting = promise_t();
        LOG_DEBUG("other_cluster_waiting: promise resolved");
    t.resolve();
//    LOG_INFO("function on_receive_other_cluster_:END");

    }

void HotStuffCore::on_receive_other_cluster_(int cid) {
    LOG_DEBUG("on_receive_other_cluster_ for cid %d", cid);

    decide_after_mc(cid);

//    LOG_INFO("function on_receive_other_cluster_:END");

}


void HotStuffCore::on_hqc_update() {
//    LOG_INFO("function on_hqc_update:START");
    auto t = std::move(hqc_update_waiting);
    hqc_update_waiting = promise_t();
    LOG_DEBUG("hqc_update_waiting: promise resolved");
    t.resolve();
//    LOG_INFO("function on_hqc_update:END");


}

HotStuffCore::operator std::string () const {
    DataStream s;
    s << "<hotstuff "
      << "hqc=" << get_hex10(hqc.first->get_hash()) << " "
      << "hqc.height=" << std::to_string(hqc.first->height) << " "
      << "b_lock=" << get_hex10(b_lock->get_hash()) << " "
      << "b_exec=" << get_hex10(b_exec->get_hash()) << " "
      << "vheight=" << std::to_string(vheight) << " "
      << "tails=" << std::to_string(tails.size()) << ">";
    return s;
}

}
