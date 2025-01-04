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

#include "hotstuff/hotstuff.h"
#include "hotstuff/client.h"
#include "hotstuff/liveness.h"

using salticidae::static_pointer_cast;
using salticidae::get_hex10;


#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN


const int My_AGG = 0;
const int My_ECHO = 1;
const int My_READY = 2;

namespace hotstuff {

    const opcode_t MsgPropose::opcode;
    MsgPropose::MsgPropose(const Proposal &proposal) { serialized << proposal; }
    void MsgPropose::postponed_parse(HotStuffCore *hsc) {
        proposal.hsc = hsc;
        serialized >> proposal;
    }


    const opcode_t MsgVote::opcode;
    MsgVote::MsgVote(const Vote &vote) { serialized << vote; }
    void MsgVote::postponed_parse(HotStuffCore *hsc) {
        vote.hsc = hsc;
        serialized >> vote;
    }




    const opcode_t MsgLComplain::opcode;
    MsgLComplain::MsgLComplain(const LComplain &vote) { serialized << vote; }
    void MsgLComplain::postponed_parse(HotStuffCore *hsc) {
        vote.hsc = hsc;
        serialized >> vote;
    }


    const opcode_t MsgRecs::opcode;
    MsgRecs::MsgRecs(const recs &reconfig_vars_single) { serialized << reconfig_vars_single; }
    void MsgRecs::postponed_parse(HotStuffCore *hsc) {

        serialized >> reconfig_vars_single;
    }

    const opcode_t MsgRecsAggEchoReady::opcode;
    MsgRecsAggEchoReady::MsgRecsAggEchoReady(const recs &reconfig_vars_single, const int &type, const int& reconfig_count)
    {
        serialized << reconfig_vars_single << type << reconfig_count;
    }
    void MsgRecsAggEchoReady::postponed_parse(HotStuffCore *hsc) {

        serialized >> reconfig_vars_single >> type >> reconfig_count;
    }


    const opcode_t MsgRComplain::opcode;
    MsgRComplain::MsgRComplain(const RComplain &vote) { serialized << vote; }
    void MsgRComplain::postponed_parse(HotStuffCore *hsc) {
        vote.hsc = hsc;
        serialized >> vote;
    }


    const opcode_t MsgReqBlock::opcode;
    MsgReqBlock::MsgReqBlock(const std::vector<uint256_t> &blk_hashes) {
        serialized << htole((uint32_t)blk_hashes.size());
        for (const auto &h: blk_hashes)
            serialized << h;
    }

    MsgReqBlock::MsgReqBlock(DataStream &&s) {
        uint32_t size;
        s >> size;
        size = letoh(size);
        blk_hashes.resize(size);
        for (auto &h: blk_hashes) s >> h;
    }

    const opcode_t MsgRespBlock::opcode;
    MsgRespBlock::MsgRespBlock(const std::vector<block_t> &blks) {
        serialized << htole((uint32_t)blks.size());
        for (auto blk: blks) serialized << *blk;
    }

    void MsgRespBlock::postponed_parse(HotStuffCore *hsc) {
        uint32_t size;
        serialized >> size;
        size = letoh(size);
        blks.resize(size);
        for (auto &blk: blks)
        {
            Block _blk;
            _blk.unserialize(serialized, hsc);
            HOTSTUFF_LOG_INFO("MsgRespBlock::postponed_parse: _blk.get_height() is %d",
                              int(_blk.get_height()) );
            blk = hsc->storage->add_blk(std::move(_blk), hsc->get_config());
        }
    }




// TODO: improve this function
    void HotStuffBase::exec_command(const ClientCommandVars &ccv, commit_cb_t callback) {

//        HOTSTUFF_LOG_INFO("exec_command, adding to cmd_pending with current size with key= %d, val = %d,"
//                          "cluster_num = %d, node_num = %d, reconfig_mode = %d",
//                          ccv.key, ccv.val, ccv.cluster_number, ccv.node_number, ccv.reconfig_mode);


        cmd_pending.enqueue(std::make_pair(ccv, callback));


    }

    void HotStuffBase::on_fetch_blk(const block_t &blk) {
#ifdef HOTSTUFF_BLK_PROFILE
        blk_profiler.get_tx(blk->get_hash());
#endif
        LOG_INFO("fetched %.10s", get_hex(blk->get_hash()).c_str());
        part_fetched++;
        fetched++;
        //for (auto cmd: blk->get_cmds()) on_fetch_cmd(cmd);
        const uint256_t &blk_hash = blk->get_hash();
        auto it = blk_fetch_waiting.find(blk_hash);
        if (it != blk_fetch_waiting.end())
        {
            it->second.resolve(blk);
            blk_fetch_waiting.erase(it);
        }
    }

    bool HotStuffBase::on_deliver_blk(const block_t &blk) {
        const uint256_t &blk_hash = blk->get_hash();
        bool valid;
        /* sanity check: all parents must be delivered */
        for (const auto &p: blk->get_parent_hashes())
            assert(storage->is_blk_delivered(p));
        if ((valid = HotStuffCore::on_deliver_blk(blk)))
        {
            LOG_DEBUG("block %.10s delivered",
                      get_hex(blk_hash).c_str());
            part_parent_size += blk->get_parent_hashes().size();
            part_delivered++;
            delivered++;
        }
        else
        {
            LOG_WARN("dropping invalid block");
        }

        bool res = true;
        auto it = blk_delivery_waiting.find(blk_hash);
        if (it != blk_delivery_waiting.end())
        {
            auto &pm = it->second;
            if (valid)
            {
                pm.elapsed.stop(false);
                auto sec = pm.elapsed.elapsed_sec;
                part_delivery_time += sec;
                part_delivery_time_min = std::min(part_delivery_time_min, sec);
                part_delivery_time_max = std::max(part_delivery_time_max, sec);

                pm.resolve(blk);
            }
            else
            {
                pm.reject(blk);
                res = false;
                // TODO: do we need to also free it from storage?
            }
            blk_delivery_waiting.erase(it);
        }
        return res;
    }

    promise_t HotStuffBase::async_fetch_blk(const uint256_t &blk_hash,
                                            const PeerId *replica,
                                            bool fetch_now) {
        if (storage->is_blk_fetched(blk_hash))
            return promise_t([this, &blk_hash](promise_t pm){
                pm.resolve(storage->find_blk(blk_hash));
            });
        auto it = blk_fetch_waiting.find(blk_hash);
        if (it == blk_fetch_waiting.end())
        {
#ifdef HOTSTUFF_BLK_PROFILE
            blk_profiler.rec_tx(blk_hash, false);
#endif
            HOTSTUFF_LOG_INFO("Adding to blk_fetch_waiting %.10s", get_hex(blk_hash).c_str());

            it = blk_fetch_waiting.insert(
                    std::make_pair(
                            blk_hash,
                            BlockFetchContext(blk_hash, this))).first;
        }
        if (replica != nullptr)
            it->second.add_replica(*replica, fetch_now);
        return static_cast<promise_t &>(it->second);
    }

    promise_t HotStuffBase::async_deliver_blk(const uint256_t &blk_hash,
                                              const PeerId &replica) {
        if (storage->is_blk_delivered(blk_hash))
            return promise_t([this, &blk_hash](promise_t pm) {
                pm.resolve(storage->find_blk(blk_hash));
            });
        auto it = blk_delivery_waiting.find(blk_hash);
        if (it != blk_delivery_waiting.end())
            return static_cast<promise_t &>(it->second);
        BlockDeliveryContext pm{[](promise_t){}};
        it = blk_delivery_waiting.insert(std::make_pair(blk_hash, pm)).first;

        /* otherwise the on_deliver_batch will resolve */
        async_fetch_blk(blk_hash, &replica).then([this, replica](block_t blk) {
            /* qc_ref should be fetched */
            std::vector<promise_t> pms;
            const auto &qc = blk->get_qc();
            assert(qc);
            if (blk == get_genesis())
                pms.push_back(promise_t([](promise_t &pm){ pm.resolve(true); }));
            else
                pms.push_back(blk->verify(this, vpool));
            pms.push_back(async_fetch_blk(qc->get_obj_hash(), &replica));
            /* the parents should be delivered */
            for (const auto &phash: blk->get_parent_hashes())
                pms.push_back(async_deliver_blk(phash, replica));
            promise::all(pms).then([this, blk](const promise::values_t values) {

                HOTSTUFF_LOG_INFO("promise::any_cast<bool>(values[0]) is %d and this->on_deliver_blk(blk) is %d",
                                  promise::any_cast<bool>(values[0]), this->on_deliver_blk(blk));
                auto ret = promise::any_cast<bool>(values[0]) && this->on_deliver_blk(blk);
                if (!ret)
                    HOTSTUFF_LOG_WARN("verification failed during async delivery");
            });
        });
        return static_cast<promise_t &>(pm);
    }

    void HotStuffBase::propose_handler(MsgPropose &&msg, const Net::conn_t &conn) {
        const PeerId &peer = conn->get_peer_id();




        if (peer.is_null()) return;


        msg.postponed_parse(this);
        auto &prop = msg.proposal;
        block_t blk = prop.blk;





        if (!blk) {
            LOG_WARN("Returning due to null block with msg type: %d", prop.msg_type);
            return;
        }

        if ((cluster_id==prop.cluster_number) && (peer != get_config().get_peer_id(prop.proposer)) && (prop.msg_type<3))
        {
            LOG_WARN("invalid proposal from %d, prop.msg_type: %d", prop.proposer, prop.msg_type);
            return;
        }


        if (prop.msg_type==1)
        {

            if (int(prop.other_cluster_block_height)==6000) LOG_INFO("LatencyPlot: Received 1st MC message") ;

            std::unordered_set<int> received_leave_set = prop.current_peer_set;


            if (received_leave_set.size() > 0)
            {

                for (const int& element : received_leave_set) {
                    HOTSTUFF_LOG_INFO("received other_leave_set from 1st mc message element: %d", element);
                }

            }


            LOG_INFO("1st MC message: Reached here proposer %d, cluster_id = %d, prop.cluster number  = %d, prop.other_cluster_block_height=%d, height = %d, storage->get_blk_cache_size is %d, cluster_msg_count is %d, prop.other_cluster_block_height > cluster_msg_count = %d ",
                     prop.proposer, cluster_id, prop.cluster_number, prop.other_cluster_block_height, blk->get_height(), int(storage->get_blk_cache_size()), cluster_msg_count, int(prop.other_cluster_block_height > cluster_msg_count) );

            Proposal prop_same_cluster(id, blk, nullptr, cluster_id, prop.cluster_number, prop.other_cluster_block_height, 2, received_leave_set, prop.reconfig_count);

            auto finished_mc_cids_it = finished_mc_cids.find(int(prop.other_cluster_block_height));


            if (finished_mc_cids_it!=finished_mc_cids.end())
            {
                return;
            }

            do_broadcast_proposal(prop_same_cluster);

            receive_mc_local(prop_same_cluster);

            if (int(prop.other_cluster_block_height)==6000) LOG_INFO("LatencyPlot: Sent 2nd MC message") ;

            return;
        }


        if ((prop.msg_type==2))
        {
            receive_mc_local(prop);
            return;
        }


        LOG_WARN("4: proposal from %d, prop.msg_type: %d, cluster_id = %d, prop.cluster_number = %d, int(cluster_id==prop.cluster_number) = %d",
                 prop.proposer, prop.msg_type, cluster_id, prop.cluster_number, int(cluster_id==prop.cluster_number));


        promise::all(std::vector<promise_t>{
                async_deliver_blk(blk->get_hash(), peer)
        }).then([this, prop = std::move(prop)]() {
            on_receive_proposal(prop);
        });
    }


    void HotStuffBase::LComplain_handler(MsgLComplain &&msg, const Net::conn_t &conn) {
        const auto &peer = conn->get_peer_id();
        if (peer.is_null()) return;
        msg.postponed_parse(this);

        RcObj<LComplain> v(new LComplain(std::move(msg.vote)));
        promise::all(std::vector<promise_t>{
                async_deliver_blk(v->blk_hash, peer),
                v->verify(vpool),
        }).then([this, v=std::move(v)](const promise::values_t values) {
            if (!promise::any_cast<bool>(values[1]))
                LOG_WARN("invalid vote from %d", v->voter);
            else
                on_receive_LComplain(*v);
        });
    }

    void HotStuffBase::MsgRecs_handler(hotstuff::MsgRecs &&msg, const Net::conn_t &conn) {
        HOTSTUFF_LOG_INFO("MsgRecs_handler activated for connection %s", std::string(conn->get_peer_addr()).c_str());
        const auto &peer = conn->get_peer_id();

        if (peer.is_null()) return;
        msg.postponed_parse(this);


        ReplicaID proposer = pmaker->get_proposer();

        if (id==proposer)
        {


            pending_recs[msg.reconfig_vars_single]++;

            if (pending_recs[msg.reconfig_vars_single] > get_recs_majority())
            {


                HOTSTUFF_LOG_INFO("Returning due to got MsgRecs %s with count = %d greater than majority",
                                  std::string(msg.reconfig_vars_single).c_str(), pending_recs[msg.reconfig_vars_single]);
                return;
            }
            if (pending_recs[msg.reconfig_vars_single] == get_recs_majority())
            {

                reconfig_count++;

                HOTSTUFF_LOG_INFO("got MsgRecs %s with count = %d equal to majority, sending agg message",
                                  std::string(msg.reconfig_vars_single).c_str(), pending_recs[msg.reconfig_vars_single]);


                pn.multicast_msg(MsgRecsAggEchoReady(msg.reconfig_vars_single, My_AGG, reconfig_count), peers_current);


                pn.multicast_msg(MsgRecsAggEchoReady(msg.reconfig_vars_single, My_ECHO, reconfig_count), peers_current);
                reconfig_echo_counter[reconfig_count]++;

            }
        }

        return;

    }



    void HotStuffBase::MsgRecsAggEchoReady_handler(hotstuff::MsgRecsAggEchoReady &&msg, const Net::conn_t &conn) {
        HOTSTUFF_LOG_INFO("MsgRecsAggEchoReady_handler activated for connection %s",
                          std::string(conn->get_peer_addr()).c_str());


        const auto &peer = conn->get_peer_id();

        if (peer.is_null()) return;
        msg.postponed_parse(this);

        HOTSTUFF_LOG_INFO("MsgRecsAggEchoReady message of type %d received with reconfig_count = %d", msg.type, msg.reconfig_count);
        if (msg.type == My_AGG)
        {
            HOTSTUFF_LOG_INFO("Got reconfig AGG messages to send ECHO message");

            pn.multicast_msg(MsgRecsAggEchoReady(msg.reconfig_vars_single, My_ECHO, msg.reconfig_count), peers_current);

            // self receive
            reconfig_echo_counter[msg.reconfig_count]++;

        }

        if (msg.type == My_ECHO)
        {


            reconfig_echo_counter[msg.reconfig_count]++;

            HOTSTUFF_LOG_INFO("Got reconfig ECHO message with reconfig_echo_counter value being: %d",
                              reconfig_echo_counter[msg.reconfig_count]);

            if (reconfig_echo_counter[msg.reconfig_count] ==get_recs_majority())
            {
                HOTSTUFF_LOG_INFO("Got enough reconfig echo messages to send READY message");

                pn.multicast_msg(MsgRecsAggEchoReady(msg.reconfig_vars_single, My_READY, msg.reconfig_count), peers_current);

                pn.send_msg(MsgRecsAggEchoReady(msg.reconfig_vars_single, My_READY, msg.reconfig_count),all_peers[msg.reconfig_vars_single.node_number]);

                reconfig_ready_counter[msg.reconfig_count]++;
            }

        }

        if (msg.type == My_READY)
        {
            HOTSTUFF_LOG_INFO("got reconfig ready message for reconfig_count: %d, with count: %d ", msg.reconfig_count,
                              reconfig_ready_counter[msg.reconfig_count]);
            reconfig_ready_counter[msg.reconfig_count]++;

            if (reconfig_ready_counter[msg.reconfig_count] ==get_recs_majority())
            {

                HOTSTUFF_LOG_INFO("Got enough reconfig ready messages to reconfigure for node number: %d",
                                  msg.reconfig_vars_single.node_number);


                auto peer_to_be_reconfigured= all_peers[msg.reconfig_vars_single.node_number];
                int reconfig_mode = msg.reconfig_vars_single.reconfig_mode;


                if (reconfig_mode == 1)
                {
//                    HOTSTUFF_LOG_INFO("Added to leave set");
                    leave_set.insert(peer_to_be_reconfigured);
                    leave_set_int.insert(msg.reconfig_vars_single.node_number);

                    peers_current.clear();

                    for (const auto& peer : peers) {
                        if (leave_set.find(peer) == leave_set.end()) {
                            peers_current.push_back(peer);
                        }
                    }

                    update_config_parameters(peers_current);

                }
                if (reconfig_mode==0)
                {
                    HOTSTUFF_LOG_INFO("Removed from leave  set");

                    leave_set.erase(peer_to_be_reconfigured);
                    leave_set_int.erase(msg.reconfig_vars_single.node_number);

                    peers_current.clear();

                    for (const auto& peer : peers) {
                        if (leave_set.find(peer) == leave_set.end()) {
                            peers_current.push_back(peer);
                        }
                    }

                    update_config_parameters(peers_current);

                }


                HOTSTUFF_LOG_INFO("ready to be reconfigured, node number, cluster_number, reconfig_mode: (%d, %d, %d)", msg.reconfig_vars_single.node_number,
                         msg.reconfig_vars_single.cluster_number, msg.reconfig_vars_single.reconfig_mode);
            }

        }

        return;
    }



    void HotStuffBase::RComplain_handler(MsgRComplain &&msg, const Net::conn_t &conn) {

        LOG_WARN("received RComplain");
        const auto &peer = conn->get_peer_id();
        if (peer.is_null()) return;
        msg.postponed_parse(this);
        //auto &vote = msg.vote;
        RcObj<RComplain> v(new RComplain(std::move(msg.vote)));

        const uint256_t temp_blk_hash = storage->find_blk_hash_for_cid(v->blk_height);

        LOG_INFO("assigning local blk hash");
        v->blk_hash = temp_blk_hash;
        LOG_INFO("assigned local blk hash");
        on_receive_RComplain(*v);

    }

    void HotStuffBase::vote_handler(MsgVote &&msg, const Net::conn_t &conn) {

        LOG_INFO("function vote_handler:START");
        const auto &peer = conn->get_peer_id();
        if (peer.is_null()) return;
        msg.postponed_parse(this);
        //auto &vote = msg.vote;
        RcObj<Vote> v(new Vote(std::move(msg.vote)));
        promise::all(std::vector<promise_t>{
                async_deliver_blk(v->blk_hash, peer),
                v->verify(vpool),
        }).then([this, v=std::move(v)](const promise::values_t values) {
            if (!promise::any_cast<bool>(values[1]))
                LOG_WARN("invalid vote from %d", v->voter);
            else
            {
                LOG_INFO("going into on_receive_vote");
                on_receive_vote(*v);
            }
        });

        LOG_INFO("function vote_handler:END");

    }

    void HotStuffBase::req_blk_handler(MsgReqBlock &&msg, const Net::conn_t &conn)
    {
        const PeerId replica = conn->get_peer_id();


        LOG_INFO("got MsgReqBlock from %s", replica);

        if (replica.is_null()) return;

        auto &blk_hashes = msg.blk_hashes;

        std::vector<promise_t> pms;
        for (const auto &h: blk_hashes)
        {
            LOG_INFO("got MsgReqBlock from %s", replica);
            LOG_INFO("fetching after receiving MsgRespBlock");

            pms.push_back(async_fetch_blk(h, nullptr));

        }
        promise::all(pms).then([replica, this](const promise::values_t values) {
            std::vector<block_t> blks;
            for (auto &v: values)
            {
                auto blk = promise::any_cast<block_t>(v);

                LOG_INFO("adding block %s after receiving MsgReqBlock", std::string(*blk).c_str());

                blks.push_back(blk);
            }

            LOG_INFO("sending MsgRespBlock to replica: %s",(replica));

            pn.send_msg(MsgRespBlock(blks), replica);
        });
    }

    void HotStuffBase::resp_blk_handler(MsgRespBlock &&msg, const Net::conn_t &) {

        LOG_INFO("got MsgRespBlock");

        msg.postponed_parse(this);
        for (const auto &blk: msg.blks)
        {
            if (blk)
            {
                LOG_INFO("fetching block %s after receiving MsgRespBlock", std::string(*blk).c_str());
                on_fetch_blk(blk);
            }
        }
    }



    bool HotStuffBase::conn_handler(const salticidae::ConnPool::conn_t &conn, bool connected) {
        if (connected)
        {
            if (!pn.enable_tls) return true;
            auto cert = conn->get_peer_cert();
            //SALTICIDAE_LOG_INFO("%s", salticidae::get_hash(cert->get_der()).to_hex().c_str());
            return valid_tls_certs.count(salticidae::get_hash(cert->get_der()));
        }
        return true;
    }

    void HotStuffBase::print_stat() const {
        LOG_INFO("===== begin stats =====");
        LOG_INFO("-------- queues -------");
        LOG_INFO("blk_fetch_waiting: %lu", blk_fetch_waiting.size());
        LOG_INFO("blk_delivery_waiting: %lu", blk_delivery_waiting.size());
        LOG_INFO("decision_waiting: %lu", decision_waiting.size());
        LOG_INFO("-------- misc ---------");
        LOG_INFO("fetched: %lu", fetched);
        LOG_INFO("delivered: %lu", delivered);
        LOG_INFO("cmd_cache: %lu", storage->get_cmd_cache_size());
        LOG_INFO("blk_cache: %lu", storage->get_blk_cache_size());
        LOG_INFO("------ misc (10s) -----");
        LOG_INFO("fetched: %lu", part_fetched);
        LOG_INFO("delivered: %lu", part_delivered);
        LOG_INFO("decided: %lu", part_decided);
        LOG_INFO("nreplicas: %lu", get_config().nreplicas);
        LOG_INFO("gened: %lu", part_gened);
        LOG_INFO("avg. parent_size: %.3f",
                 part_delivered ? part_parent_size / double(part_delivered) : 0);
        LOG_INFO("delivery time: %.3f avg, %.3f min, %.3f max",
                 part_delivered ? part_delivery_time / double(part_delivered) : 0,
                 part_delivery_time_min == double_inf ? 0 : part_delivery_time_min,
                 part_delivery_time_max);

        part_parent_size = 0;
        part_fetched = 0;
        part_delivered = 0;
        part_decided = 0;
        part_gened = 0;
        part_delivery_time = 0;
        part_delivery_time_min = double_inf;
        part_delivery_time_max = 0;
#ifdef HOTSTUFF_MSG_STAT
        LOG_INFO("--- replica msg. (10s) ---");
        size_t _nsent = 0;
        size_t _nrecv = 0;
        for (const auto &replica: peers)
        {
            auto conn = pn.get_peer_conn(replica);
            if (conn == nullptr) continue;
            size_t ns = conn->get_nsent();
            size_t nr = conn->get_nrecv();
            size_t nsb = conn->get_nsentb();
            size_t nrb = conn->get_nrecvb();
            conn->clear_msgstat();
            LOG_INFO("%s: %u(%u), %u(%u), %u",
                     get_hex10(replica).c_str(), ns, nsb, nr, nrb, part_fetched_replica[replica]);
            _nsent += ns;
            _nrecv += nr;
            part_fetched_replica[replica] = 0;
        }
        nsent += _nsent;
        nrecv += _nrecv;
        LOG_INFO("sent: %lu", _nsent);
        LOG_INFO("recv: %lu", _nrecv);
        LOG_INFO("--- replica msg. total ---");
        LOG_INFO("sent: %lu", nsent);
        LOG_INFO("recv: %lu", nrecv);
#endif
        LOG_INFO("====== end stats ======");
    }

    HotStuffBase::HotStuffBase(uint32_t blk_size,
                               ReplicaID rid,
                               int cluster_id,
                               int n_clusters,
                               privkey_bt &&priv_key,
                               NetAddr listen_addr,
                               pacemaker_bt pmaker,
                               EventContext ec,
                               size_t nworker,
                               const Net::Config &netconfig):
            HotStuffCore(rid, cluster_id, std::move(priv_key)),
            listen_addr(listen_addr),
            blk_size(blk_size),
            cluster_id(cluster_id),
            n_clusters(n_clusters),
            ec(ec),
            tcall(ec),
            vpool(ec, nworker),
            pn(ec, netconfig),
            pmaker(std::move(pmaker)),

            fetched(0), delivered(0),
            nsent(0), nrecv(0),
            part_parent_size(0),
            part_fetched(0),
            part_delivered(0),
            part_decided(0),
            part_gened(0),
            part_delivery_time(0),
            part_delivery_time_min(double_inf),
            part_delivery_time_max(0),
            cluster_tracker_array()
    {


//        std::vector<int> cluster_tracker_array(n_clusters, 0);
//        cluster_tracker_array[cluster_id] = 1;

        /* register the handlers for msg from replicas */
        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::propose_handler, this, _1, _2));
        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::vote_handler, this, _1, _2));
        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::req_blk_handler, this, _1, _2));

        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::LComplain_handler, this, _1, _2));
        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::RComplain_handler, this, _1, _2));
        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::MsgRecs_handler, this, _1, _2));
        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::MsgRecsAggEchoReady_handler, this, _1, _2));



        pn.reg_handler(salticidae::generic_bind(&HotStuffBase::resp_blk_handler, this, _1, _2));
        pn.reg_conn_handler(salticidae::generic_bind(&HotStuffBase::conn_handler, this, _1, _2));

        pn.reg_error_handler([](const std::exception_ptr _err, bool fatal, int32_t async_id) {
            try {
                std::rethrow_exception(_err);
            } catch (const std::exception &err) {
                HOTSTUFF_LOG_WARN("network async error: %s\n", err.what());
            }
        });

        LOG_INFO("--- rid: %d",int(rid));



        pn.start();
        pn.listen(listen_addr);
    }

    void HotStuffBase::do_broadcast_proposal(const Proposal &prop) {

        if (leave_set.size() > 0)
        {
            HOTSTUFF_LOG_INFO("leave set size greater than 0, with peers.size() = %d, peers_current.size() = %d",
                              peers.size(), peers_current.size());
        }
//        HOTSTUFF_LOG_INFO("broadcasting to peers with size is %d\n", peers_current.size());

        pn.multicast_msg(MsgPropose(prop), peers_current);

    }





    void HotStuffBase::send_LComplain(ReplicaID last_proposer, const LComplain &vote) {
        pn.multicast_msg(MsgLComplain(vote), peers_current);
    }


    void HotStuffBase::send_RComplain_local(ReplicaID last_proposer, const RComplain &vote) {
        pn.multicast_msg(MsgRComplain(vote), peers_current);


        const uint256_t blk_hash = storage->find_blk_hash_for_cid(int(vote.blk_height));

        block_t btemp = storage->find_blk(blk_hash);

        HOTSTUFF_LOG_INFO("sending proposal due to remote complaint for blk height:%d", btemp->get_height());
        Proposal prop_other_clusters(id, btemp, nullptr, cluster_id, cluster_id, btemp->get_height(), 1);


        do_broadcast_proposal_other_clusters(prop_other_clusters);

        last_rvc = int(vote.blk_height);

        pmaker->rotate();


    }

    void HotStuffBase::send_RComplain(ReplicaID last_proposer, const RComplain &vote) {

        if (other_peers_f_plus_one.size()>0)
        {

            int count = 0;

            // Count the number of nodes in the given cluster
            for (const auto& [node_id, node_cluster_id] : cluster_map) {
                if (node_cluster_id == cluster_id) {
                    count++;
                }
            }

            int f = (count) / 3;

            if (id<=f+1)
            {
                HOTSTUFF_LOG_INFO("sending RComplaint to other_peers_f_plus_one with size = %d", int(other_peers_f_plus_one.size()));
                pn.multicast_msg(MsgRComplain(vote), other_peers_f_plus_one);
            }
            else
            {
                HOTSTUFF_LOG_INFO("not sending RComplaint due to not being in first f+1 nodes");
            }

        }


    }







    std::pair<int, std::unordered_set<int>> HotStuffBase::get_leave_set() {

        if (reconfig_count > reconfig_sent)
        {
            HOTSTUFF_LOG_INFO("reconfig_count > reconfig_sent, returning non zero leave set with size: %d", leave_set_int.size());
            reconfig_sent = reconfig_count;

            return {reconfig_count, leave_set_int};
        }
        else
        {
            return {reconfig_count, leave_set_int_empty};
        }
    }



    void HotStuffBase::update_other_f_plus_one(std::unordered_set<int> &leave_set_fun, int cls_id)
    {

        std::unordered_set<PeerId> leave_peer_ids;

        for (int node_id : leave_set_fun) {
            if (node_id >= 0 && node_id < static_cast<int>(all_peers.size())) {
                leave_peer_ids.insert(all_peers[node_id]);
            }
        }


        // Iterate through all clusters except the one with the given cluster_id
        for (const auto& [current_cluster_id, peer_set] : cluster_to_nodes_PeerIds) {

            if (current_cluster_id == cluster_id) {
                continue; // Skip the cluster matching cluster_id
            }

            // Calculate f + 1 where f = number of nodes in cluster / 3
            size_t f;
            if(current_cluster_id==cls_id)
            {
                f = (peer_set.size() - leave_set_fun.size() - 1) / 3;
            }
            else
            {
                f = (peer_set.size() - 1) / 3;
            }


            size_t num_peers = f + 1;

            // Add up to f + 1 PeerIds to the other_peers_f_plus_one vector
            size_t count = 0;

            for (const auto& peer : peer_set) {

                if (count >= num_peers) {
                    break; // Stop when we have added enough peers
                }

                if (current_cluster_id==cls_id)
                {
                    if (leave_peer_ids.find(peer)==leave_peer_ids.end())
                    {
                        HOTSTUFF_LOG_INFO("added peer");
                        other_peers_f_plus_one.push_back(peer);
                        ++count;
                    }
                    else
                    {
                        HOTSTUFF_LOG_INFO("peer ignored");
                    }
                }
                else
                {
                    other_peers_f_plus_one.push_back(peer);
                    ++count;
                }



            }
        }


    }





    void HotStuffBase::do_broadcast_proposal_other_clusters(const Proposal &prop) {


        if (other_peers_f_plus_one_init == 0)
        {

            // Iterate through all clusters except the one with the given cluster_id
            for (const auto& [current_cluster_id, peer_set] : cluster_to_nodes_PeerIds) {
                if (current_cluster_id == cluster_id) {
                    continue; // Skip the cluster matching cluster_id
                }

                // Calculate f + 1 where f = number of nodes in cluster / 3
                size_t f = peer_set.size() / 3;
                size_t num_peers = f + 1;

                // Add up to f + 1 PeerIds to the other_peers_f_plus_one vector
                size_t count = 0;
                for (const auto& peer : peer_set) {
                    if (count >= num_peers) {
                        break; // Stop when we have added enough peers
                    }



                    other_peers_f_plus_one.push_back(peer);
                    ++count;
                }
            }


            other_peers_f_plus_one_init = 1;
        }


//        std::vector<PeerId> other_peers_f_plus_one = std::vector<PeerId>(
//                other_peers.begin(), other_peers.begin() + get_config().nreplicas
//        -get_config().nmajority+1);



        HOTSTUFF_LOG_INFO("Broadcasting to other clusters with");

        pn.multicast_msg(MsgPropose(prop), other_peers_f_plus_one);

    }









    void HotStuffBase::receive_mc_local(Proposal &prop) {

        block_t blk = prop.blk;

//        if (int(prop.other_cluster_block_height)==6000) LOG_INFO("LatencyPlot: Receieved 2nd MC message") ;

        std::unordered_set<int> received_leave_set = prop.current_peer_set;

        int recon_count = prop.reconfig_count;

        if (clusterid_to_reconfig_count[prop.pre_amp_cluster_number]!=recon_count)
        {
            HOTSTUFF_LOG_INFO("clusterid_to_reconfig_count cluster_number %d, recon_count: %d",
                              prop.pre_amp_cluster_number, recon_count);
            clusterid_to_reconfig_count[prop.pre_amp_cluster_number] = recon_count;

            update_other_f_plus_one(received_leave_set, int(prop.pre_amp_cluster_number));
        }


        LOG_INFO("2nd MC message: Reached here proposer %d, cluster number,  = %d, prop.pre_amp_cluster_number = %d, prop.other_cluster_block_height = %d, height = %d",
                 prop.proposer, prop.cluster_number, prop.pre_amp_cluster_number,prop.other_cluster_block_height, blk->get_height());

        auto finished_mc_cids_it = finished_mc_cids.find(int(prop.other_cluster_block_height));

        if (finished_mc_cids_it==finished_mc_cids.end())
        {

            auto it = cid_to_cluster_tracker_array.find(prop.other_cluster_block_height);
            if (it == cid_to_cluster_tracker_array.end())
            {
                cluster_tracker_array = std::vector<int>();
                for(int x;x < n_clusters; x++ ) cluster_tracker_array.push_back(0);
                cluster_tracker_array[cluster_id] = 1;
            }
            else
            {
                cluster_tracker_array = cid_to_cluster_tracker_array[prop.other_cluster_block_height];
            }


            cluster_tracker_array[prop.pre_amp_cluster_number] = cluster_tracker_array[prop.pre_amp_cluster_number] + 1;
            bool test_flag = true;

            for (int biter = 0; biter < n_clusters; biter++)
            {
                if (cluster_tracker_array[biter] == 0)
                {
                    test_flag = false;
                }
            }

            if (test_flag)
            {
                cluster_msg_count = cluster_msg_count + 1;

                LOG_INFO("Adding to finished_mc_cids for height: %d", int(prop.other_cluster_block_height));
                finished_mc_cids.insert(int(prop.other_cluster_block_height));

                if (int(prop.other_cluster_block_height)==6000) LOG_INFO("LatencyPlot: going to execute based on 2nd MC message") ;

                on_receive_other_cluster_(int(prop.other_cluster_block_height));

                for (int biter = 0; biter < n_clusters; biter++)
                {
                    cluster_tracker_array[biter] = 0;
                }
                cluster_tracker_array[cluster_id] = 1;
            }

            cid_to_cluster_tracker_array[prop.other_cluster_block_height] = cluster_tracker_array;


        }

        return;




    }


    void HotStuffBase::start_remote_view_change_timer(int timer_cid, const block_t btemp)
    {

        cid_to_remote_view_change_timer[timer_cid] = TimerEvent(ec, [this, timer_cid, btemp](TimerEvent &)
        {

            HOTSTUFF_LOG_INFO("timer for remote-view-change triggerred with cid:%d", timer_cid);


            HOTSTUFF_LOG_INFO("block_t found for timer_cid: %d with height:%d",
                              timer_cid, btemp->get_height());

            Proposal prop_other_clusters(id, btemp, nullptr, cluster_id, cluster_id, btemp->get_height(), 3);

            bool leader_check = check_leader();


            auto it = finished_mc_cids.find(timer_cid);

            if (it==finished_mc_cids.end() && (timer_cid-last_rvc)>100 )
            {
                {
                    HOTSTUFF_LOG_INFO("starting local remote view change complaint");
                    start_local_complain(btemp->get_height(), btemp->get_hash());

                }
                last_rvc = timer_cid;
            }

        });
        cid_to_remote_view_change_timer[timer_cid].add(rvct_timeout);


    }

    bool HotStuffBase::did_receive_mc(int blk_height)
    {
        auto it = finished_mc_cids.find(blk_height);
        return (it!=finished_mc_cids.end());
    }


    bool HotStuffBase::did_update(int blk_height)
    {
        auto it = finished_update_cids.find(blk_height);
        return (it!=finished_update_cids.end());
    }


    void HotStuffBase::update_finished_update_cids(int blk_height)
    {
        finished_update_cids.insert(blk_height);
    }





    void HotStuffBase::reset_remote_view_change_timer(int timer_blk_height) {
        HOTSTUFF_LOG_INFO("Deleting timer remote-view-change for blk_height = %d", timer_blk_height);

        cid_to_remote_view_change_timer[timer_blk_height].del();
        cid_to_remote_view_change_timer[timer_blk_height].clear();

        auto it = cid_to_remote_view_change_timer.find(timer_blk_height);
        if (it != cid_to_remote_view_change_timer.end())
        {
            cid_to_remote_view_change_timer.erase(it);
        }

    }


    void HotStuffBase::decide_after_mc(int blk_height) {
        HOTSTUFF_LOG_INFO("decide after mc message START for blk height:%d", blk_height);


            const uint256_t blk_hash = storage->find_blk_hash_for_cid(blk_height);

            static const uint8_t negativeOne[] = { 0xFF };

            if (blk_hash!= negativeOne)
            {

                block_t btemp = storage->find_blk(blk_hash);


                if (did_update(blk_height))
                {
                    for (size_t i = 0; i < (btemp->get_cmds()).size(); i++)
                    {
                        HOTSTUFF_LOG_INFO("Since already updated, do_decide for height:%d, btemp = %s, blk_hash = %d", int(btemp->get_height()), std::string(*btemp).c_str(), blk_hash);


                        do_decide(Finality(id, 1, i, btemp->get_height(),  (btemp->get_cmds())[i],   btemp->get_hash()),  (btemp->get_keys())[i], (btemp->get_vals())[i]);


                    }

                    reset_remote_view_change_timer(blk_height);


                }

                if (int(blk_height)==6000) LOG_INFO("LatencyPlot: Finished execution") ;



            }



    }

    void HotStuffBase::store_in_map_for_mc(const block_t &blk)
    {
        HOTSTUFF_LOG_INFO("storing blk in  %s",std::string(*blk).c_str());

        storage->add_cid_blkhash(blk);
    }





    void HotStuffBase::do_vote(ReplicaID last_proposer, const Vote &vote) {
        pmaker->beat_resp(last_proposer)
                .then([this, vote](ReplicaID proposer) {

                    int self_id = get_id();

                    HOTSTUFF_LOG_INFO("Going to vote with proposer: %d, id: %d", int(proposer), self_id);

                    if (proposer == get_id())
                    {
                        //throw HotStuffError("unreachable line");
                        HOTSTUFF_LOG_INFO("self receiving vote");
                        on_receive_vote(vote);
                    }
                    else
                    {
                        HOTSTUFF_LOG_INFO("sending vote to other nodes of same cluster");

                        pn.send_msg(MsgVote(vote), get_config().get_peer_id(proposer));
                    }
                });
    }

    void HotStuffBase::do_consensus(const block_t &blk) {
        pmaker->on_consensus(blk);
    }


    bool HotStuffBase::check_leader() {
        return (get_id()==pmaker->get_proposer());
    }


    void HotStuffBase::do_decide_read_only(Finality &&fin, int key, int value) {




        {

            std::string status = "";
            part_decided++;

            state_machine_execute(fin);

//        try
            {
//            bool cond = key_val_store.find(fin.cmd_hash) != key_val_store.end();
//            if (cond)
//            {
//
//                status =  db_read(key);
//
//                HOTSTUFF_LOG_INFO("do_decide_read_only: key found, status is %s", status);
//            }
//            else
//            {
//                HOTSTUFF_LOG_INFO("do_decide_read_only: key not found", status);
//
//            }
            }

            auto it = decision_waiting.find(fin.cmd_hash);
            if (it != decision_waiting.end())
            {
                it->second(std::move(fin));
                decision_waiting.erase(it);
            }


        }


    }


    void HotStuffBase::send_reconfig_ack(hotstuff::Finality &&fin, recs r) {

        std::string status = "";
        part_decided++;

        state_machine_execute(fin);

        auto it = decision_waiting.find(fin.cmd_hash);
        if (it != decision_waiting.end())
        {
            it->second(std::move(fin));
            decision_waiting.erase(it);
        }




        ReplicaID proposer = pmaker->get_proposer();

        if (id!=proposer)
        {
            HOTSTUFF_LOG_INFO("sending reconfig ack");

            pn.send_msg(MsgRecs(r), get_config().get_peer_id(proposer));
        }
        else
        {
            pending_recs[r]++;

        }


    }




    void HotStuffBase::do_decide(Finality &&fin, int key, int val) {
    HOTSTUFF_LOG_INFO("do_decide() START with part_decided: %d", int(part_decided));

        std::string status = "";


        part_decided++;



        state_machine_execute(fin);




//        try
        {
//            bool cond = key_val_store.find(fin.cmd_hash) != key_val_store.end();

//            if (cond)
            {
//                std::pair key_val = key_val_store.at(fin.cmd_hash);
//                status =  db_write(key_val.first, key_val.second);
                status =  db_write(key, val);
                HOTSTUFF_LOG_INFO("do_decide: write completed, status is %s", status.c_str());


            }
//            else
//            {
//                HOTSTUFF_LOG_INFO("do_decide: write incomplete");
//
//            }

        }

        auto it = decision_waiting.find(fin.cmd_hash);
        if (it != decision_waiting.end())
        {
            it->second(std::move(fin));
            decision_waiting.erase(it);
        }

        HOTSTUFF_LOG_INFO("fin.cmd_height is %d", fin.cmd_height);


    }

    HotStuffBase::~HotStuffBase() {}

    void HotStuffBase::start(
            std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&replicas,
            bool ec_loop) {
        for (size_t i = 0; i < replicas.size(); i++)
        {
            auto &addr = std::get<0>(replicas[i]);
            auto cert_hash = std::move(std::get<2>(replicas[i]));
            valid_tls_certs.insert(cert_hash);
            auto peer = pn.enable_tls ? salticidae::PeerId(cert_hash) : salticidae::PeerId(addr);


            HotStuffCore::add_replica(i, peer, std::move(std::get<1>(replicas[i])));
            if (addr != listen_addr)
            {
                peers.push_back(peer);
                pn.add_peer(peer);
                pn.set_peer_addr(peer, addr);
                pn.conn_peer(peer);

            }
        }

        /* ((n - 1) + 1 - 1) / 3 */
        uint32_t nfaulty = peers.size() / 3;
        if (nfaulty == 0)
            LOG_WARN("too few replicas in the system to tolerate any failure");
        on_init(nfaulty);
        pmaker->init(this);

        if (ec_loop)
            ec.dispatch();
    }







    void HotStuffBase::start_mc(
            std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&replicas,
            std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&all_replicas,
            std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&other_replicas,
            std::unordered_map<int, int> cluster_map_input, int remote_view_change_test,
            int orig_idx, bool ec_loop) {







        LOG_INFO("replicas sizes are %lu, %lu, %lu", replicas.size(), all_replicas.size(), other_replicas.size());

        cluster_map = cluster_map_input;




        for (size_t i = 0; i < replicas.size(); i++)
        {

            auto &addr = std::get<0>(replicas[i]);
            auto cert_hash = std::move(std::get<2>(replicas[i]));
            valid_tls_certs.insert(cert_hash);
            auto peer = pn.enable_tls ? salticidae::PeerId(cert_hash) : salticidae::PeerId(addr);
            HotStuffCore::add_replica(i, peer, std::move(std::get<1>(replicas[i])));

//            if (peer== get_config().get_peer_id(i))
//            {
//                HOTSTUFF_LOG_INFO("peer equal to get peer id for i:%d", i);
//            }

            if (addr != listen_addr)
            {
                peers.push_back(peer);


                if (leave_set.find(peer) == leave_set.end()) {
                    peers_current.push_back(peer);
                }

                pn.add_peer(peer);
                pn.set_peer_addr(peer, addr);
                pn.conn_peer(peer);
            }


        }




        for (size_t i = 0; i < other_replicas.size(); i++)
        {
            auto &addr = std::get<0>(other_replicas[i]);
            auto cert_hash = std::move(std::get<2>(other_replicas[i]));
            valid_tls_certs.insert(cert_hash);
            auto peer = pn.enable_tls ? salticidae::PeerId(cert_hash) : salticidae::PeerId(addr);
//            HotStuffCore::add_replica(i, peer, std::move(std::get<1>(other_replicas[i])));
            if (addr != listen_addr)
            {
                other_peers.push_back(peer);

                pn.add_peer(peer);
                pn.set_peer_addr(peer, addr);
                pn.conn_peer(peer);

            }

        }

        for (size_t i = 0; i < all_replicas.size(); i++)
        {
            auto &addr = std::get<0>(all_replicas[i]);
            auto cert_hash = std::move(std::get<2>(all_replicas[i]));
            auto peer = pn.enable_tls ? salticidae::PeerId(cert_hash) : salticidae::PeerId(addr);
            {
                all_peers.push_back(peer);
            }
        }


        for (size_t node_id = 0; node_id < all_peers.size(); node_id++) {
            int cluster_id = cluster_map[node_id]; // Get the cluster ID for the current node ID
            HOTSTUFF_LOG_INFO("cluster_to_nodes_PeerIds cluster_id %d, node_id %d", cluster_id, node_id);
            cluster_to_nodes_PeerIds[cluster_id].push_back(all_peers[node_id]); // Add the PeerId to the cluster
        }



        HOTSTUFF_LOG_INFO("enabled tls");

        /* ((n - 1) + 1 - 1) / 3 */
        uint32_t nfaulty = peers.size() / 3;

        LOG_INFO("nfaulty, peers.size() are %d, %d \n", int(nfaulty), peers.size());
        if (nfaulty == 0)
            LOG_WARN("too few replicas in the system to tolerate any failure");

        on_init(nfaulty);

        pmaker->init(this);
        if (ec_loop)
            ec.dispatch();



//        if (cluster_id ==0)
//        {
//            leave_set.insert(all_peers[3]);
//            leave_set_int.insert(3);
//
//            peers_current.clear();
//
//            for (const auto& peer : peers) {
//                if (leave_set.find(peer) == leave_set.end()) {
//                    peers_current.push_back(peer);
//                }
//            }
//        }

        cmd_pending.reg_handler(ec, [this](cmd_queue_t &q) {

            std::pair<ClientCommandVars, commit_cb_t> e;


            while (q.try_dequeue(e))
            {
                ReplicaID proposer = pmaker->get_proposer();


                const auto &cmd_hash = e.first.cmd_hash;
                auto it = decision_waiting.find(cmd_hash);






                if (it == decision_waiting.end())
                {
                    HOTSTUFF_LOG_INFO("decision_waiting, cmd_hash absent (this mostly happens)");

                    it = decision_waiting.insert(std::make_pair(cmd_hash, e.second)).first;

                }
                else
                {
                    HOTSTUFF_LOG_INFO("decision_waiting, cmd_hash present");


                    e.second(Finality(id, 0, 0, 0, cmd_hash, uint256_t()) );

                }

//                HOTSTUFF_LOG_INFO("key, val is %d, %d", e.first.key, e.first.val);



                int key = e.first.key;
                int val = e.first.val;
                int cluster_num = e.first.cluster_number;
                int node_number = e.first.node_number;
                int reconfig_number = e.first.reconfig_mode;

                recs reconfig_vars_single{cluster_num, node_number, reconfig_number};

//                HOTSTUFF_LOG_INFO("before db entry");

//                key_val_store[cmd_hash] = std::pair(key,val);
//                HOTSTUFF_LOG_INFO("Added to DB successfully");


                if (reconfig_number >= 0)
                {
                    HOTSTUFF_LOG_INFO("Reconfig Client Message Received");
                    send_reconfig_ack(Finality(id, 1, 0, 0, cmd_hash, uint256_t()), reconfig_vars_single );
                }

                {

                    if ((key%100>15)  )
                    {


                        do_decide_read_only(Finality(id, 1, 0, 0, cmd_hash, uint256_t()), key, val );
                    }

                    if (proposer != get_id()) continue;

                    if (key%100<=15)
                    {
                        cmd_pending_buffer.push(ClientCommandVars{cmd_hash, key, val, cluster_num, node_number});

                    }







                    if (cmd_pending_buffer.size() >= blk_size)
                    {
                        HOTSTUFF_LOG_INFO("Block size worth of pending cmds found");

                        std::vector<uint256_t> cmds;
                        std::vector<int> keys;
                        std::vector<int> vals;

                        for (uint32_t i = 0; i < blk_size; i++)
                        {
                            cmds.push_back((cmd_pending_buffer.front()).cmd_hash);
                            keys.push_back((cmd_pending_buffer.front()).key);
                            vals.push_back((cmd_pending_buffer.front()).val);


                            cmd_pending_buffer.pop();
                        }
                        pmaker->beat().then([this, cmds = std::move(cmds), keys = std::move(keys), vals = std::move(vals)  ](ReplicaID proposer) {
                            if (proposer == get_id())
                            {

                                on_propose(cmds, keys, vals, pmaker->get_parents());

                            }
                        });
                        return true;
                    }




                }





            }
            return false;
        });
    }


}
