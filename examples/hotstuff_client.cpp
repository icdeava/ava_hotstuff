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
#include <random>
#include <memory>
#include <signal.h>
#include <sys/time.h>
#include <fstream>

#include "hotstuff/database.h"
#include "hotstuff/in_memory_db.cpp"
#include "hotstuff/helper.h"

#include "hotstuff/hotstuff.h"


#include "salticidae/type.h"
#include "salticidae/netaddr.h"
#include "salticidae/network.h"
#include "salticidae/util.h"
#include "salticidae/event.h"

#include "salticidae/msg.h"

#include "hotstuff/util.h"
#include "hotstuff/type.h"
#include "hotstuff/client.h"
#include "salticidae/conn.h"

#define CPU_FREQ 2.2

using salticidae::Config;
using salticidae::PeerId;

using hotstuff::ReplicaID;
using hotstuff::NetAddr;
using hotstuff::EventContext;
using hotstuff::MsgReqCmd;
using hotstuff::MsgRespCmd;

using hotstuff::CommandDummy;
using hotstuff::HotStuffError;
using hotstuff::uint256_t;
using hotstuff::opcode_t;
using hotstuff::command_t;

EventContext ec;
ReplicaID proposer;
size_t max_async_num;
int max_iter_num;
int join_client;
uint32_t cid;
uint32_t cnt = 0;
uint32_t nfaulty;

bool to_join = true;
bool to_join2 = true;
bool to_join3 = true;



uint32_t reconfig_client = 0;
uint32_t reconfig_done = 0;


uint32_t n_clusters;


uint32_t table_size =  20000;
double denom = 0;
double g_zipf_theta = 0.5;
double zeta_2_theta;
uint64_t the_n;




myrand *mrand;



struct Request {
    command_t cmd;
    size_t confirmed;
    salticidae::ElapsedTime et;
    Request(const command_t &cmd): cmd(cmd), confirmed(0) { et.start(); }
};





using Net = salticidae::MsgNetwork<opcode_t>;


void client_resp_cmd_handler(MsgRespCmd &&msg, const Net::conn_t &);


std::unordered_map<ReplicaID, Net::conn_t> conns;
std::unordered_map<ReplicaID, Net::conn_t> my_conns;
std::unordered_map<ReplicaID, Net::conn_t> other_conns;
std::unordered_map<int, int> cluster_map;

std::unordered_map<const uint256_t, Request> waiting;
std::vector<NetAddr> replicas;

std::vector<std::string> replicas_certs;


std::vector<std::pair<struct timeval, double>> elapsed;
std::unique_ptr<Net> mn;


std::vector<PeerId> reconfig_peers_client;




uint64_t zipf(uint64_t n, double theta)
{
    assert(this->the_n == n);
    assert(theta == g_zipf_theta);
    double alpha = 1 / (1 - theta);
    double zetan = denom;
    double eta = (1 - pow(2.0 / n, 1 - theta)) /
                 (1 - zeta_2_theta / zetan);
    //	double eta = (1 - pow(2.0 / n, 1 - theta)) /
    //		(1 - zeta_2_theta / zetan);
    double u = (double)(mrand->next() % 10000000) / 10000000;
    double uz = u * zetan;
    if (uz < 1)
        return 1;
    if (uz < 1 + pow(0.5, theta))
        return 2;
    return 1 + (uint64_t)(n * pow(eta * u - eta + 1, alpha));
}


uint64_t get_server_clock()
{
#if defined(__i386__)
    uint64_t ret;
	__asm__ __volatile__("rdtsc"
						 : "=A"(ret));
#elif defined(__x86_64__)
    unsigned hi, lo;
	__asm__ __volatile__("rdtsc"
						 : "=a"(lo), "=d"(hi));
	uint64_t ret = ((uint64_t)lo) | (((uint64_t)hi) << 32);
	ret = (uint64_t)((double)ret / CPU_FREQ);
#else
    timespec *tp = new timespec;
    clock_gettime(CLOCK_REALTIME, tp);
    uint64_t ret = tp->tv_sec * 1000000000 + tp->tv_nsec;
    delete tp;
#endif
    return ret;
}






void connect_all()
{




    const std::string filePath = "cluster_info_hs.txt"; // Change this to the path of your file

    // Open the file
    std::ifstream inputFile(filePath);



    if (!inputFile.is_open()) {
        // File does not exist, throw an exception
        throw HotStuffError("cluster_info_hs.txt missing");
    }

    // Vector to store the numbers
    std::vector<int> numbers;

    // Read numbers from the file
    int temp_cluster_count = 0;
    int number;
    while (inputFile >> number) {

        numbers.push_back(number);
        cluster_map[temp_cluster_count] = number;
        temp_cluster_count++;
    }

    // Close the file
    inputFile.close();



    HOTSTUFF_LOG_INFO("cluster_map[0], cluster_map[4] is "
                      "%d, %d", cluster_map[0], cluster_map[4]);


    mrand = (myrand *) new myrand();

    mrand->init(get_server_clock());



    zeta_2_theta = zeta(2, g_zipf_theta);
    the_n = table_size - 1;
    denom = zeta(the_n, g_zipf_theta);






    n_clusters = 0;
    for (auto it : cluster_map)
    {
        if (it.second > n_clusters)
        {
            n_clusters++;
        }
    }
    n_clusters++;

    nfaulty = 0;
    for (size_t i = 0; i < replicas.size(); i++)
    {
        if (cluster_map[int(i)]==int(cid % n_clusters))
        {
            HOTSTUFF_LOG_INFO("connecting to replica %d", i);
            nfaulty++;
            conns.insert(std::make_pair(i, mn->connect_sync(replicas[i])));
        }
    }


    HOTSTUFF_LOG_INFO("replicas.size() is %d", int(replicas.size()));



    nfaulty = (nfaulty - 1)/3;
    HOTSTUFF_LOG_INFO("CHECK Set nfaulty = %zu, replicas.size() = %d", nfaulty, replicas.size());

}

bool try_send(bool check = true) {
    if ((!check || waiting.size() < max_async_num) && max_iter_num)
    {
//        HOTSTUFF_LOG_INFO("cid is %d",int(cid));

//        auto cmd = new CommandDummy(cid, cnt++, 127,0,0,1);


        int g_zipf_theta = 0.5;
        int temp_key = zipf(table_size - 1, g_zipf_theta);

        mrand->next();
        int temp_value = 2;

//        HOTSTUFF_LOG_INFO("temp_key, temp_value are : %d, %d", temp_key, temp_value);


        if (reconfig_client==0)
        {
            auto cmd = new CommandDummy(cid, cnt++, temp_key, temp_value);


            MsgReqCmd msg(*cmd);
            for (auto &p: conns)
            {
                mn->send_msg(msg, p.second);
            }

            waiting.insert(std::make_pair(
                    cmd->get_hash(), Request(cmd)));

            if (max_iter_num > 0)
            {
                max_iter_num--;
            }

            return true;

        }

        else
        {
            if (reconfig_done==0)
            {
                auto cmd = new CommandDummy(cid, cnt++, temp_key, temp_value, 0, 0, 1);
                reconfig_done = 1;

                MsgReqCmd msg(*cmd);
                for (auto &p: conns)
                {
                    mn->send_msg(msg, p.second);
                }

                waiting.insert(std::make_pair(
                        cmd->get_hash(), Request(cmd)));

                if (max_iter_num > 0)
                {
                    max_iter_num--;
                }

                return false;

            }
        }
    }
    return false;
}

void client_resp_cmd_handler(MsgRespCmd &&msg, const Net::conn_t &) {
    auto &fin = msg.fin;
    const uint256_t &cmd_hash = fin.cmd_hash;



    //commented reconfig code
    int temp_cmd_height = fin.cmd_height;




    bool to_print = true;


    auto it = waiting.find(cmd_hash);
    auto &et = it->second.et;



    if (it == waiting.end()) return;
    et.stop();


    if (reconfig_client ==0 )
    {
        if (++it->second.confirmed <= nfaulty) return; // wait for f + 1 ack

    }
    else
    {
        if (++it->second.confirmed <= 2*nfaulty) return; // wait for 2f + 1 ack
    }



//#ifndef HOTSTUFF_ENABLE_BENCHMARK


    HOTSTUFF_LOG_INFO("%.6f ",
                      std::string(fin).c_str(),
                      et.elapsed_sec);
//#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);




    // commented reconfig print block code
//    if (to_print)
    {
        elapsed.push_back(std::make_pair(tv, et.elapsed_sec));

    }


//#endif
//    HOTSTUFF_LOG_INFO("got response for cid:%d", int(it->second.cmd->get_cid()));

    waiting.erase(it);
    while (try_send());
}

std::pair<std::string, std::string> split_ip_port_cport(const std::string &s) {
    auto ret = salticidae::trim_all(salticidae::split(s, ";"));
    return std::make_pair(ret[0], ret[1]);
}

int main(int argc, char **argv) {
    Config config("hotstuff.gen.conf");

    auto opt_idx = Config::OptValInt::create(0);
    auto opt_reconfig_client = Config::OptValInt::create(0);

    auto opt_replicas = Config::OptValStrVec::create();
    auto opt_max_iter_num = Config::OptValInt::create(100);
    auto opt_max_async_num = Config::OptValInt::create(10);
    auto opt_cid = Config::OptValInt::create(-1);
    auto opt_max_cli_msg = Config::OptValInt::create(65536); // 64K by default

    auto shutdown = [&](int) { ec.stop(); };
    salticidae::SigEvent ev_sigint(ec, shutdown);
    salticidae::SigEvent ev_sigterm(ec, shutdown);
    ev_sigint.add(SIGINT);
    ev_sigterm.add(SIGTERM);






    mn = std::make_unique<Net>(ec, Net::Config().max_msg_size(opt_max_cli_msg->get()));
    mn->reg_handler(client_resp_cmd_handler);
    mn->start();

    config.add_opt("idx", opt_idx, Config::SET_VAL);
    config.add_opt("reconfig_client", opt_reconfig_client, Config::SET_VAL);
    config.add_opt("cid", opt_cid, Config::SET_VAL);
    config.add_opt("replica", opt_replicas, Config::APPEND);
    config.add_opt("iter", opt_max_iter_num, Config::SET_VAL);
    config.add_opt("max-async", opt_max_async_num, Config::SET_VAL);
    config.add_opt("max-cli-msg", opt_max_cli_msg, Config::SET_VAL, 'S', "the maximum client message size");
    config.parse(argc, argv);
    auto idx = opt_idx->get();
    max_iter_num = opt_max_iter_num->get();
    reconfig_client = opt_reconfig_client->get();

    max_async_num = opt_max_async_num->get();
    std::vector<std::string> raw;
    std::vector<std::string> raw_cert;

    int countt = 0;
    for (const auto &s: opt_replicas->get())
    {
        HOTSTUFF_LOG_INFO("counting replicas: %d \n", countt);
        auto res = salticidae::trim_all(salticidae::split(s, ","));
        if (res.size() < 1)
            throw HotStuffError("format error");

//        if (cluster_map[countt]==int(cid%2))
        {
            HOTSTUFF_LOG_INFO("pushed countt %d to raw with cluster_map[countt] = %d and int(cid percent 2) = %d",
                              countt,cluster_map[countt], int(cid%2));
            raw.push_back(res[0]);

            raw_cert.push_back(res[1]);
        }

        countt++;
    }

    if (!(0 <= idx && (size_t)idx < raw.size() && raw.size() > 0))
        throw std::invalid_argument("out of range");
    cid = opt_cid->get() != -1 ? opt_cid->get() : idx;


//    HOTSTUFF_LOG_INFO("-> cid = %d", int(nfaulty));
    HOTSTUFF_LOG_INFO("-> cid, idx = %d, %d", int(cid), int((size_t)idx));
    for (const auto &p: raw)
    {
        auto _p = split_ip_port_cport(p);
        size_t _;
        replicas.push_back(NetAddr(NetAddr(_p.first).ip, htons(stoi(_p.second, &_))));
    }


    for (const auto& p : raw_cert) {
        replicas_certs.push_back(p);
    }


    HOTSTUFF_LOG_INFO("CHECK replicas size = %d, raw size = %d", replicas.size(), raw.size());

    nfaulty = 1;//(replicas.size() - 1) / 3;
    HOTSTUFF_LOG_INFO("nfaulty = %zu", nfaulty);



//    if(reconfig_client==1)
//    {
//
//        salticidae::TimerEvent timer_event = salticidae::TimerEvent(ec, [](salticidae::TimerEvent &)
//        {
//            HOTSTUFF_LOG_INFO(" timer event triggered ");
//
//        });
//
//        timer_event.add(20);
//
//    }



    connect_all();
    while (try_send());


    int interval_ms = 1000;



    ec.dispatch();

//#ifdef HOTSTUFF_ENABLE_BENCHMARK
    for (const auto &e: elapsed)
    {
        char fmt[64];
        struct tm *tmp = localtime(&e.first.tv_sec);
        strftime(fmt, sizeof fmt, "%Y-%m-%d %H:%M:%S.%%06u [hotstuff info] %%.6f\n", tmp);
        fprintf(stdout, fmt, e.first.tv_usec, e.second);
    }
//#endif
    return 0;
}