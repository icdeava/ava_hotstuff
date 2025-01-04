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
#include <iostream>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <random>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "salticidae/stream.h"
#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"

#include "hotstuff/promise.hpp"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/util.h"
#include "hotstuff/client.h"
#include "hotstuff/hotstuff.h"
#include "hotstuff/liveness.h"

#include "hotstuff/database.h"
#include "hotstuff/in_memory_db.cpp"
#include "hotstuff/helper.h"

using salticidae::MsgNetwork;
using salticidae::ClientNetwork;
using salticidae::ElapsedTime;
using salticidae::Config;
using salticidae::get_hex10;

using salticidae::_1;
using salticidae::_2;
using salticidae::static_pointer_cast;
using salticidae::trim_all;
using salticidae::split;

using hotstuff::TimerEvent;
using hotstuff::EventContext;
using hotstuff::NetAddr;
using hotstuff::HotStuffError;
using hotstuff::CommandDummy;
using hotstuff::Finality;
using hotstuff::command_t;
using hotstuff::uint256_t;
using hotstuff::opcode_t;
using hotstuff::bytearray_t;
using hotstuff::DataStream;
using hotstuff::ReplicaID;
using hotstuff::MsgReqCmd;
using hotstuff::MsgRespCmd;
using hotstuff::get_hash;
using hotstuff::promise_t;


using HotStuff = hotstuff::HotStuffSecp256k1;

class HotStuffApp: public HotStuff {
    double stat_period;
    double impeach_timeout;
    int one_time_impeach = 0;
    EventContext ec;
    EventContext req_ec;
    EventContext resp_ec;
    /** Network messaging between a replica and its client. */
    ClientNetwork<opcode_t> cn;
    /** Timer object to schedule a periodic printing of system statistics */
    TimerEvent ev_stat_timer;
    /** Timer object to monitor the progress for simple impeachment */
    TimerEvent impeach_timer;
    /** The listen address for client RPC */
    NetAddr clisten_addr;


    std::unordered_map<const uint256_t, promise_t> unconfirmed;

    using conn_t = ClientNetwork<opcode_t>::conn_t;
    using resp_queue_t = salticidae::MPSCQueueEventDriven<std::pair<Finality, NetAddr>>;

    /* for the dedicated thread sending responses to the clients */
    std::thread req_thread;
    std::thread resp_thread;
    resp_queue_t resp_queue;
    salticidae::BoxObj<salticidae::ThreadCall> resp_tcall;
    salticidae::BoxObj<salticidae::ThreadCall> req_tcall;




    DataBase *db = new InMemoryDB();

    void client_request_cmd_handler(MsgReqCmd &&, const conn_t &);

    static command_t parse_cmd(DataStream &s) {
        auto cmd = new CommandDummy();
        s >> *cmd;
        return cmd;
    }

    void reset_imp_timer() {
        impeach_timer.del();
        impeach_timer.add(impeach_timeout);
    }




    std::string db_read(int key) override {
        std::string status = "";
        HOTSTUFF_LOG_INFO("db_read: key is %d", key);

        db->Get(std::to_string(key));
        status =  "READ";


        return status;
    }


    std::string db_write(int key, int val) override {
        std::string status = "";
//        HOTSTUFF_LOG_INFO("db_write: key, val is %d, %d", key, val);

        db->Put(std::to_string(key), std::to_string(2));
//        db->Get(std::to_string(key));

        status =  "UPDATE";


        return status;
    }




    void state_machine_execute(const Finality &fin) override {



        {






            std::string status = "";

//            HOTSTUFF_LOG_INFO("state_machine_execute: status = %s ", status.c_str());

//            if (key_val.first%2==0)
            {
//                db->Put(std::to_string(key_val.first), std::to_string(key_val.second));
//                status = "UPDATE";
            }
//            else
            {
//                status =  "READ: value = " + db->Get(std::to_string(key_val.first));

            }


            reset_imp_timer();


            #ifndef HOTSTUFF_ENABLE_BENCHMARK

            HOTSTUFF_LOG_INFO("replicated %s",
                              std::string(fin).c_str());







            #endif



        }




    }





#ifdef HOTSTUFF_MSG_STAT
    std::unordered_set<conn_t> client_conns;
    void print_stat() const;
#endif

    public:
    HotStuffApp(uint32_t blk_size,
                double stat_period,
                double impeach_timeout,
                ReplicaID idx,
                int cluster_id,
                int n_clusters,
                const bytearray_t &raw_privkey,
                NetAddr plisten_addr,
                NetAddr clisten_addr,
                hotstuff::pacemaker_bt pmaker,
                const EventContext &ec,
                size_t nworker,
                const Net::Config &repnet_config,
                const ClientNetwork<opcode_t>::Config &clinet_config);

    void start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &reps);
    void start_mc(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &reps,
               const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &all_reps,
               const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &other_reps,
                  std::unordered_map<int, int> cluster_map_input, int remote_view_change_test, int orig_idx);


    void stop();
};

std::pair<std::string, std::string> split_ip_port_cport(const std::string &s) {
    auto ret = trim_all(split(s, ";"));
    if (ret.size() != 2)
        throw std::invalid_argument("invalid cport format");
    return std::make_pair(ret[0], ret[1]);
}

salticidae::BoxObj<HotStuffApp> papp = nullptr;




int main(int argc, char **argv) {

    Config config("hotstuff.gen.conf");

    ElapsedTime elapsed;
    elapsed.start();

    auto opt_blk_size = Config::OptValInt::create(1);
    auto opt_parent_limit = Config::OptValInt::create(-1);
    auto opt_stat_period = Config::OptValDouble::create(1);
    auto opt_replicas = Config::OptValStrVec::create();
    auto opt_idx = Config::OptValInt::create(0);
    auto opt_client_port = Config::OptValInt::create(-1);
    auto opt_privkey = Config::OptValStr::create();
    auto opt_tls_privkey = Config::OptValStr::create();
    auto opt_tls_cert = Config::OptValStr::create();
    auto opt_help = Config::OptValFlag::create(false);
    auto opt_pace_maker = Config::OptValStr::create("rr");
//    auto opt_pace_maker = Config::OptValStr::create("dummy");

    auto opt_fixed_proposer = Config::OptValInt::create(1);
    auto opt_base_timeout = Config::OptValDouble::create(25);
    auto opt_prop_delay = Config::OptValDouble::create(1);
    auto opt_imp_timeout = Config::OptValDouble::create(25);
    auto opt_nworker = Config::OptValInt::create(1);
    auto opt_repnworker = Config::OptValInt::create(1);
    auto opt_repburst = Config::OptValInt::create(100);
    auto opt_clinworker = Config::OptValInt::create(8);
    auto opt_cliburst = Config::OptValInt::create(1000);
    auto opt_notls = Config::OptValFlag::create(false);
    auto opt_max_rep_msg = Config::OptValInt::create(4 << 20); // 4M by default
    auto opt_max_cli_msg = Config::OptValInt::create(65536); // 64K by default


    auto opt_remote_view_change_test = Config::OptValInt::create(-6000);



    config.add_opt("block-size", opt_blk_size, Config::SET_VAL);
    config.add_opt("parent-limit", opt_parent_limit, Config::SET_VAL);
    config.add_opt("stat-period", opt_stat_period, Config::SET_VAL);
    config.add_opt("replica", opt_replicas, Config::APPEND, 'a', "add an replica to the list");
    config.add_opt("idx", opt_idx, Config::SET_VAL, 'i', "specify the index in the replica list");
    config.add_opt("cport", opt_client_port, Config::SET_VAL, 'c', "specify the port listening for clients");
    config.add_opt("privkey", opt_privkey, Config::SET_VAL);
    config.add_opt("tls-privkey", opt_tls_privkey, Config::SET_VAL);
    config.add_opt("tls-cert", opt_tls_cert, Config::SET_VAL);
    config.add_opt("pace-maker", opt_pace_maker, Config::SET_VAL, 'p', "specify pace maker (dummy, rr)");
    config.add_opt("proposer", opt_fixed_proposer, Config::SET_VAL, 'l', "set the fixed proposer (for dummy)");
    config.add_opt("base-timeout", opt_base_timeout, Config::SET_VAL, 't', "set the initial timeout for the Round-Robin Pacemaker");
    config.add_opt("prop-delay", opt_prop_delay, Config::SET_VAL, 't', "set the delay that follows the timeout for the Round-Robin Pacemaker");
    config.add_opt("imp-timeout", opt_imp_timeout, Config::SET_VAL, 'u', "set impeachment timeout (for sticky)");
    config.add_opt("nworker", opt_nworker, Config::SET_VAL, 'n', "the number of threads for verification");
    config.add_opt("repnworker", opt_repnworker, Config::SET_VAL, 'm', "the number of threads for replica network");
    config.add_opt("repburst", opt_repburst, Config::SET_VAL, 'b', "");
    config.add_opt("clinworker", opt_clinworker, Config::SET_VAL, 'M', "the number of threads for client network");
    config.add_opt("cliburst", opt_cliburst, Config::SET_VAL, 'B', "");
    config.add_opt("notls", opt_notls, Config::SWITCH_ON, 's', "disable TLS");
    config.add_opt("max-rep-msg", opt_max_rep_msg, Config::SET_VAL, 'S', "the maximum replica message size");
    config.add_opt("max-cli-msg", opt_max_cli_msg, Config::SET_VAL, 'S', "the maximum client message size");
    config.add_opt("help", opt_help, Config::SWITCH_ON, 'h', "show this help info");
    config.add_opt("remote_view_change_test", opt_remote_view_change_test, Config::SET_VAL);



    EventContext ec;

    config.parse(argc, argv);
    if (opt_help->get())
    {
        config.print_help();
        exit(0);
    }


    auto remote_view_change_test = opt_remote_view_change_test->get();



    auto idx = opt_idx->get();
    auto client_port = opt_client_port->get();
    std::vector<std::tuple<std::string, std::string, std::string>> replicas;

    std::vector<std::tuple<std::string, std::string, std::string>> other_replicas;
    std::vector<std::tuple<std::string, std::string, std::string>> all_replicas;


    std::unordered_map<int, int> cluster_map;


    const std::string filePath = "cluster_info_hs.txt"; // Change this to the path of your file

    // Open the file
    std::ifstream inputFile(filePath);


    if (!inputFile.is_open()) {
        // File does not exist, throw an exception


        namespace fs = std::filesystem;
        std::cout << "Current working directory: " << fs::current_path() << std::endl;


        throw HotStuffError("cluster_info_hs.txt missing");
    }


    // Vector to store the numbers
    std::vector<int> numbers;

    // Read numbers from the file
    int temp_cluster_count = 0;
    int number;
    while (inputFile >> number) {

        numbers.push_back(number);
        HOTSTUFF_LOG_INFO("temp_cluster_count, number is %d, %d", temp_cluster_count, number);

        cluster_map[temp_cluster_count] = number;
        temp_cluster_count++;
    }

    // Close the file
    inputFile.close();



    int n_clusters = 0;

    for (auto it : cluster_map)
    {
        std::cout << " " << it.first << ":" << it.second << std::endl;
        if (it.second > n_clusters)
        {
            n_clusters++;
        }
    }
    n_clusters++;




//    HOTSTUFF_LOG_INFO("idx is %d\n", idx);
    int count_rep =0;
    int count_rep_cluster= 0;

    int cluster_idx = 0;
    bool flag_intraidx = false;

    for (const auto &s: opt_replicas->get())
    {

        HOTSTUFF_LOG_INFO("opt_replicas in loop\n");


        if (cluster_map[int(idx)] == cluster_map[int(count_rep)])
        {
            flag_intraidx = true;
            auto res = trim_all(split(s, ","));
            if (res.size() != 3)
                throw HotStuffError("invalid replica info");

            HOTSTUFF_LOG_INFO("res[0], res[1], res[2] are: \n %s, \n %s, \n %s\n", std::string(res[0]).c_str(), std::string(res[1]).c_str(), std::string(res[2]).c_str());
            replicas.push_back(std::make_tuple(res[0], res[1], res[2]));
            all_replicas.push_back(std::make_tuple(res[0], res[1], res[2]));


        }
        else
        {
            auto res = trim_all(split(s, ","));
            if (res.size() != 3)
                throw HotStuffError("invalid replica info");
            other_replicas.push_back(std::make_tuple(res[0], res[1], res[2]));
            all_replicas.push_back(std::make_tuple(res[0], res[1], res[2]));

        }


        if (flag_intraidx)
        {
            if (int(idx)==count_rep) cluster_idx = int(count_rep_cluster);
            count_rep_cluster++;
        }

        count_rep++;
    }

    HOTSTUFF_LOG_INFO("replicas, all_replicas sizes are %d, %d\n", replicas.size(), all_replicas.size());
    HOTSTUFF_LOG_INFO("idx, cluster_idx is %d, %d\n", idx, cluster_idx);

    if (!(0 <= idx && (size_t)idx < all_replicas.size()))
        throw HotStuffError("replica idx out of range");



    std::string binding_addr = std::get<0>(all_replicas[idx]);
    if (client_port == -1)
    {
        auto p = split_ip_port_cport(binding_addr);
        size_t idx;
        try {
            client_port = stoi(p.second, &idx);
        } catch (std::invalid_argument &) {
            throw HotStuffError("client port not specified");
        }
    }

    HOTSTUFF_LOG_INFO("dsadasd 1\n");

    NetAddr plisten_addr{split_ip_port_cport(binding_addr).first};

    auto parent_limit = opt_parent_limit->get();
    hotstuff::pacemaker_bt pmaker;



    if (opt_pace_maker->get() == "dummy")
    {
        HOTSTUFF_LOG_INFO("Default dummy pacemaker with clinworker= %d", opt_clinworker->get());
        pmaker = new hotstuff::PaceMakerDummyFixed(opt_fixed_proposer->get(), parent_limit);

//        HOTSTUFF_LOG_INFO("Round robin pacemaker, with %d and %d", int(opt_base_timeout->get()), int(opt_prop_delay->get()));
//        pmaker = new hotstuff::PaceMakerRR(ec, parent_limit, opt_base_timeout->get(), opt_prop_delay->get());

    }
    else
    {
        HOTSTUFF_LOG_INFO("Round robin pacemaker, with %d and %d", int(opt_base_timeout->get()), int(opt_prop_delay->get()));
        pmaker = new hotstuff::PaceMakerRR(ec, parent_limit, opt_base_timeout->get(), opt_prop_delay->get());

    }

    HOTSTUFF_LOG_INFO("dsadasd 3\n");

    HotStuffApp::Net::Config repnet_config;
    ClientNetwork<opcode_t>::Config clinet_config;
    repnet_config.max_msg_size(opt_max_rep_msg->get());
    clinet_config.max_msg_size(opt_max_cli_msg->get());
    if (!opt_tls_privkey->get().empty() && !opt_notls->get())
    {
        auto tls_priv_key = new salticidae::PKey(
                salticidae::PKey::create_privkey_from_der(
                    hotstuff::from_hex(opt_tls_privkey->get())));
        auto tls_cert = new salticidae::X509(
                salticidae::X509::create_from_der(
                    hotstuff::from_hex(opt_tls_cert->get())));
        repnet_config
            .enable_tls(true)
            .tls_key(tls_priv_key)
            .tls_cert(tls_cert);
    }
    repnet_config
        .burst_size(opt_repburst->get())
        .nworker(opt_repnworker->get());
    clinet_config
        .burst_size(opt_cliburst->get())
        .nworker(opt_clinworker->get());

    HOTSTUFF_LOG_INFO("dsadasd 3.5\n");

    papp = new HotStuffApp(opt_blk_size->get(),
                        opt_stat_period->get(),
                        opt_imp_timeout->get(),
                        cluster_idx,
                        cluster_map[int(idx)],
                        n_clusters,
                        hotstuff::from_hex(opt_privkey->get()),
                        plisten_addr,
                        NetAddr("0.0.0.0", client_port),
                        std::move(pmaker),
                        ec,
                        opt_nworker->get(),
                        repnet_config,
                        clinet_config);
    std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> reps;
    std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> all_reps;
    std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> other_reps;


    for (auto &r: replicas)
    {
        auto p = split_ip_port_cport(std::get<0>(r));
        reps.push_back(std::make_tuple(
            NetAddr(p.first),
            hotstuff::from_hex(std::get<1>(r)),
            hotstuff::from_hex(std::get<2>(r))));
    }


    for (auto &r: all_replicas)
    {
        auto p = split_ip_port_cport(std::get<0>(r));
        all_reps.push_back(std::make_tuple(
                NetAddr(p.first),
                hotstuff::from_hex(std::get<1>(r)),
                hotstuff::from_hex(std::get<2>(r))));
    }

    for (auto &r: other_replicas)
    {
        auto p = split_ip_port_cport(std::get<0>(r));
        other_reps.push_back(std::make_tuple(
                NetAddr(p.first),
                hotstuff::from_hex(std::get<1>(r)),
                hotstuff::from_hex(std::get<2>(r))));
    }

    auto shutdown = [&](int) { papp->stop(); };
    salticidae::SigEvent ev_sigint(ec, shutdown);
    salticidae::SigEvent ev_sigterm(ec, shutdown);
    ev_sigint.add(SIGINT);
    ev_sigterm.add(SIGTERM);

//    papp->start(reps);
    papp->start_mc(reps, all_reps, other_reps, cluster_map, remote_view_change_test, idx);

    elapsed.stop(true);
    return 0;
}

HotStuffApp::HotStuffApp(uint32_t blk_size,
                        double stat_period,
                        double impeach_timeout,
                        ReplicaID idx,
                        int cluster_id,
                        int n_clusters,
                        const bytearray_t &raw_privkey,
                        NetAddr plisten_addr,
                        NetAddr clisten_addr,
                        hotstuff::pacemaker_bt pmaker,
                        const EventContext &ec,
                        size_t nworker,
                        const Net::Config &repnet_config,
                        const ClientNetwork<opcode_t>::Config &clinet_config):
    HotStuff(blk_size, idx, cluster_id, n_clusters, raw_privkey,
            plisten_addr, std::move(pmaker), ec, nworker, repnet_config),
    stat_period(stat_period),
    impeach_timeout(impeach_timeout),
    ec(ec),
    cn(req_ec, clinet_config),
    clisten_addr(clisten_addr) {
    /* prepare the thread used for sending back confirmations */
    resp_tcall = new salticidae::ThreadCall(resp_ec);
    req_tcall = new salticidae::ThreadCall(req_ec);





    resp_queue.reg_handler(resp_ec, [this](resp_queue_t &q) {
        std::pair<Finality, NetAddr> p;
        while (q.try_dequeue(p))
        {
            try {

                HOTSTUFF_LOG_INFO("responding to cmd_height:%d, cmd_idx:%d, with Netaddr = %s",
                                  int(p.first.cmd_height), int(p.first.cmd_idx), std::string(p.second).c_str());
                cn.send_msg(MsgRespCmd(std::move(p.first)), p.second);


            } catch (std::exception &err) {
                HOTSTUFF_LOG_WARN("unable to send to the client: %s", err.what());
            }
        }
        return false;
    });

    /* register the handlers for msg from clients */
    cn.reg_handler(salticidae::generic_bind(&HotStuffApp::client_request_cmd_handler, this, _1, _2));
    cn.start();
    cn.listen(clisten_addr);
}





void HotStuffApp::client_request_cmd_handler(MsgReqCmd &&msg, const conn_t &conn) {
    const NetAddr addr = conn->get_addr();
    auto cmd = parse_cmd(msg.serialized);

    const auto &cmd_hash = cmd->get_hash();
    HOTSTUFF_LOG_INFO("processing cmd %s, with cid: %d, key: %d, value: %d",
                      std::string(*cmd).c_str(), int(cmd->get_cid()), int(cmd->get_key()), int(cmd->get_val()) );




    exec_command(hotstuff::ClientCommandVars{cmd->get_hash(), cmd->get_key(),
                                   cmd->get_val(), cmd->get_cluster_num(), cmd->get_node_num(),
                                   cmd->get_reconfig_mode()}, [this, addr](Finality fin) {
        resp_queue.enqueue(std::make_pair(fin, addr));


    });
}

void HotStuffApp::start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &reps) {
    ev_stat_timer = TimerEvent(ec, [this](TimerEvent &) {
        HotStuff::print_stat();
        HotStuffApp::print_stat();
        //HotStuffCore::prune(100);
        ev_stat_timer.add(stat_period);
    });
    ev_stat_timer.add(stat_period);
    impeach_timer = TimerEvent(ec, [this](TimerEvent &) {
        if (get_decision_waiting().size())
            get_pace_maker()->impeach();
        reset_imp_timer();
    });
    impeach_timer.add(impeach_timeout);
    HOTSTUFF_LOG_INFO("** starting the system with parameters **");
    HOTSTUFF_LOG_INFO("blk_size = %lu", blk_size);
    HOTSTUFF_LOG_INFO("conns = %lu", HotStuff::size());
    HOTSTUFF_LOG_INFO("** starting the event loop...");
    HotStuff::start(reps);
    cn.reg_conn_handler([this](const salticidae::ConnPool::conn_t &_conn, bool connected) {
        auto conn = salticidae::static_pointer_cast<conn_t::type>(_conn);
        if (connected)
            client_conns.insert(conn);
        else
            client_conns.erase(conn);
        return true;
    });
    req_thread = std::thread([this]() { req_ec.dispatch(); });
    resp_thread = std::thread([this]() { resp_ec.dispatch(); });
    /* enter the event main loop */
    ec.dispatch();
}



void HotStuffApp::start_mc(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &reps,
                        const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &all_reps,
                        const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &other_reps,
                           std::unordered_map<int, int> cluster_map_input, int remote_view_change_test, int orig_idx) {
    ev_stat_timer = TimerEvent(ec, [this](TimerEvent &) {
        HotStuff::print_stat();
        HotStuffApp::print_stat();
        //HotStuffCore::prune(100);
        ev_stat_timer.add(stat_period);
    });
    ev_stat_timer.add(stat_period);
    impeach_timer = TimerEvent(ec, [this](TimerEvent &) {
        if (get_decision_waiting().size())
        {
            get_pace_maker()->impeach();
        }
        reset_imp_timer();
    });
    impeach_timer.add(impeach_timeout);
    HOTSTUFF_LOG_INFO("** starting the system with parameters **");
    HOTSTUFF_LOG_INFO("blk_size = %lu", blk_size);
    HOTSTUFF_LOG_INFO("conns = %lu", HotStuff::size());
    HOTSTUFF_LOG_INFO("** starting the event loop...");


    db->Open("db-test");


//    for (int i=0;i < 20000; i++)
//    {
//        db->Put(std::to_string(i), std::to_string(0));
//    }


    printf("DB testing\nInsert key K1 with value V1\n");
    db->Put("K1", "V1");
    printf("DB K1, V1 inserted\n");


    HotStuff::start_mc(reps, all_reps, other_reps, cluster_map_input,remote_view_change_test, orig_idx);


    cn.reg_conn_handler([this](const salticidae::ConnPool::conn_t &_conn, bool connected) {
        auto conn = salticidae::static_pointer_cast<conn_t::type>(_conn);
        if (connected)
            client_conns.insert(conn);
        else
            client_conns.erase(conn);
        return true;
    });
    req_thread = std::thread([this]() { req_ec.dispatch(); });
    resp_thread = std::thread([this]() { resp_ec.dispatch(); });
    /* enter the event main loop */
    ec.dispatch();
}











void HotStuffApp::stop() {
    papp->req_tcall->async_call([this](salticidae::ThreadCall::Handle &) {
        req_ec.stop();
    });
    papp->resp_tcall->async_call([this](salticidae::ThreadCall::Handle &) {
        resp_ec.stop();
    });

    req_thread.join();
    resp_thread.join();
    ec.stop();
}

void HotStuffApp::print_stat() const {
#ifdef HOTSTUFF_MSG_STAT
    HOTSTUFF_LOG_INFO("--- client msg. (10s) ---");
    size_t _nsent = 0;
    size_t _nrecv = 0;
    for (const auto &conn: client_conns)
    {
        if (conn == nullptr) continue;
        size_t ns = conn->get_nsent();
        size_t nr = conn->get_nrecv();
        size_t nsb = conn->get_nsentb();
        size_t nrb = conn->get_nrecvb();
        conn->clear_msgstat();
        HOTSTUFF_LOG_INFO("%s: %u(%u), %u(%u)",
            std::string(conn->get_addr()).c_str(), ns, nsb, nr, nrb);
        _nsent += ns;
        _nrecv += nr;
    }
    HOTSTUFF_LOG_INFO("--- end client msg. ---");
#endif
}
